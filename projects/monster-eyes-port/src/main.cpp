#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include "composite_tft_display.h"
#include "monster_controller.h"
#include "touch.h"
#include "web_control.h"
#include "board_config.h"
#include "audio.h"
#include "backlight.h"

TFT_eSPI display;
CompositeTftDisplay backend(display);
MonsterController monster(backend);
Touch touch;
Audio audio;
Backlight backlight;
bool wasTouched = false;
bool sleepCandidate = false;
uint32_t touchStartedAt = 0;
int touchStartX = 0;
int touchStartY = 0;
int batteryPercent = -1;
uint32_t nextBatterySample = 0;
WebControl web(monster, audio, batteryPercent, backlight);
bool controlsVisible = false;
bool controlsDirty = false;
// Which way up the toy is held. Defaults to flipped, which is the way it is
// actually used; the panel's rotation 1 is upside down in the case.
bool flipped = true;
uint32_t controlsUntil = 0;
bool backgroundPending = true;
uint8_t lastRenderedStyle = 0xFF;
bool lastWifiConnected = false;
bool lastProvisioning = false;
bool lastMuted = false;
bool lastWifiEnabled = true;

namespace {

// Kept in NVS beside the style and audio settings. The flip describes how the
// hardware is being held rather than a passing choice, so a reboot turning the
// picture upside down is a bug, not a reset to a sensible default.
constexpr char kPrefsNamespace[] = "monster-eyes";
constexpr char kFlipKey[] = "flipped";

void applyFlip(bool value) {
    flipped = value;
    display.setRotation(flipped ? 3 : 1);
    touch.setFlipped(flipped);
}

bool loadFlip() {
    Preferences p;
    p.begin(kPrefsNamespace, true);
    const bool stored = p.isKey(kFlipKey);
    const bool value = p.getBool(kFlipKey, true);
    p.end();
    // Worth a line: the flip cannot be seen in a screenshot, because the
    // capture mirror is filled in panel coordinates before TFT_eSPI applies
    // the rotation, so this is the only way to tell from off the toy.
    Serial.printf("flip: %s (%s)\n", value ? "on" : "off",
                  stored ? "stored" : "default");
    return value;
}

void storeFlip(bool value) {
    Preferences p;
    p.begin(kPrefsNamespace, false);
    p.putBool(kFlipKey, value);
    p.end();
}

}  // namespace

void drawSoundIcon(int x, int y) {
    uint16_t color = audio.hasSound() ? TFT_WHITE : TFT_LIGHTGREY;
    // Match the Wi-Fi icon's 15-pixel height; the slash stays inside that box.
    display.fillRect(x - 10, y - 3, 4, 7, color);
    display.fillTriangle(x - 6, y - 3, x, y - 7, x, y + 7, color);
    for (int r = 4; r <= 7; r += 3) {
        for (int dy = -r; dy <= r; ++dy) {
            int dx = int(sqrtf(float(r * r - dy * dy)));
            display.drawPixel(x + dx + 1, y + dy, color);
        }
    }
    if (audio.muted()) {
        display.drawLine(x - 10, y - 7, x + 9, y + 7, TFT_RED);
        display.drawLine(x - 9, y - 7, x + 10, y + 7, TFT_RED);
    }
}

// Called on a timer as well as when the controls open. The web interface
// reads batteryPercent through WebControl, and it used to be sampled only
// while the on-screen controls were being drawn -- so a device nobody had
// touched since boot reported the battery as unavailable indefinitely.
constexpr uint32_t BATTERY_SAMPLE_MS = 30000;

