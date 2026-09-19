#pragma once

#include <Adafruit_Monster_Eyes.h>
#include <vector>
#include "composite_tft_display.h"

class MonsterController {
public:
  explicit MonsterController(CompositeTftDisplay &display) : _display(display) {}
  ~MonsterController();
  bool begin();
  bool refreshPackages();
  bool setStyle(uint8_t style);
  bool nextStyle();
  bool previousStyle();
  void animate() { if (_eyes) _eyes->animate(); }
  void setGaze(float x, float y) { if (_eyes) _eyes->setGaze(x, y); }
  void releaseGaze() { if (_eyes) _eyes->releaseGaze(); }
  void blink() { if (_eyes) _eyes->blink(); }
  uint8_t style() const { return _style; }
  uint8_t styleCount() const { return _packages.size(); }
  const char *styleName(uint8_t style) const;

private:
  struct Package { String id, name, config; };
  CompositeTftDisplay &_display;
  std::vector<Package> _packages;
  alignas(Adafruit_Monster_Eyes) uint8_t _storage[sizeof(Adafruit_Monster_Eyes)];
  Adafruit_Monster_Eyes *_eyes = nullptr;
  uint8_t _style = 0;
};
