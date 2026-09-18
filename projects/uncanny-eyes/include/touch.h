#pragma once

#include <Arduino.h>

struct TouchPoint { float x, y; };

class Touch {
public:
  bool begin();
  int read(TouchPoint &p);
private:
  bool _ok = false;
};
