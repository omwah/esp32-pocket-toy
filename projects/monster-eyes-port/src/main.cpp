#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "composite_tft_display.h"
#include "monster_controller.h"
#include "touch.h"
#include "board_config.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
MonsterController monster(backend);
Touch touch;
bool wasTouched = false;
bool sleepCandidate = false;
uint32_t touchStartedAt = 0;
int touchStartX = 0;
int touchStartY = 0;
int batteryPercent = -1;
bool controlsVisible = false;
bool controlsDirty = false;
bool flipped = false;
uint32_t controlsUntil = 0;
bool backgroundPending = true;
uint8_t lastRenderedStyle = 0xFF;

void sampleBattery() {
  analogSetPinAttenuation(BATTERY_ADC, ADC_11db);
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) totalMv += analogReadMilliVolts(BATTERY_ADC);
  int batteryMv = int(totalMv / 16) * 2;
  if (batteryMv < 2500) { batteryPercent = -1; return; }
  static const int mv[] = {3300,3400,3500,3600,3700,3800,3900,4000,4100,4200};
  static const int pct[] = {0,5,10,15,30,50,65,80,90,100};
  if (batteryMv <= mv[0]) batteryPercent = 0;
  else if (batteryMv >= mv[9]) batteryPercent = 100;
  else for (int i = 1; i < 10; ++i) if (batteryMv <= mv[i]) {
    batteryPercent = pct[i-1] + (batteryMv - mv[i-1]) *
                     (pct[i] - pct[i-1]) / (mv[i] - mv[i-1]);
    break;
  }
}

void enterDeepSleep() {
  display.writecommand(TFT_DISPOFF);
  display.writecommand(0x10);
  digitalWrite(TFT_BL, LOW);
  pinMode(AUDIO_AMP_ENABLE, OUTPUT);
  digitalWrite(AUDIO_AMP_ENABLE, HIGH);
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  pinMode(WAKE_BUTTON, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(WAKE_BUTTON, 0);
  delay(50);
  esp_deep_sleep_start();
}

void drawScreenBackground() {
  const uint16_t color = monster.screenBackground();
  display.fillRect(0, 0, SCREEN_W, 56, color);
  display.fillRect(0, 184, SCREEN_W, SCREEN_H - 184, color);
  display.fillRect(0, 56, 18, 128, color);
  display.fillRect(146, 56, 28, 128, color);
  display.fillRect(302, 56, 18, 128, color);
}

void showControls(uint32_t now) {
  if (!controlsVisible) {
    sampleBattery();
    controlsDirty = true;
  }
  controlsVisible = true;
  controlsUntil = now + 5000;
}

void drawControls() {
  display.fillRect(0, 0, SCREEN_W, 28, TFT_DARKGREY);
  display.setTextDatum(TC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  display.drawString(monster.styleName(monster.style()), SCREEN_W / 2, 5, 2);
  char battery[8];
  if (batteryPercent < 0) snprintf(battery, sizeof(battery), "--%%");
  else snprintf(battery, sizeof(battery), "%d%%", batteryPercent);
  uint16_t batteryColor = batteryPercent < 0 ? TFT_LIGHTGREY :
      (batteryPercent <= 20 ? TFT_RED : (batteryPercent <= 50 ? TFT_YELLOW : TFT_GREEN));
  display.setTextDatum(TR_DATUM);
  display.setTextColor(batteryColor, TFT_DARKGREY);
  display.drawString(battery, SCREEN_W - 4, 5, 2);
  display.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, TFT_DARKGREY);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  display.drawString("< PREV", SCREEN_W / 6, SCREEN_H - 20, 2);
  display.drawString("FLIP", SCREEN_W / 2, SCREEN_H - 20, 2);
  display.drawString("NEXT >", SCREEN_W * 5 / 6, SCREEN_H - 20, 2);
}

void hideControls() {
  if (!controlsVisible) return;
  controlsVisible = false;
  controlsDirty = false;
  display.fillRect(0, 0, SCREEN_W, 28, monster.screenBackground());
  display.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, monster.screenBackground());
}

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  display.init();
  display.setRotation(1);
  // Monster Eyes produces native-endian RGB565 words. TFT_eSPI's pushImage()
  // needs byte swapping enabled before sending those words over SPI.
  display.setSwapBytes(true);
  display.fillScreen(TFT_BLACK);

  if (!monster.begin()) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString("Monster Eyes failed", 4, 4, 2);
    while (true) delay(1000);
  }
  if (!touch.begin()) Serial.println("touch unavailable; using autonomous gaze");
  Serial.println("Monster Eyes composite TFT backend ready");
}

void loop() {
  TouchPoint point{};
  bool touched = touch.read(point) > 0;
  uint32_t now = millis();
  if (touched) {
    if (!wasTouched) {
      touchStartedAt = now;
      touchStartX = point.x;
      touchStartY = point.y;
      sleepCandidate = true;
    }
    // This backend's map-space axes are opposite the panel's touch axes.
    float x = constrain((SCREEN_W * 0.5f - point.x) / (SCREEN_W * 0.5f), -1.0f, 1.0f);
    // Use screen-down-positive touch input for the map-space Y coordinate.
    float y = constrain((point.y - SCREEN_H * 0.5f) / (SCREEN_H * 0.5f), -1.0f, 1.0f);
    monster.setGaze(x, y);
    float dx = point.x - touchStartX;
    float dy = point.y - touchStartY;
    if (dx * dx + dy * dy > 225) sleepCandidate = false;
    if (sleepCandidate && now - touchStartedAt >= 2000) enterDeepSleep();
  } else if (wasTouched) {
    monster.releaseGaze();
    if (now - touchStartedAt < 300) {
      if (!controlsVisible) {
        showControls(now);
      } else if (touchStartY >= SCREEN_H - 50) {
        hideControls();
        if (touchStartX < SCREEN_W / 3) monster.previousStyle();
        else if (touchStartX > SCREEN_W * 2 / 3) monster.nextStyle();
        else {
          flipped = !flipped;
          display.setRotation(flipped ? 3 : 1);
          touch.setFlipped(flipped);
          display.fillScreen(monster.screenBackground());
        }
        backgroundPending = true;
        showControls(now);
        controlsDirty = true;
      } else {
        monster.blink();
        showControls(now);
      }
    }
  }
  if (!touched) sleepCandidate = false;
  wasTouched = touched;

  static String command;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      command.trim();
      if (command == "next") {
        monster.nextStyle();
        backgroundPending = true;
        controlsDirty = controlsVisible;
      } else if (command == "previous") {
        monster.previousStyle();
        backgroundPending = true;
        controlsDirty = controlsVisible;
      }
      command = "";
    } else if (c != '\r' && command.length() < 32) command += c;
  }
  if (controlsVisible && int32_t(now - controlsUntil) >= 0) hideControls();
  if (monster.style() != lastRenderedStyle) {
    lastRenderedStyle = monster.style();
    backgroundPending = true;
  }
  monster.animate();
  if (backgroundPending) {
    drawScreenBackground();
    backgroundPending = false;
  }
  if (controlsVisible && controlsDirty) {
    drawControls();
    controlsDirty = false;
  }
}
