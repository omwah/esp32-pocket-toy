#pragma once

#include <Arduino.h>

// PWM dimming of the panel backlight. The level is a percentage, floored well
// above zero so the screen can never be turned dark enough to hide the
// controls that would turn it back up, and is persisted in NVS.
class Backlight {
public:
    void begin();
    void setPercent(uint8_t percent);
    uint8_t percent() const { return _percent; }
    // Hand the pin back to plain GPIO and drive it dark, for deep sleep.
    void off();

private:
    void apply(uint8_t percent);
    uint8_t _percent = 100;
    bool _ready = false;
};
