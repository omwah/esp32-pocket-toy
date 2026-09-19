// Procedural ESP32 adaptation inspired by Adafruit's Uncanny_Eyes.
// Original concept and eye code: Phil Burgess / Adafruit Industries (MIT).
// https://github.com/adafruit/Uncanny_Eyes
#include "eyes.h"
#include "config.h"
#include "eye_assets.h"
#include <FFat.h>
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
  _sclera = (uint16_t *)ps_malloc(EYE_MAX_SCLERA_PIXELS * sizeof(uint16_t));
  _iris = (uint16_t *)ps_malloc(EYE_MAX_IRIS_PIXELS * sizeof(uint16_t));
  _upper = (uint8_t *)ps_malloc(EYE_MAX_LID_PIXELS);
  _lower = (uint8_t *)ps_malloc(EYE_MAX_LID_PIXELS);
  if (!_sclera || !_iris || !_upper || !_lower || !loadStyle(0)) return false;
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

  if (nowMs - _lastBatterySample >= 30000) sampleBattery();
  if (!_blinkStart && nowMs >= _nextBlink) _blinkStart = nowMs;
  if (_blinkStart && nowMs - _blinkStart >= 240) {
    _blinkStart = 0;
    _nextBlink = nowMs + random(1800, 6200);
    if (random(5) == 0) _nextBlink = nowMs + 180; // occasional double blink
  }
}

const char *Eyes::styleName() const { return EYE_ASSETS[_style].name; }
const char *Eyes::styleNameAt(uint8_t style) const {
  return style < EYE_ASSET_COUNT ? EYE_ASSETS[style].name : "Unknown";
}
uint8_t Eyes::styleCount() const { return EYE_ASSET_COUNT; }

void Eyes::sampleBattery() {
  _lastBatterySample = millis();
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

bool Eyes::loadStyle(uint8_t style) {
  if (style >= EYE_ASSET_COUNT) return false;
  const EyeAssetInfo &asset = EYE_ASSETS[style];
  File file = FFat.open(asset.path, FILE_READ);
  uint8_t header[16];
  if (!file || file.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, "EYE1", 4) != 0) {
    Serial.printf("eye asset unavailable: %s\n", asset.path);
    return false;
  }
  auto u16 = [&](int offset) { return (uint16_t)(header[offset] | (header[offset + 1] << 8)); };
  if (u16(4) != asset.scleraW || u16(6) != asset.scleraH ||
      u16(8) != asset.irisW || u16(10) != asset.irisH ||
      u16(12) != asset.lidW || u16(14) != asset.lidH) return false;
  size_t scleraBytes = (size_t)asset.scleraW * asset.scleraH * 2;
  size_t irisBytes = (size_t)asset.irisW * asset.irisH * 2;
  size_t lidBytes = (size_t)asset.lidW * asset.lidH;
  bool ok = file.read((uint8_t *)_sclera, scleraBytes) == scleraBytes &&
            file.read((uint8_t *)_iris, irisBytes) == irisBytes &&
            file.read(_upper, lidBytes) == lidBytes &&
            file.read(_lower, lidBytes) == lidBytes;
  file.close();
  if (!ok) Serial.printf("eye asset truncated: %s\n", asset.path);
  return ok;
}

void Eyes::nextStyle() { setStyle((_style + 1) % EYE_ASSET_COUNT); }

void Eyes::previousStyle() {
  setStyle((_style + EYE_ASSET_COUNT - 1) % EYE_ASSET_COUNT);
}

void Eyes::setStyle(uint8_t style) {
  if (style >= EYE_ASSET_COUNT || style == _style || !loadStyle(style)) return;
  _style = style;
  showControls();
  _blinkStart = _styleChangedAt;
}

