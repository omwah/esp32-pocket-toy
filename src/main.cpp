// Space battle screensaver for the Hosyond ESP32-S3 2.8" touch display.
//
// A starfield and a planet form the backdrop for two fleets fighting it out.
// Drag to pan, pinch to zoom; after a few idle seconds the camera resumes
// drifting toward wherever the fighting is heaviest.
//
// Board specifics, verified pin assignments and the build flags this needs are
// documented in DEVICE.md.
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "battle.h"
#include "camera.h"
#include "config.h"
#include "renderer.h"
#include "touch.h"

TFT_eSPI tft;
Renderer renderer;
Battle   battle;
Camera   camera;
Touch    touch;

uint32_t lastFrameUs = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);          // landscape 320x240
  tft.fillScreen(TFT_BLACK);

  if (!renderer.begin(tft)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("framebuffer alloc failed", 8, 8, 2);
    while (true) delay(1000);
  }

  if (!touch.begin())
    Serial.println("touch controller not responding; running without input");

  camera.begin();
  battle.begin(esp_random());

  lastFrameUs = micros();
  Serial.printf("ready: psram_free=%u\n", (unsigned)ESP.getFreePsram());
}

void loop() {
  uint32_t nowUs = micros();
  float dt = (nowUs - lastFrameUs) * 1e-6f;
  lastFrameUs = nowUs;

  // Guard against a stall producing a huge integration step.
  if (dt > 0.1f) dt = 0.1f;

  uint32_t nowMs = millis();

  Vec2 p0, p1;
  int n = touch.read(p0, p1);
  if (n > 0) camera.onTouch(n, p0, p1, nowMs);
  else       camera.onRelease(nowMs);

  battle.update(dt);
  camera.update(dt, nowMs, battle.actionCentre(), battle.actionSpread());

  renderer.draw(battle, camera);
}
