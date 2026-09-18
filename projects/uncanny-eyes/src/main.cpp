// Animated eyes for the Hosyond ES3C28P ESP32-S3 display.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "eyes.h"
#include "touch.h"

TFT_eSPI display;
Eyes eyes;
Touch touch;
bool pressActive = false;
TouchPoint pressStart{};
uint32_t pressStartedAt = 0;

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
  } else if (!down && pressActive) {
    // A short, stationary gesture selects the next design. Drags only steer.
    if (now - pressStartedAt <= 350) eyes.nextStyle();
    pressActive = false;
  } else if (down && pressActive) {
    float dx = point.x - pressStart.x, dy = point.y - pressStart.y;
    if (dx * dx + dy * dy > 225) pressStartedAt = now - 1000;
  }

  eyes.update(now, down, point.x, point.y);
  eyes.draw();
  delay(1);
}
