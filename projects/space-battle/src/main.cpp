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
#include <esp_sleep.h>

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

// Tap tracking. A press is a button tap only if it stays near where it started
// and is released quickly; otherwise it is a camera pan, and the camera must
// still receive it. This is why the button cannot simply act on touch-down.
bool     pressActive   = false;
bool     pressIsTap    = false;   // still within slop and time budget
Vec2     pressStart{0, 0};
uint32_t pressStartMs  = 0;
uint32_t buttonShownMs = 0;       // when the control was last made visible
bool     sleepCandidate = false;

void enterDeepSleep() {
  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(0x10); // ILI9341 sleep-in
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

bool inMuteButton(const Vec2 &p) {
  float dx = p.x - MUTE_ICON_X, dy = p.y - MUTE_ICON_Y;
  return dx * dx + dy * dy <= (float)MUTE_HIT_R * MUTE_HIT_R;
}

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

  if (n > 0) {
    // Any touch reveals the control, so it is there when wanted but not
    // permanently painted over the artwork.
    buttonShownMs = nowMs;

    if (!pressActive) {
      pressActive  = true;
      pressStart   = p0;
      pressStartMs = nowMs;
      sleepCandidate = (n == 1);
      // Only a single-finger press starting on the button can be a tap.
      pressIsTap   = (n == 1) && inMuteButton(p0);
    } else {
      float travel = (p0 - pressStart).len();
      if (n > 1 || travel > TAP_SLOP_PX) sleepCandidate = false;
      if (pressIsTap && (n > 1 || travel > TAP_SLOP_PX ||
                         nowMs - pressStartMs > TAP_MAX_MS))
        pressIsTap = false;
      if (sleepCandidate && nowMs - pressStartMs >= 2000) enterDeepSleep();
    }

    // The camera sees the press either way. A tap moves it imperceptibly, and
    // withholding it until the gesture resolves would make panning feel laggy.
    camera.onTouch(n, p0, p1, nowMs);

  } else {
    if (pressActive && pressIsTap) {
      audio.setMuted(!audio.muted());
      buttonShownMs = nowMs;
    }
    pressActive = false;
    pressIsTap  = false;
    sleepCandidate = false;
    camera.onRelease(nowMs);
  }

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

  // Fade the button out once it has gone unused for a while.
  uint32_t shownFor = nowMs - buttonShownMs;
  float alpha;
  if (buttonShownMs == 0)                  alpha = 0.0f;
  else if (shownFor < BUTTON_VISIBLE_MS)   alpha = 1.0f;
  else if (shownFor < BUTTON_VISIBLE_MS + BUTTON_FADE_MS)
    alpha = 1.0f - (float)(shownFor - BUTTON_VISIBLE_MS) / BUTTON_FADE_MS;
  else                                     alpha = 0.0f;

  renderer.draw(battle, camera, audio.muted(), alpha);
}