void sampleBattery() {
    nextBatterySample = millis() + BATTERY_SAMPLE_MS;
    analogSetPinAttenuation(BATTERY_ADC, ADC_11db);
    uint32_t totalMv = 0;
    for (int i = 0; i < 16; ++i)
        totalMv += analogReadMilliVolts(BATTERY_ADC);
    int batteryMv = int(totalMv / 16) * 2;
    if (batteryMv < 2500) {
        batteryPercent = -1;
        return;
    }
    static const int mv[] = {3300, 3400, 3500, 3600, 3700,
                             3800, 3900, 4000, 4100, 4200};
    static const int pct[] = {0, 5, 10, 15, 30, 50, 65, 80, 90, 100};
    if (batteryMv <= mv[0])
        batteryPercent = 0;
    else if (batteryMv >= mv[9])
        batteryPercent = 100;
    else
        for (int i = 1; i < 10; ++i)
            if (batteryMv <= mv[i]) {
                batteryPercent = pct[i - 1] + (batteryMv - mv[i - 1]) *
                                                  (pct[i] - pct[i - 1]) /
                                                  (mv[i] - mv[i - 1]);
                break;
            }
}

void enterDeepSleep() {
    audio.stop();
    web.stop();
    display.writecommand(TFT_DISPOFF);
    display.writecommand(0x10);
    backlight.off();
    pinMode(AUDIO_AMP_ENABLE, OUTPUT);
    digitalWrite(AUDIO_AMP_ENABLE, HIGH);
    pinMode(TOUCH_RST, OUTPUT);
    digitalWrite(TOUCH_RST, LOW);
    pinMode(WAKE_BUTTON, INPUT_PULLUP);
    esp_sleep_enable_ext0_wakeup(WAKE_BUTTON, 0);
    delay(50);
    esp_deep_sleep_start();
}

void drawScreenBackground() {
    const uint16_t color = monster.screenBackground();
    if (backend.eyeCount() == 1) {
        // Everything outside the one eye, taken from where it actually landed:
        // the literals below describe the pair and would leave stale pixels
        // here. Not assumed to reach the top and bottom either, since begin()
        // shrinks the eye when the tables will not fit, which leaves a band
        // above and below it.
        const int x0 = backend.eyeOriginX(), y0 = backend.eyeOriginY();
        const int size = backend.eyeSize();
        if (x0 > 0) display.fillRect(0, 0, x0, SCREEN_H, color);
        if (x0 + size < SCREEN_W)
            display.fillRect(x0 + size, 0, SCREEN_W - (x0 + size), SCREEN_H,
                             color);
        if (y0 > 0) display.fillRect(x0, 0, size, y0, color);
        if (y0 + size < SCREEN_H)
            display.fillRect(x0, y0 + size, size, SCREEN_H - (y0 + size),
                             color);
        return;
    }
    display.fillRect(0, 0, SCREEN_W, 56, color);
    display.fillRect(0, 184, SCREEN_W, SCREEN_H - 184, color);
    display.fillRect(0, 56, 18, 128, color);
    display.fillRect(146, 56, 28, 128, color);
    display.fillRect(302, 56, 18, 128, color);
}

void showControls(uint32_t now) {
    if (!controlsVisible) {
        sampleBattery();
        controlsDirty = true;
    }
    controlsVisible = true;
    controlsUntil = now + 5000;
}

// How tall the header actually is. It carries a second line only when there
// is an address or a setup SSID to put there, and the rows below it belong to
// whatever is behind -- the eye, with one of them.
int controlsHeaderHeight() {
    return (web.provisioning() || web.connected()) ? 52 : 28;
}

