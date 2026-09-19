// Procedural ESP32 adaptation inspired by Adafruit's Uncanny_Eyes.
// Original concept and eye code: Phil Burgess / Adafruit Industries (MIT).
// https://github.com/adafruit/Uncanny_Eyes
#include "eyes.h"
#include "config.h"
#include "eye_assets.h"
#include <math.h>

namespace {
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
uint32_t hash2(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
}

bool Eyes::begin(TFT_eSPI &display) {
  _display = &display;
  _frame = new TFT_eSprite(&display);
  _frame->setColorDepth(16);
  if (!_frame->createSprite(SCREEN_W, SCREEN_H)) return false;
  uint32_t now = millis();
  _nextBlink = now + random(1200, 3500);
  _holdEnd = now;
  chooseTarget(now);
  sampleBattery();
  return true;
}

void Eyes::chooseTarget(uint32_t nowMs) {
  _fromX = _gazeX; _fromY = _gazeY;
  // Uniform points in an ellipse look less mechanical than independent axes.
  float a = random(0, 6284) * 0.001f;
  float r = sqrtf(random(0, 1001) / 1000.0f);
  _toX = cosf(a) * r; _toY = sinf(a) * r;
  _moveStart = nowMs;
  _moveEnd = nowMs + random(180, 650);
  _holdEnd = _moveEnd + random(350, 2600);
}

float Eyes::blinkAmount(uint32_t nowMs) const {
  if (!_blinkStart) return 0;
  uint32_t elapsed = nowMs - _blinkStart;
  constexpr uint32_t CLOSE_MS = 85, HOLD_MS = 30, OPEN_MS = 125;
  if (elapsed < CLOSE_MS) return smooth(elapsed / (float)CLOSE_MS);
  if (elapsed < CLOSE_MS + HOLD_MS) return 1;
  if (elapsed < CLOSE_MS + HOLD_MS + OPEN_MS)
    return 1.0f - smooth((elapsed - CLOSE_MS - HOLD_MS) / (float)OPEN_MS);
  return 0;
}

void Eyes::update(uint32_t nowMs, bool touched, float touchX, float touchY) {
  if (touched) {
    float targetX = constrain((touchX - SCREEN_W * .5f) / (SCREEN_W * .5f), -1.0f, 1.0f);
    float targetY = constrain((touchY - SCREEN_H * .5f) / (SCREEN_H * .5f), -1.0f, 1.0f);
    _gazeX += (targetX - _gazeX) * .22f;
    _gazeY += (targetY - _gazeY) * .22f;
    if (!_touchWasDown && !_blinkStart) _blinkStart = nowMs;
  } else {
    if (nowMs >= _holdEnd) chooseTarget(nowMs);
    if (nowMs < _moveEnd) {
      float t = smooth((nowMs - _moveStart) / (float)(_moveEnd - _moveStart));
      _gazeX = _fromX + (_toX - _fromX) * t;
      _gazeY = _fromY + (_toY - _fromY) * t;
    } else if (_moveEnd) {
      _gazeX = _toX; _gazeY = _toY;
    }
  }
  _touchWasDown = touched;

  if (!_blinkStart && nowMs >= _nextBlink) _blinkStart = nowMs;
  if (_blinkStart && nowMs - _blinkStart >= 240) {
    _blinkStart = 0;
    _nextBlink = nowMs + random(1800, 6200);
    if (random(5) == 0) _nextBlink = nowMs + 180; // occasional double blink
  }
}

const char *Eyes::styleName() const { return EYE_ASSETS[_style].name; }

void Eyes::sampleBattery() {
  analogSetPinAttenuation(BATTERY_ADC, ADC_11db);
  uint32_t totalMv = 0;
  for (int i = 0; i < 16; ++i) totalMv += analogReadMilliVolts(BATTERY_ADC);
  int batteryMv = (totalMv / 16) * 2; // R14/R15 1:1 voltage divider
  if (batteryMv < 2500) { _batteryPercent = -1; return; } // no battery

  // Approximate a resting single-cell LiPo discharge curve. A percentage is
  // inherently approximate while charging or under load, but more useful than
  // treating voltage as linear between 3.3 and 4.2 V.
  static const int mv[]  = {3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200};
  static const int pct[] = {   0,    5,   10,   15,   30,   50,   65,   80,   90,  100};
  if (batteryMv <= mv[0]) _batteryPercent = 0;
  else if (batteryMv >= mv[9]) _batteryPercent = 100;
  else for (int i = 1; i < 10; ++i) {
    if (batteryMv <= mv[i]) {
      _batteryPercent = pct[i-1] + (batteryMv - mv[i-1]) *
                        (pct[i] - pct[i-1]) / (mv[i] - mv[i-1]);
      break;
    }
  }
}

void Eyes::showControls() {
  _styleChangedAt = millis();
  sampleBattery();
}

void Eyes::nextStyle() {
  _style = (_style + 1) % EYE_ASSET_COUNT;
  showControls();
  _blinkStart = _styleChangedAt;
}

void Eyes::previousStyle() {
  _style = (_style + EYE_ASSET_COUNT - 1) % EYE_ASSET_COUNT;
  showControls();
  _blinkStart = _styleChangedAt;
}

void Eyes::drawEye(int cx, float blink) {
  const EyeAsset &asset = EYE_ASSETS[_style];
  const bool fixedGaze = false;
  const float gx = _gazeX;
  const float gy = _gazeY;
  const int eyeIndex = cx > SCREEN_W / 2 ? 1 : 0;
  const int pupilRadius = (_style == 7) ? 23 : ((_style == 8) ? 11 : 14);
  // Match the IRIS_WIDTH encoded by each upstream style. Some flat cartoon
  // styles intentionally use a 1x1 colour texture spread over a large iris.
  static const uint8_t irisRadii[] = {40, 80, 80, 64, 40, 40, 64, 52, 40, 52};
  const int irisRadius = irisRadii[_style];

  for (int y = EYE_Y - EYE_HALF_H; y < EYE_Y + EYE_HALF_H; ++y) {
    for (int x = cx - EYE_HALF_W; x < cx + EYE_HALF_W; ++x) {
      // Sample the original 128x128 viewport directly. The oval envelope
      // removes transparent corner pixels without inventing extra artwork.
      int mapX = x - cx + 64;             // 0..127
      int mapY = y - (EYE_Y - 64);        // 0..127
      int artX = eyeIndex ? mapX : 127 - mapX;

      float edgeX = (x - cx + 0.5f) / EYE_HALF_W;
      float halfHeight = EYE_HALF_H * sqrtf(fmaxf(0.0f, 1.0f - edgeX * edgeX));
      if (fabsf(y - EYE_Y + 0.5f) > halfHeight) {
        _frame->drawPixel(x, y, rgb(7, 3, 10));
        continue;
      }

      int sampledArtX = constrain(artX, 0, 127);
      int lidX = sampledArtX + ((asset.lidW >= 256) ? eyeIndex * 128 : 0);
      lidX = constrain(lidX, 0, asset.lidW - 1);
      int lidY = constrain(mapY * asset.lidH / 128, 0, asset.lidH - 1);
      uint8_t upper = asset.upper[lidY * asset.lidW + lidX];
      uint8_t lower = asset.lower[lidY * asset.lidW + lidX];
      uint8_t baseUpper = fixedGaze ? 0 : constrain(55 + gy * 35, 0, 100);
      uint8_t baseLower = fixedGaze ? 0 : constrain(55 - gy * 25, 0, 100);
      uint8_t upperThreshold = baseUpper + (254 - baseUpper) * blink;
      uint8_t lowerThreshold = baseLower + (254 - baseLower) * blink;
      if (upper <= upperThreshold || lower <= lowerThreshold) {
        // The framebuffer has no alpha channel; painting the scene background
        // is the equivalent of transparency around and over the eye.
        _frame->drawPixel(x, y, rgb(7, 3, 10));
        continue;
      }

      // Gaze pans a 128x128 window over the larger sclera source image.
      int sx = (asset.scleraW - 128) / 2 + sampledArtX - lroundf((eyeIndex ? gx : -gx) * GAZE_RANGE_X);
      int sy = (asset.scleraH - 128) / 2 + mapY - lroundf(gy * GAZE_RANGE_Y);
      sx = constrain(sx, 0, asset.scleraW - 1);
      sy = constrain(sy, 0, asset.scleraH - 1);
      uint16_t colour = asset.sclera[sy * asset.scleraW + sx];

      int px = x - (cx + lroundf(gx * GAZE_RANGE_X));
      int py = y - (EYE_Y + lroundf(gy * GAZE_RANGE_Y));
      float radius = sqrtf((float)(px * px + py * py));
      bool slit;
      if (_style == 1 || _style == 4 || _style == 6)
        slit = abs(px) < 3 + abs(py) / 18 && abs(py) < irisRadius;
      else if (_style == 3)
        slit = abs(py) < 4 + abs(px) / 20 && abs(px) < irisRadius;
      else if (_style == 9) { // deer's wide horizontal pupil
        float nx = px / 31.0f, ny = py / 7.0f;
        slit = nx * nx + ny * ny < 1.0f;
      } else
        slit = radius < pupilRadius;

      if (slit) {
        colour = (_style == 5) ? rgb(255, 20, 8) : TFT_BLACK;
      } else if (radius < irisRadius && asset.irisW > 0) {
        float angle = atan2f((float)py, (float)px);
        int ix = (int)((angle + PI) * asset.irisW / (2.0f * PI)) % asset.irisW;
        // Do not mirror angular texture coordinates. Both eyes share one
        // light source, so painted highlights must point the same direction.
        // Upstream's polar table stores distance inward from the iris edge:
        // row zero is the outside, higher rows approach the pupil.
        int iy = constrain((int)((irisRadius - radius) * asset.irisH / irisRadius),
                           0, asset.irisH - 1);
        colour = asset.iris[iy * asset.irisW + ix];
      }
      _frame->drawPixel(x, y, colour);
    }
  }
}

void Eyes::draw(bool soundPresent, bool muted) {
  _frame->fillSprite(rgb(7, 3, 10));
  float blink = blinkAmount(millis());
  drawEye(EYE_X[0], blink);
  drawEye(EYE_X[1], blink);
  if (controlsVisible(millis())) {
    _frame->setTextDatum(TC_DATUM);
    _frame->setTextColor(TFT_WHITE, rgb(7, 3, 10));
    _frame->drawString(styleName(), SCREEN_W / 2, 5, 2);

    uint16_t soundColour = !soundPresent ? TFT_DARKGREY : (muted ? TFT_RED : TFT_GREEN);
    const char *soundLabel = !soundPresent ? "NO SND" : (muted ? "MUTED" : "SOUND");
    _frame->drawRoundRect(3, 3, 52, 22, 4, soundColour);
    _frame->setTextDatum(MC_DATUM);
    _frame->setTextColor(soundColour, rgb(7, 3, 10));
    _frame->drawString(soundLabel, 29, 14, 1);

    // Style navigation stays clear of the eyes and uses generous touch areas.
    _frame->drawRoundRect(3, SCREEN_H - 35, 52, 31, 5, TFT_WHITE);
    _frame->drawRoundRect(SCREEN_W / 2 - 29, SCREEN_H - 35, 58, 31, 5, TFT_CYAN);
    _frame->drawRoundRect(SCREEN_W - 55, SCREEN_H - 35, 52, 31, 5, TFT_WHITE);
    _frame->setTextColor(TFT_WHITE, rgb(7, 3, 10));
    _frame->drawString("<", 29, SCREEN_H - 20, 2);
    _frame->setTextColor(TFT_CYAN, rgb(7, 3, 10));
    _frame->drawString("FLIP", SCREEN_W / 2, SCREEN_H - 20, 1);
    _frame->setTextColor(TFT_WHITE, rgb(7, 3, 10));
    _frame->drawString(">", SCREEN_W - 29, SCREEN_H - 20, 2);

    char battery[8];
    if (_batteryPercent < 0) snprintf(battery, sizeof(battery), "--%%");
    else snprintf(battery, sizeof(battery), "%d%%", _batteryPercent);
    uint16_t batteryColour = _batteryPercent < 0 ? TFT_LIGHTGREY :
      (_batteryPercent <= 20 ? TFT_RED :
       (_batteryPercent <= 50 ? TFT_YELLOW : TFT_GREEN));
    _frame->setTextDatum(TR_DATUM);
    _frame->setTextColor(batteryColour, rgb(7, 3, 10));
    _frame->drawString(battery, SCREEN_W - 4, 5, 2);
  }
  _frame->pushSprite(0, 0);
}