void Eyes::drawEye(int cx, float blink) {
  const EyeAssetInfo &asset = EYE_ASSETS[_style];
  const bool fixedGaze = false;
  const float gx = _gazeX;
  const float gy = _gazeY;
  const int eyeIndex = cx > SCREEN_W / 2 ? 1 : 0;
  static const uint8_t pupilRadii[] = {
    14,14,14,14,14,14,14,23,11,14,16, // existing styles
    14,10,14,0,25,15,14,1,8,10,14,14  // Monster M4SK artwork
  };
  static const uint8_t irisRadii[] = {
    40,80,80,64,40,40,64,52,40,52,57,
    43,55,25,63,58,58,50,60,35,45,38,40
  };
  const int pupilRadius = pupilRadii[_style];
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
      uint8_t upper = _upper[lidY * asset.lidW + lidX];
      uint8_t lower = _lower[lidY * asset.lidW + lidX];
      uint8_t baseUpper = (_style == 10 || fixedGaze) ? 0 : constrain(55 + gy * 35, 0, 100);
      uint8_t baseLower = (_style == 10 || fixedGaze) ? 0 : constrain(55 - gy * 25, 0, 100);
      uint8_t upperThreshold = baseUpper + (254 - baseUpper) * blink;
      uint8_t lowerThreshold = baseLower + (254 - baseLower) * blink;
      bool covered = upper <= upperThreshold || lower <= lowerThreshold;
      // Anime eyes stay fully round between blinks so the entire oversized
      // iris is visible; the original maps return only during the blink.
      if (_style == 10 && blink < 0.01f) covered = false;
      if (covered) {
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
      uint16_t colour = _sclera[sy * asset.scleraW + sx];

      int px = x - (cx + lroundf(gx * GAZE_RANGE_X));
      int py = y - (EYE_Y + lroundf(gy * GAZE_RANGE_Y));
      float radius = sqrtf((float)(px * px + py * py));
      bool slit;
      if (_style == 1 || _style == 4 || _style == 6 || _style == 12 || _style == 20)
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
        // Preserve the animated texture rotation from the Monster M4SK
        // Demon and Doom Spiral configurations.
        if (_style == 12)
          angle += millis() * (eyeIndex ? 0.001885f : -0.001885f); // +/-18 RPM
        else if (_style == 14)
          angle += millis() * (eyeIndex ? 0.007330f : 0.008378f);  // 70/80 RPM
        else if (_style == 17)
          angle += millis() * 0.000628f; // Hypno Red: slow 6 RPM spiral
        int ix = (int)((angle + PI) * asset.irisW / (2.0f * PI));
        ix = (ix % asset.irisW + asset.irisW) % asset.irisW;
        if (_style == 14 && eyeIndex) ix = asset.irisW - 1 - ix;
        // Do not mirror angular texture coordinates. Both eyes share one
        // light source, so painted highlights must point the same direction.
        // Upstream's polar table stores distance inward from the iris edge:
        // row zero is the outside, higher rows approach the pupil.
        int iy = constrain((int)((irisRadius - radius) * asset.irisH / irisRadius),
                           0, asset.irisH - 1);
        colour = _iris[iy * asset.irisW + ix];
      }

      if (_style == 10) {
        // Large catchlights plus animated star glints, all attached to the iris.
        int hx1=px+(eyeIndex?13:17), hy1=py+(eyeIndex?16:20);
        int hx2=px-(eyeIndex?17:13), hy2=py+(eyeIndex?13:10);
        if (hx1*hx1+hy1*hy1<72 || hx2*hx2+hy2*hy2<14) colour=TFT_WHITE;

        static const int8_t stars[][2]={{-25,-5},{19,-25},{27,17},{-17,29},{7,35}};
        uint32_t phase=millis()/140;
        for (uint8_t i=0;i<5;++i) {
          // Offset each right-eye star independently instead of cloning the
          // left eye's constellation. Twinkle phases differ as well.
          int starX=stars[i][0]+(eyeIndex?((i*7)%9-4):0);
          int starY=stars[i][1]+(eyeIndex?((i*5)%7-3):0);
          int sx=px-starX, sy=py-starY;
          // Each star pulses on a different phase. At peak it grows a crisp
          // four-point cross, producing a visible glimmer rather than noise.
          int reach=((phase+i*3+eyeIndex*5)%11<4)?3:1;
          if ((abs(sx)==0 && abs(sy)<=reach) ||
              (abs(sy)==0 && abs(sx)<=reach) ||
              (reach==3 && abs(sx)==1 && abs(sy)==1)) {
            colour=(i&1)?TFT_WHITE:rgb(150,220,255);
            break;
          }
        }
      }
      _frame->drawPixel(x, y, colour);
    }
  }
}

void Eyes::draw(bool soundPresent, bool muted, bool wifiConfigured,
                bool wifiConnected, bool provisioning, const char *setupSsid,
                const char *setupPassword, bool showIpAddress,
                const char *ipAddress) {
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

    // Compact Wi-Fi glyph immediately left of the battery percentage.
    uint16_t wifiColour = wifiConnected ? TFT_GREEN :
      (wifiConfigured ? TFT_YELLOW : TFT_RED);
    const int wx = SCREEN_W - 55, wy = 18;
    // Two thick, rounded-looking concentric bands and a round centre point,
    // matching the familiar solid Wi-Fi mark rather than angular chevrons.
    for (int dy = -15; dy <= 0; ++dy) {
      for (int dx = -15; dx <= 15; ++dx) {
        int r2 = dx * dx + dy * dy;
        bool outer = r2 >= 121 && r2 <= 196 && dy < -abs(dx) / 4;
        bool inner = r2 >= 36 && r2 <= 81 && dy < -abs(dx) / 4;
        if (outer || inner) _frame->drawPixel(wx + dx, wy + dy, wifiColour);
      }
    }
    _frame->fillCircle(wx, wy, 3, wifiColour);
    if (!wifiConfigured) {
      _frame->drawLine(wx - 13, wy - 14, wx + 12, wy + 2, TFT_RED);
      _frame->drawLine(wx - 12, wy - 14, wx + 13, wy + 2, TFT_RED);
    }
  }

  // Provisioning details remain visible even when the touch controls time out.
  if (provisioning || showIpAddress) {
    char status[64];
    if (provisioning)
      snprintf(status, sizeof(status), "%s  %s", setupSsid, setupPassword);
    else
      snprintf(status, sizeof(status), "IP: %s", ipAddress);
    _frame->setTextDatum(TC_DATUM);
    _frame->setTextColor(TFT_CYAN, rgb(7, 3, 10));
    _frame->drawString(status, SCREEN_W / 2, 27, 1);
  }
  _frame->pushSprite(0, 0);
}
