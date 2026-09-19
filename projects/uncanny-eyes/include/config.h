#pragma once

#include <Arduino.h>

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

constexpr uint8_t TOUCH_I2C_ADDR = 0x38;
constexpr int TOUCH_SDA = 16;
constexpr int TOUCH_SCL = 15;
constexpr int TOUCH_RST = 18;
constexpr int TOUCH_INT = 17;
constexpr int BATTERY_ADC = 9; // 1:1 divider; measured voltage is ADC x 2

constexpr uint8_t ES8311_I2C_ADDR = 0x18;
constexpr int SPK_ENABLE = 1; // active low
constexpr int I2S_MCLK = 4;
constexpr int I2S_BCLK = 5;
constexpr int I2S_LRCK = 7;
constexpr int I2S_DOUT = 8;
constexpr int AUDIO_RATE = 16000;

constexpr int EYE_Y = 120;
constexpr int EYE_X[2] = {82, 238};
// Render the source artwork at its native 128x128 viewport.
constexpr int EYE_HALF_W = 64;
constexpr int EYE_HALF_H = 64;
constexpr int IRIS_RADIUS = 36;
constexpr int PUPIL_RADIUS = 14;
constexpr int GAZE_RANGE_X = 17;
constexpr int GAZE_RANGE_Y = 12;