void drawControls() {
    const int headerHeight = controlsHeaderHeight();
    // With two eyes the band down to row 56 is the sketch's either way, so it
    // is cleared to the background. With one eye the rows below the header are
    // the eye's, and painting them here would leave a bar of background across
    // the picture.
    if (backend.eyeCount() != 1)
        display.fillRect(0, 0, SCREEN_W, 56, monster.screenBackground());
    display.fillRect(0, 0, SCREEN_W, headerHeight, TFT_DARKGREY);
    display.setTextDatum(TC_DATUM);
    display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    display.drawString(monster.styleName(monster.style()), SCREEN_W / 2, 5, 2);
    drawSoundIcon(20, 12);
    if (web.provisioning()) {
        String setup = String(web.setupSsid()) + " / " + web.setupPassword();
        display.drawString(setup, SCREEN_W / 2, 32, 1);
    } else if (web.connected()) {
        display.drawString(web.ipAddress(), SCREEN_W / 2, 32, 1);
    }
    const bool wifiOn = web.enabled();
    uint16_t wifiColor =
        !wifiOn ? TFT_LIGHTGREY
                : (web.connected() ? TFT_GREEN
                                   : (web.configured() ? TFT_YELLOW : TFT_RED));
    const int wx = SCREEN_W - 47, wy = 17;
    for (int dy = -12; dy <= 0; ++dy) {
        for (int dx = -12; dx <= 12; ++dx) {
            int r2 = dx * dx + dy * dy;
            bool outer = r2 >= 81 && r2 <= 121 && dy < -abs(dx) / 4;
            bool inner = r2 >= 25 && r2 <= 49 && dy < -abs(dx) / 4;
            if (outer || inner) display.drawPixel(wx + dx, wy + dy, wifiColor);
        }
    }
    display.fillCircle(wx, wy, 2, wifiColor);
    if (!wifiOn || !web.configured()) {
        display.drawLine(wx - 11, wy - 12, wx + 10, wy + 1, TFT_RED);
        display.drawLine(wx - 10, wy - 12, wx + 11, wy + 1, TFT_RED);
    }
    char battery[8];
    if (batteryPercent < 0)
        snprintf(battery, sizeof(battery), "--%%");
    else
        snprintf(battery, sizeof(battery), "%d%%", batteryPercent);
    uint16_t batteryColor =
        batteryPercent < 0
            ? TFT_LIGHTGREY
            : (batteryPercent <= 20
                   ? TFT_RED
                   : (batteryPercent <= 50 ? TFT_YELLOW : TFT_GREEN));
    display.setTextDatum(TR_DATUM);
    display.setTextColor(batteryColor, TFT_DARKGREY);
    // Font 2 matches the icon height; draw twice for slightly more visual
    // weight.
    display.drawString(battery, SCREEN_W - 4, 5, 2);
    display.drawString(battery, SCREEN_W - 5, 5, 2);
    display.fillRect(0, SCREEN_H - 40, SCREEN_W, 40, TFT_DARKGREY);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, TFT_DARKGREY);
    display.drawString("< PREV", SCREEN_W / 6, SCREEN_H - 20, 2);
    display.drawString("FLIP", SCREEN_W / 2, SCREEN_H - 20, 2);
    display.drawString("NEXT >", SCREEN_W * 5 / 6, SCREEN_H - 20, 2);
}

void hideControls() {
    if (!controlsVisible) return;
    controlsVisible = false;
    // The rows go back to the renderer, which repaints them on the next frame.
    backend.setReservedRows(0, 0);
    controlsDirty = false;
    display.fillRect(0, 0, SCREEN_W, 56, monster.screenBackground());
    display.fillRect(0, SCREEN_H - 40, SCREEN_W, 40,
                     monster.screenBackground());
}

void setup() {
    Serial.begin(115200);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    display.init();
    applyFlip(loadFlip());
    // Monster Eyes produces native-endian RGB565 words. TFT_eSPI's pushImage()
    // needs byte swapping enabled before sending those words over SPI.
    display.setSwapBytes(true);
    display.fillScreen(TFT_BLACK);
    // After init(), which claims TFT_BL as a plain output itself.
    backlight.begin();

    if (!monster.begin()) {
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.drawString("Monster Eyes failed", 4, 4, 2);
        while (true)
            delay(1000);
    }
    if (!touch.begin())
        Serial.println("touch unavailable; using autonomous gaze");
    audio.begin();
    audio.setPackage(monster.configPath());
    lastMuted = audio.muted();
    web.begin();
    lastWifiEnabled = web.enabled();
    Serial.println("Monster Eyes composite TFT backend ready");
}

