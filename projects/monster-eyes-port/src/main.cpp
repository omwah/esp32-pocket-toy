#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "composite_tft_display.h"
#include "monster_controller.h"
#include "touch.h"
#include "web_control.h"
#include "board_config.h"
#include "audio.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
MonsterController monster(backend);
Touch touch;
Audio audio;
bool wasTouched = false;
bool sleepCandidate = false;
uint32_t touchStartedAt = 0;
int touchStartX = 0;
int touchStartY = 0;
int batteryPercent = -1;
WebControl web(monster, audio, batteryPercent);
bool controlsVisible = false;
bool controlsDirty = false;
bool flipped = false;
bool showWifiIp = false;
uint32_t controlsUntil = 0;
bool backgroundPending = true;
uint8_t lastRenderedStyle = 0xFF;
bool lastWifiConnected = false;
bool lastProvisioning = false;
bool lastMuted = false;

void drawSoundIcon(int x, int y) {
  uint16_t color = audio.hasSound() ? TFT_WHITE : TFT_LIGHTGREY;
  // Speaker silhouette inspired by the supplied icon; the slash alone denotes mute.
  display.fillRect(x - 11, y - 5, 5, 10, color);
  display.fillTriangle(x - 6, y - 5, x + 1, y - 11, x + 1, y + 11, color);
  for (int r = 6; r <= 11; r += 5) {
    for (int dy = -r; dy <= r; ++dy) {
      int dx = int(sqrtf(float(r * r - dy * dy)));
      if (dx >= 0) display.drawPixel(x + dx + 1, y + dy, color);
    }
  }
  if (audio.muted()) {
    display.drawLine(x - 12, y - 13, x + 14, y + 13, TFT_RED);
    display.drawLine(x - 11, y - 13, x + 15, y + 13, TFT_RED);
  }
}

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
  audio.stop();
  web.stop();
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
  const bool hasStatusLine = web.provisioning() || showWifiIp;
  const int headerHeight = hasStatusLine ? 52 : 28;
  display.fillRect(0, 0, SCREEN_W, 56, monster.screenBackground());
  display.fillRect(0, 0, SCREEN_W, headerHeight, TFT_DARKGREY);
  display.setTextDatum(TC_DATUM);
  display.setTextColor(TFT_WHITE, TFT_DARKGREY);
  display.drawString(monster.styleName(monster.style()), SCREEN_W / 2, 5, 2);
  drawSoundIcon(20, 14);
  if (web.provisioning()) {
    String setup = String(web.setupSsid()) + " / " + web.setupPassword();
    display.drawString(setup, SCREEN_W / 2, 32, 1);
  } else if (showWifiIp) {
    display.drawString(web.ipAddress(), SCREEN_W / 2, 32, 1);
  }
  uint16_t wifiColor = web.connected() ? TFT_GREEN : (web.configured() ? TFT_YELLOW : TFT_RED);
  const int wx = SCREEN_W - 47, wy = 17;
  for (int dy = -12; dy <= 0; ++dy) {
    for (int dx = -12; dx <= 12; ++dx) {
      int r2 = dx * dx + dy * dy;
      bool outer = r2 >= 81 && r2 <= 121 && dy < -abs(dx) / 4;
      bool inner = r2 >= 25 && r2 <= 49 && dy < -abs(dx) / 4;
      if (outer || inner) display.drawPixel(wx + dx, wy + dy, wifiColor);
    }
  }
  display.fillCircle(wx, wy, 2, wifiColor);
  if (!web.configured()) {
    display.drawLine(wx - 11, wy - 12, wx + 10, wy + 1, TFT_RED);
    display.drawLine(wx - 10, wy - 12, wx + 11, wy + 1, TFT_RED);
  }
  char battery[8];
  if (batteryPercent < 0) snprintf(battery, sizeof(battery), "--%%");
  else snprintf(battery, sizeof(battery), "%d%%", batteryPercent);
  uint16_t batteryColor = batteryPercent < 0 ? TFT_LIGHTGREY :
      (batteryPercent <= 20 ? TFT_RED : (batteryPercent <= 50 ? TFT_YELLOW : TFT_GREEN));
  display.setTextDatum(TR_DATUM);
  display.setTextColor(batteryColor, TFT_DARKGREY);
  // Font 2 matches the icon height; draw twice for slightly more visual weight.
  display.drawString(battery, SCREEN_W - 4, 5, 2);
  display.drawString(battery, SCREEN_W - 5, 5, 2);
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
  display.fillRect(0, 0, SCREEN_W, 56, monster.screenBackground());
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
  audio.begin();
  audio.setPackage(monster.configPath());
  lastMuted = audio.muted();
  web.begin();
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
      } else if (touchStartY < 55 && touchStartX < 45 && audio.present()) {
        audio.setMuted(!audio.muted());
        showControls(now);
        controlsDirty = true;
      } else if (touchStartY < 55 && touchStartX >= SCREEN_W - 72 && touchStartX < SCREEN_W - 22) {
        showWifiIp = !showWifiIp;
        showControls(now);
        controlsDirty = true;
      } else if (touchStartY >= SCREEN_H - 50) {
        hideControls();
        if (touchStartX < SCREEN_W / 3) { monster.previousStyle(); web.manualStyleSelected(); }
        else if (touchStartX > SCREEN_W * 2 / 3) { monster.nextStyle(); web.manualStyleSelected(); }
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

  uint8_t styleBeforeWeb = monster.style();
  web.update(now);
  audio.update(now);
  if (audio.muted() != lastMuted) {
    lastMuted = audio.muted();
    controlsDirty = controlsVisible;
  }
  if (web.connected() != lastWifiConnected || web.provisioning() != lastProvisioning) {
    lastWifiConnected = web.connected();
    lastProvisioning = web.provisioning();
    controlsDirty = controlsVisible;
  }
  if (monster.style() != styleBeforeWeb) {
    backgroundPending = true;
    controlsDirty = controlsVisible;
  }
  if (controlsVisible && int32_t(now - controlsUntil) >= 0) hideControls();
  if (monster.style() != lastRenderedStyle) {
    if (lastRenderedStyle != 0xFF) audio.setPackage(monster.configPath());
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
