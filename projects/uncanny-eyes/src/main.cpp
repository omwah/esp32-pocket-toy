// Animated eyes for the Hosyond ES3C28P ESP32-S3 display.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "eyes.h"
#include "touch.h"
#include "audio.h"
#include "config.h"

TFT_eSPI display;
Eyes eyes;
Touch touch;
Audio audio;
uint32_t nextSoundAt = 0;
bool pressActive = false;
TouchPoint pressStart{};
uint32_t pressStartedAt = 0;
bool sleepCandidate = false;

void enterDeepSleep() {
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

  if (!eyes.begin(display)) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString("framebuffer allocation failed", 8, 8, 2);
    while (true) delay(1000);
  }
  if (!touch.begin()) Serial.println("touch unavailable; using autonomous gaze");
  audio.begin();
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
      eyes.nextStyle();
      nextSoundAt = now + random(8000, 20000);
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

  eyes.update(now, down, point.x, point.y);
  if ((int32_t)(now - nextSoundAt) >= 0) {
    audio.playStyle(eyes.style());
    nextSoundAt = now + random(15000, 45000);
  }
  audio.update();
  eyes.draw();
  delay(1);
}
