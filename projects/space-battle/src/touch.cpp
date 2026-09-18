#include "touch.h"
#include <Arduino.h>
#include <Wire.h>

bool Touch::begin() {
  // The controller needs a hardware reset before it will answer.
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(300);

  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);

  Wire.beginTransmission(TOUCH_I2C_ADDR);
  _ok = (Wire.endTransmission() == 0);
  return _ok;
}

int Touch::read(Vec2 &p0, Vec2 &p1) {
  if (!_ok) return 0;

  // Register 0x02: status byte (low nibble = point count) then 6 bytes per point.
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((int)TOUCH_I2C_ADDR, 11) != 11) return 0;

  uint8_t b[11];
  for (int i = 0; i < 11; i++) b[i] = Wire.read();

  int n = b[0] & 0x0F;
  if (n < 1 || n > 2) return 0;

  // Portrait raw -> landscape screen.
  auto map = [](int rx, int ry) -> Vec2 {
    return {(float)ry, (float)(SCREEN_H - rx)};
  };

  p0 = map(((b[1] & 0x0F) << 8) | b[2], ((b[3] & 0x0F) << 8) | b[4]);
  if (n == 2) p1 = map(((b[7] & 0x0F) << 8) | b[8], ((b[9] & 0x0F) << 8) | b[10]);

  return n;
}