void loop() {
    TouchPoint point{};
    bool touched = touch.read(point) > 0;
    uint32_t now = millis();
    if (touched) {
        if (!wasTouched) {
            touchStartedAt = now;
            touchStartX = point.x;
            touchStartY = point.y;
            sleepCandidate = true;
        }
        // This backend's map-space axes are opposite the panel's touch axes.
        float x = constrain((SCREEN_W * 0.5f - point.x) / (SCREEN_W * 0.5f),
                            -1.0f, 1.0f);
        // Use screen-down-positive touch input for the map-space Y coordinate.
        float y = constrain((point.y - SCREEN_H * 0.5f) / (SCREEN_H * 0.5f),
                            -1.0f, 1.0f);
        monster.setGaze(x, y);
        float dx = point.x - touchStartX;
        float dy = point.y - touchStartY;
        if (dx * dx + dy * dy > 225) sleepCandidate = false;
        if (sleepCandidate && now - touchStartedAt >= 2000) enterDeepSleep();
    } else if (wasTouched) {
        monster.releaseGaze();
        if (now - touchStartedAt < 300) {
            if (!controlsVisible) {
                showControls(now);
            } else if (touchStartY < 55 && touchStartX < 45 &&
                       audio.present()) {
                audio.setMuted(!audio.muted());
                showControls(now);
                controlsDirty = true;
            } else if (touchStartY < 55 && touchStartX >= SCREEN_W - 72 &&
                       touchStartX < SCREEN_W - 22) {
                web.setEnabled(!web.enabled());
                showControls(now);
                controlsDirty = true;
            } else if (touchStartY >= SCREEN_H - 50) {
                hideControls();
                if (touchStartX < SCREEN_W / 3) {
                    monster.previousStyle();
                    web.manualStyleSelected();
                } else if (touchStartX > SCREEN_W * 2 / 3) {
                    monster.nextStyle();
                    web.manualStyleSelected();
                } else {
                    applyFlip(!flipped);
                    storeFlip(flipped);
                    display.fillScreen(monster.screenBackground());
                }
                backgroundPending = true;
                showControls(now);
                controlsDirty = true;
            } else {
                monster.blink();
                showControls(now);
            }
        }
    }
    if (!touched) sleepCandidate = false;
    wasTouched = touched;

    if (int32_t(now - nextBatterySample) >= 0) sampleBattery();

    uint8_t styleBeforeWeb = monster.style();
    web.update(now);
    audio.update(now);
    if (audio.muted() != lastMuted) {
        lastMuted = audio.muted();
        controlsDirty = controlsVisible;
    }
    if (web.connected() != lastWifiConnected ||
        web.provisioning() != lastProvisioning ||
        web.enabled() != lastWifiEnabled) {
        lastWifiConnected = web.connected();
        lastProvisioning = web.provisioning();
        lastWifiEnabled = web.enabled();
        controlsDirty = controlsVisible;
    }
    if (monster.style() != styleBeforeWeb) {
        backgroundPending = true;
        controlsDirty = controlsVisible;
    }
    if (controlsVisible && int32_t(now - controlsUntil) >= 0) hideControls();
    // Hand the sketch exactly the rows its controls occupy, before the frame
    // is drawn. Refreshed every frame rather than at showControls(), because
    // the header changes height when Wi-Fi connects and the eye should get
    // those rows back in the same frame.
    if (controlsVisible) backend.setReservedRows(controlsHeaderHeight(), 40);
    if (monster.style() != lastRenderedStyle) {
        if (lastRenderedStyle != 0xFF) audio.setPackage(monster.configPath());
        lastRenderedStyle = monster.style();
        backgroundPending = true;
    }
    monster.animate();
    if (backgroundPending) {
        drawScreenBackground();
        backgroundPending = false;
    }
    if (controlsVisible && controlsDirty) {
        drawControls();
        controlsDirty = false;
    }
}
