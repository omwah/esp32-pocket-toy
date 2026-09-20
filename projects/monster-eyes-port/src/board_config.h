#pragma once

#include <Arduino.h>

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr uint8_t TOUCH_I2C_ADDR = 0x38;
constexpr int TOUCH_SDA = 16;
constexpr int TOUCH_SCL = 15;
constexpr int TOUCH_RST = 18;
constexpr int TOUCH_INT = 17;
constexpr int BATTERY_ADC = 9;
constexpr int AUDIO_AMP_ENABLE = 1;
constexpr uint8_t ES8311_I2C_ADDR = 0x18;
constexpr int I2S_MCLK = 4;
constexpr int I2S_BCLK = 5;
constexpr int I2S_LRCK = 7;
constexpr int I2S_DOUT = 8;
constexpr gpio_num_t WAKE_BUTTON = GPIO_NUM_0;
