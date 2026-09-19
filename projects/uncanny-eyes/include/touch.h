#pragma once

#include <Arduino.h>

struct TouchPoint { float x, y; };

class Touch {
public:
  bool begin();
  int read(TouchPoint &p);
  void setFlipped(bool flipped) { _flipped = flipped; }
private:
  bool _ok = false;
  bool _flipped = false;
};
