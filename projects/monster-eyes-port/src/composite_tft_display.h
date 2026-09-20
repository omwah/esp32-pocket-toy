#pragma once

#include <Adafruit_Monster_Eyes.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

// Monster Eyes backend for two square eyes on one 320x240 TFT_eSPI panel.
//
// The backend can also keep a mirror of the panel in memory, so the device can
// hand a screenshot to the web interface. The panel itself cannot be read back
// -- MISO is not dependable on this board and the controller returns mangled
// 18-bit data -- so instead every stripe on its way to the screen is copied
// into a buffer as well. The mirror is only written while a capture is armed,
// which is for exactly one rendered frame, so the cost is nothing at all the
// rest of the time.
class CompositeTftDisplay : public Eyes_StripeDisplay {
public:
    static constexpr int PANEL_W = 320, PANEL_H = 240;
    // Where each eye's square sits on the panel. A function rather than an
    // array, which would need a definition outside the class before C++17.
    static constexpr int EYE_Y = 56;
    static constexpr int eyeX(int eye) { return eye ? 174 : 18; }

    explicit CompositeTftDisplay(TFT_eSPI &tft)
        : Eyes_StripeDisplay(2, 16), _tft(tft) {}

    ~CompositeTftDisplay() {
        if (_mirror) free(_mirror);
    }

    bool begin() override { return true; }

    void clear(uint16_t color) override {
        _tft.fillScreen(color);
        // Keep the mirror in step, so a screenshot shows the background the
        // eyes are actually sitting on rather than whatever was there before.
        if (_mirror)
            for (size_t i = 0; i < size_t(PANEL_W) * PANEL_H; ++i)
                _mirror[i] = color;
        _background = color;
    }

    // Arm the mirror for the next frame drawn. Allocates on first use, by
    // preference in PSRAM: 150 KiB is more than the internal heap can spare
    // once the textures are loaded.
    bool armCapture() {
        if (!_mirror) {
            const size_t bytes = size_t(PANEL_W) * PANEL_H * sizeof(uint16_t);
            _mirror = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
            if (!_mirror) _mirror = (uint16_t *)malloc(bytes);
            if (!_mirror) return false;
            for (size_t i = 0; i < size_t(PANEL_W) * PANEL_H; ++i)
                _mirror[i] = _background;
        }
        _capturing = true;
        return true;
    }

    void disarmCapture() { _capturing = false; }

    // The mirror, in the panel's own pixel order. Null until a capture has
    // been armed at least once.
    const uint16_t *mirror() const { return _mirror; }

protected:
    void panelSize(int *width, int *height) override {
        // Report the logical size available to one eye. Physical placement is
        // handled per-eye in flushStripe().
        *width = 128;
        *height = 128;
    }

    void flushStripe(int eye, int x0, int width, uint16_t *pixels) override {
        const int x = eyeX(eye & 1) + x0;
        const int y = EYE_Y;
        _tft.pushImage(x, y, width, eyeSize(), pixels);
        if (!_capturing || !_mirror) return;
        // The stripe is row-major, `width` pixels per row, exactly as
        // pushImage() reads it.
        for (int row = 0; row < eyeSize(); ++row)
            memcpy(&_mirror[size_t(y + row) * PANEL_W + x],
                   &pixels[size_t(row) * width], size_t(width) * 2);
    }

private:
    TFT_eSPI &_tft;
    uint16_t *_mirror = nullptr;
    uint16_t _background = 0;
    bool _capturing = false;
};
