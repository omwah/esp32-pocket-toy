#pragma once

#include <Arduino.h>

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

constexpr uint8_t TOUCH_I2C_ADDR = 0x38;
constexpr int TOUCH_SDA = 16;
constexpr int TOUCH_SCL = 15;
constexpr int TOUCH_RST = 18;
constexpr int TOUCH_INT = 17;

constexpr int EYE_Y = 120;
constexpr int EYE_X[2] = {82, 238};
// Render the source artwork at its native 128x128 viewport.
constexpr int EYE_HALF_W = 64;
constexpr int EYE_HALF_H = 64;
constexpr int IRIS_RADIUS = 36;
constexpr int PUPIL_RADIUS = 14;
constexpr int GAZE_RANGE_X = 17;
constexpr int GAZE_RANGE_Y = 12;
