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

#include "audio.h"
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
Audio    audio;

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

  audio.begin();   // failure is not fatal; the screensaver just runs silent


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

  // Turn this tick's events into sound. Anything off screen is skipped and
  // distant events are quieter, so the mix follows whatever the camera is
  // looking at rather than the whole battlefield at once.
  if (audio.present()) {
    const BattleEvent *ev = battle.events();
    for (int i = 0; i < battle.eventCount(); i++) {
      Vec2 sp = camera.toScreen(ev[i].pos);
      if (!camera.visible(sp, 40)) continue;

      // Pan by horizontal screen position; attenuate toward the frame edges.
      float pan = (sp.x / SCREEN_W) * 2.0f - 1.0f;
      float edge = fmaxf(fabsf(pan), fabsf((sp.y / SCREEN_H) * 2.0f - 1.0f));
      float near = 1.0f - 0.45f * fminf(1.0f, edge);

      // Zoomed out there are far more audible events, so scale back to keep
      // the mix from turning into a wall of noise.
      float z = fminf(1.0f, camera.zoom() * 1.4f);

      switch (ev[i].kind) {
        case EV_FIRE_LIGHT:
          // Only a fraction of shots are voiced; every one would be mush.
          if (random(0, 100) < 22)
            audio.play(SFX_LASER, 0.70f * near * z, pan);
          break;
        case EV_FIRE_HEAVY:
          audio.play(SFX_CANNON, 1.00f * near, pan);
          break;
        case EV_HIT:
          if (random(0, 100) < 14)
            audio.play(SFX_HIT, 0.55f * near * z, pan);
          break;
        case EV_DEATH:
          if (ev[i].cls == CAPITAL) {
            audio.play(SFX_EXPLOSION, 1.40f * near, pan);
            audio.play(SFX_RUMBLE,    1.20f * near, pan);
          } else if (ev[i].cls == CRUISER) {
            audio.play(SFX_EXPLOSION, 1.10f * near, pan);
          } else if (random(0, 100) < 55) {
            audio.play(SFX_EXPLOSION, 0.90f * near * z, pan);
          }
          break;
      }
    }
  }

  audio.update();

  renderer.draw(battle, camera);
}
