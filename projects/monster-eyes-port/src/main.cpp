#include <Arduino.h>
#include <TFT_eSPI.h>
#include "composite_tft_display.h"
#include "monster_controller.h"
#include "touch.h"
#include "board_config.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
MonsterController monster(backend);
Touch touch;
bool wasTouched = false;
uint32_t touchStartedAt = 0;
int touchStartX = 0;
int touchStartY = 0;
bool controlsVisible = false;
bool flipped = false;
uint32_t controlsUntil = 0;

void showControls(uint32_t now) {
  controlsVisible = true;
  controlsUntil = now + 5000;
}

void drawControls() {
  display.fillRect(0, 0, SCREEN_W, 28, TFT_DARKGREY);
  display.setTextDatum(TC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  display.drawString(monster.styleName(monster.style()), SCREEN_W / 2, 5, 2);
  display.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, TFT_DARKGREY);
  display.setTextDatum(MC_DATUM);
  display.drawString("< PREV", SCREEN_W / 6, SCREEN_H - 20, 2);
  display.drawString("FLIP", SCREEN_W / 2, SCREEN_H - 20, 2);
  display.drawString("NEXT >", SCREEN_W * 5 / 6, SCREEN_H - 20, 2);
}

void hideControls() {
  if (!controlsVisible) return;
  controlsVisible = false;
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
    }
    // This backend's map-space axes are opposite the panel's touch axes.
    float x = constrain((SCREEN_W * 0.5f - point.x) / (SCREEN_W * 0.5f), -1.0f, 1.0f);
    // Use screen-down-positive touch input for the map-space Y coordinate.
    float y = constrain((point.y - SCREEN_H * 0.5f) / (SCREEN_H * 0.5f), -1.0f, 1.0f);
    monster.setGaze(x, y);
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
        showControls(now);
      } else {
        monster.blink();
        showControls(now);
      }
    }
  }
  wasTouched = touched;

  static String command;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      command.trim();
      if (command == "next") monster.nextStyle();
      else if (command == "previous") monster.previousStyle();
      command = "";
    } else if (c != '\r' && command.length() < 32) command += c;
  }
  if (controlsVisible && int32_t(now - controlsUntil) >= 0) hideControls();
  monster.animate();
  if (controlsVisible) drawControls();
}
