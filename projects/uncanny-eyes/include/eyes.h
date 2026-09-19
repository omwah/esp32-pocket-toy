#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class Eyes {
public:
  bool begin(TFT_eSPI &display);
  void update(uint32_t nowMs, bool touched, float touchX, float touchY);
  void nextStyle();
  void previousStyle();
  void showControls();
  bool controlsVisible(uint32_t nowMs) const { return nowMs - _styleChangedAt < 3500; }
  uint8_t style() const { return _style; }
  void draw(bool soundPresent, bool muted);
private:
  TFT_eSPI *_display = nullptr;
  TFT_eSprite *_frame = nullptr;
  float _gazeX = 0, _gazeY = 0;
  float _fromX = 0, _fromY = 0, _toX = 0, _toY = 0;
  uint32_t _moveStart = 0, _moveEnd = 0, _holdEnd = 0;
  uint32_t _blinkStart = 0, _nextBlink = 0;
  bool _touchWasDown = false;
  uint8_t _style = 0;
  uint32_t _styleChangedAt = 0;
  int _batteryPercent = -1;

  float blinkAmount(uint32_t nowMs) const;
  void chooseTarget(uint32_t nowMs);
  void drawEye(int centreX, float blink);
  void sampleBattery();
  const char *styleName() const;
};
