#include "backlight.h"

#include <Preferences.h>

namespace {

// LEDC channel 0 is unclaimed: audio runs on I2S and the panel itself on SPI,
// so nothing else in this sketch uses the PWM peripheral.
constexpr uint8_t CHANNEL = 0;
constexpr uint32_t FREQUENCY = 5000;
constexpr uint8_t RESOLUTION = 8;
// Below this the panel reads as off, which would strand the user with no way
// to find the slider again.
constexpr uint8_t MIN_PERCENT = 5;

}  // namespace

void Backlight::begin() {
    Preferences prefs;
    prefs.begin("monster-light", true);
    _percent = constrain(prefs.getUChar("brightness", 100), MIN_PERCENT, 100);
    prefs.end();
    // TFT_eSPI::init() has already claimed TFT_BL as a plain output and driven
    // it to TFT_BACKLIGHT_ON, so the timer can only be attached after that.
    ledcSetup(CHANNEL, FREQUENCY, RESOLUTION);
    ledcAttachPin(TFT_BL, CHANNEL);
    _ready = true;
    apply(_percent);
}

void Backlight::apply(uint8_t percent) {
    if (!_ready) return;
    uint32_t duty = uint32_t(percent) * 255 / 100;
    // TFT_BACKLIGHT_ON says which level lights this panel; on a board wired
    // active-low the duty cycle is the other way up.
    if (TFT_BACKLIGHT_ON == LOW) duty = 255 - duty;
    ledcWrite(CHANNEL, duty);
}

void Backlight::setPercent(uint8_t percent) {
    percent = constrain(percent, MIN_PERCENT, 100);
    if (percent == _percent) return;
    _percent = percent;
    apply(_percent);
    Preferences prefs;
    prefs.begin("monster-light", false);
    prefs.putUChar("brightness", _percent);
    prefs.end();
}

void Backlight::off() {
    if (_ready) {
        ledcDetachPin(TFT_BL);
        _ready = false;
    }
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON == HIGH ? LOW : HIGH);
}
