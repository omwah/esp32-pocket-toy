// Animated eyes for the Hosyond ES3C28P ESP32-S3 display.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <FFat.h>
#include <esp_sleep.h>
#include "eyes.h"
#include "touch.h"
#include "audio.h"
#include "config.h"
#include "web_control.h"

TFT_eSPI display;
Eyes eyes;
Touch touch;
Audio audio;
WebControl web(eyes, audio);
uint32_t nextSoundAt = 0;
bool pressActive = false;
TouchPoint pressStart{};
uint32_t pressStartedAt = 0;
bool sleepCandidate = false;
bool flipped = false;
bool showWifiIp = false;

void enterDeepSleep() {
  web.stop();
  audio.stop();
  display.writecommand(TFT_DISPOFF);
  display.writecommand(0x10); // ILI9341 sleep-in
  digitalWrite(TFT_BL, LOW);
  pinMode(1, OUTPUT);       // audio amplifier disable, active high
  digitalWrite(1, HIGH);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);

  pinMode(0, INPUT_PULLUP); // BOOT button wakes on a low level
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  delay(50);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  display.init();
  display.setRotation(1); // 320x240 landscape
  display.fillScreen(TFT_BLACK);

  if (!FFat.begin(false)) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString("FATFS assets missing", 8, 8, 2);
    display.drawString("run: pio run -t uploadfs", 8, 30, 1);
    while (true) delay(1000);
  }
  Serial.printf("assets: fatfs=%u used=%u\n", (unsigned)FFat.totalBytes(),
                (unsigned)FFat.usedBytes());

  if (!eyes.begin(display)) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString("framebuffer allocation failed", 8, 8, 2);
    while (true) delay(1000);
  }
  if (!touch.begin()) Serial.println("touch unavailable; using autonomous gaze");
  audio.begin();
  pinMode(0, INPUT_PULLUP);
  delay(20);
  web.begin(digitalRead(0) == LOW); // Hold BOOT during startup to provision Wi-Fi.
  nextSoundAt = millis() + random(15000, 45000);
  Serial.printf("eyes ready: psram_free=%u\n", (unsigned)ESP.getFreePsram());
}

void loop() {
  TouchPoint point{};
  bool down = touch.read(point) > 0;
  uint32_t now = millis();

  if (down && !pressActive) {
    pressActive = true;
    pressStart = point;
    pressStartedAt = now;
    sleepCandidate = true;
  } else if (!down && pressActive) {
    // A short, stationary gesture selects the next design. Drags only steer.
    if (now - pressStartedAt <= 350) {
      // The first tap only reveals the controls. Once visible, the explicit
      // buttons handle sound and style selection; taps elsewhere refresh them.
      if (!eyes.controlsVisible(now)) {
        eyes.showControls();
      } else if (pressStart.x <= 60 && pressStart.y <= 38 && audio.present()) {
        audio.setMuted(!audio.muted());
        eyes.showControls();
      } else if (pressStart.x >= SCREEN_W - 78 && pressStart.x <= SCREEN_W - 38 &&
                 pressStart.y <= 38) {
        showWifiIp = !showWifiIp;
        eyes.showControls();
      } else if (pressStart.x <= 65 && pressStart.y >= SCREEN_H - 45) {
        eyes.previousStyle();
        web.manualStyleSelected();
        nextSoundAt = now + random(8000, 20000);
      } else if (pressStart.x >= SCREEN_W / 2 - 40 &&
                 pressStart.x <= SCREEN_W / 2 + 40 &&
                 pressStart.y >= SCREEN_H - 45) {
        flipped = !flipped;
        display.setRotation(flipped ? 3 : 1);
        touch.setFlipped(flipped);
        eyes.showControls();
      } else if (pressStart.x >= SCREEN_W - 65 && pressStart.y >= SCREEN_H - 45) {
        eyes.nextStyle();
        web.manualStyleSelected();
        nextSoundAt = now + random(8000, 20000);
      } else {
        eyes.showControls();
      }
    }
    pressActive = false;
    sleepCandidate = false;
  } else if (down && pressActive) {
    float dx = point.x - pressStart.x, dy = point.y - pressStart.y;
    if (dx * dx + dy * dy > 225) {
      sleepCandidate = false;
      pressStartedAt = now - 1000; // also disqualify style-changing tap
    }
    if (sleepCandidate && now - pressStartedAt >= 2000) enterDeepSleep();
  }

  uint8_t styleBeforeWeb = eyes.style();
  web.update(now);
  if (eyes.style() != styleBeforeWeb) nextSoundAt = now + random(8000, 20000);
  eyes.update(now, down, point.x, point.y);
  if ((int32_t)(now - nextSoundAt) >= 0) {
    audio.playStyle(eyes.style());
    nextSoundAt = now + random(15000, 45000);
  }
  audio.update();
  String wifiIp = showWifiIp ? web.ipAddress() : String();
  eyes.draw(audio.present(), audio.muted(), web.configured(), web.connected(),
            web.provisioning(), web.setupSsid(), web.setupPassword(), showWifiIp,
            wifiIp.c_str());
  delay(1);
}
