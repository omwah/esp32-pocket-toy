#pragma once
#include "config.h"
#include "vec2.h"

// FT6336G capacitive controller. Reports up to two points.
// Raw coordinates arrive in the panel's portrait frame and are converted to
// landscape screen pixels here -- see DEVICE.md for how the mapping was derived.
class Touch {
public:
  bool begin();
  // Returns the number of active points (0..2) and fills p0/p1.
  int  read(Vec2 &p0, Vec2 &p1);
  bool present() const { return _ok; }

private:
  bool _ok = false;
};
