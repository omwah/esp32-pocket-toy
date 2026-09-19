#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Adafruit_Monster_Eyes.h>
#include "composite_tft_display.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
Adafruit_Monster_Eyes monster(&backend);

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
  Serial.println("Monster Eyes composite TFT backend ready");
}

void loop() { monster.animate(); }
