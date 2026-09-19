#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Adafruit_Monster_Eyes.h>
#include "composite_tft_display.h"
#include "touch.h"
#include "board_config.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
Adafruit_Monster_Eyes monster(&backend);
Touch touch;
bool wasTouched = false;
uint32_t touchStartedAt = 0;

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

  monster.setVerbose(Serial);
  monster.setStorageEnabled(true);
  monster.setDriveModeEnabled(false);
  monster.setConfigFile("/config.eye");
  monster.setEyeSize(128);
  monster.setEyeRadius(62);
  monster.setIrisRadius(40);
  monster.setSelfTest(false);
  if (!monster.begin()) {
    display.setTextColor(TFT_RED, TFT_BLACK);
    display.drawString(monster.errorString() ? monster.errorString() : "Monster Eyes failed", 4, 4, 2);
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
    if (!wasTouched) touchStartedAt = now;
    // This backend's map-space axes are opposite the panel's touch axes.
    float x = constrain((SCREEN_W * 0.5f - point.x) / (SCREEN_W * 0.5f), -1.0f, 1.0f);
    // Use screen-down-positive touch input for the map-space Y coordinate.
    float y = constrain((point.y - SCREEN_H * 0.5f) / (SCREEN_H * 0.5f), -1.0f, 1.0f);
    monster.setGaze(x, y);
  } else if (wasTouched) {
    monster.releaseGaze();
    if (now - touchStartedAt < 300) monster.blink();
  }
  wasTouched = touched;
  monster.animate();
}
