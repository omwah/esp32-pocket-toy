#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class Eyes {
public:
  bool begin(TFT_eSPI &display);
  void update(uint32_t nowMs, bool touched, float touchX, float touchY);
  void nextStyle();
  void previousStyle();
  void setStyle(uint8_t style);
  bool setStyleEnabled(uint8_t style, bool enabled);
  bool styleEnabled(uint8_t style) const;
  uint8_t enabledStyleCount() const;
  uint32_t enabledMask() const { return _enabledMask; }
  void setEnabledMask(uint32_t mask);
  void showControls();
  bool controlsVisible(uint32_t nowMs) const { return nowMs - _styleChangedAt < 3500; }
  uint8_t style() const { return _style; }
  uint8_t styleCount() const;
  const char *styleName() const;
  const char *styleNameAt(uint8_t style) const;
  int batteryPercent() const { return _batteryPercent; }
  void draw(bool soundPresent, bool muted, bool wifiConfigured,
            bool wifiConnected, bool provisioning, const char *setupSsid,
            const char *setupPassword, bool showIpAddress,
            const char *ipAddress);
private:
  TFT_eSPI *_display = nullptr;
  TFT_eSprite *_frame = nullptr;
  float _gazeX = 0, _gazeY = 0;
  float _fromX = 0, _fromY = 0, _toX = 0, _toY = 0;
  uint32_t _moveStart = 0, _moveEnd = 0, _holdEnd = 0;
  uint32_t _blinkStart = 0, _nextBlink = 0;
  bool _touchWasDown = false;
  uint8_t _style = 0;
  uint32_t _enabledMask = 0xFFFFFFFFu;
  uint32_t _styleChangedAt = 0;
  uint32_t _lastBatterySample = 0;
  int _batteryPercent = -1;
  uint16_t *_sclera = nullptr;
  uint16_t *_iris = nullptr;
  uint8_t *_upper = nullptr;
  uint8_t *_lower = nullptr;

  bool loadStyle(uint8_t style);
  float blinkAmount(uint32_t nowMs) const;
  void chooseTarget(uint32_t nowMs);
  void drawEye(int centreX, float blink);
  void sampleBattery();
};
