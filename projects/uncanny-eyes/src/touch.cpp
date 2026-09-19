#include "touch.h"
#include "config.h"
#include <Wire.h>

bool Touch::begin() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  _ok = Wire.endTransmission() == 0;
  return _ok;
}

int Touch::read(TouchPoint &p) {
  if (!_ok) return 0;
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom((int)TOUCH_I2C_ADDR, 5) != 5) return 0;
  uint8_t b[5];
  for (uint8_t &v : b) v = Wire.read();
  if ((b[0] & 0x0f) == 0) return 0;
  int rawX = ((b[1] & 0x0f) << 8) | b[2];
  int rawY = ((b[3] & 0x0f) << 8) | b[4];
  float x = constrain(rawY, 0, SCREEN_W - 1);
  float y = constrain(SCREEN_H - rawX, 0, SCREEN_H - 1);
  // Rotation 3 is 180 degrees from rotation 1, so touch coordinates must
  // follow the displayed controls when the device is turned upside down.
  p.x = _flipped ? SCREEN_W - 1 - x : x;
  p.y = _flipped ? SCREEN_H - 1 - y : y;
  return 1;
}
