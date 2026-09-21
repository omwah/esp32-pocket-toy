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

    // The gap those default positions leave between the two 128 px squares.
    static constexpr int DEFAULT_GAP = 174 - (18 + 128);

    explicit CompositeTftDisplay(TFT_eSPI &tft)
        : Eyes_StripeDisplay(2, 16), _tft(tft) {}

    // A package may ask for one eye filling the panel instead of two side by
    // side (extensions.display.singleEye). Only the layout changes: one eye is
    // as large as the panel's short side and centred, rather than two 128px
    // squares at fixed offsets.
    bool setEyeCount(uint8_t eyes) override {
        if (eyes != 1 && eyes != 2) return false;
        if (eyes != _numEyes) {
            _numEyes = eyes;
            // Force panelSize() to be asked again: the logical panel differs
            // between the two layouts.
            _panelW = 0;
        }
        return true;
    }

    // How far apart the pair sits. Each eye moves half the difference from the
    // default, so they close on the middle of the panel rather than drifting
    // off one side of it.
    bool setEyeGap(int gap) override {
        if (_numEyes != 2) return false;
        if (gap == EYE_GAP_DEFAULT) {  // Back to the panel's own layout
            _nudge = 0;
            return true;
        }
        // Negative gaps overlap the two squares, which is how a face whose
        // eyes nearly touch is laid out: a drawn eye leaves cream margin
        // inside its square, and where they overlap both write that same
        // background. Half a square is as far as that can sensibly go.
        const int floorGap = -(eyeSize() ? eyeSize() : 128) / 2;
        if (gap < floorGap) gap = floorGap;
        const int limit = 18 * 2 + DEFAULT_GAP;  // Eyes meet the edges
        if (gap > limit) gap = limit;
        _nudge = (DEFAULT_GAP - gap) / 2;
        return true;
    }

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

    // Where a single eye sits, so the sketch can clear what is left around it
    // rather than assuming the two-eye layout. Meaningless with two eyes,
    // which use the fixed pair of offsets above.
    int eyeOriginX() const { return originX(); }
    int eyeOriginY() const { return originY(); }

    // Rows at the top and bottom the renderer must not paint, so the sketch's
    // header and footer can own them outright.
    //
    // Redrawing the controls after every frame instead would flicker badly:
    // both would be writing the same pixels thirty times a second and the eye
    // would show through between them. Reserving the band means each pixel is
    // written once, by whoever owns it.
    //
    // With two eyes this changes nothing -- they occupy rows 56 to 183 and
    // never reach the bands -- so it costs the pair neither pixels nor time.
    void setReservedRows(int top, int bottom) {
        _reserveTop = top;
        _reserveBottom = bottom;
    }

protected:
    void panelSize(int *width, int *height) override {
        if (_numEyes == 1) {
            // The whole panel, so the base class sizes the eye to the short
            // side and centres it for us.
            *width = PANEL_W;
            *height = PANEL_H;
            return;
        }
        // Report the logical size available to one eye. Physical placement is
        // handled per-eye in flushStripe().
        *width = 128;
        *height = 128;
    }

    void flushStripe(int eye, int x0, int width, uint16_t *pixels) override {
        // With one eye the base class has already centred it, so use the
        // origin it worked out rather than the two-eye offsets.
        const int x = (_numEyes == 1)
                          ? originX() + x0
                          : eyeX(eye & 1) + ((eye & 1) ? -_nudge : _nudge) + x0;
        const int y = (_numEyes == 1) ? originY() : EYE_Y;

        // Clip to the rows the renderer is allowed. The stripe is row-major
        // with `width` pixels a row, so skipping the first rows is an offset
        // into the same buffer rather than a copy.
        const int top = _reserveTop;
        const int bottom = PANEL_H - _reserveBottom;
        const int visibleY0 = (y > top) ? y : top;
        const int visibleY1 = (y + eyeSize() < bottom) ? y + eyeSize() : bottom;
        if (visibleY1 > visibleY0)
            _tft.pushImage(x, visibleY0, width, visibleY1 - visibleY0,
                           pixels + size_t(visibleY0 - y) * width);
        if (!_capturing || !_mirror) return;
        // The stripe is row-major, `width` pixels per row, exactly as
        // pushImage() reads it.
        for (int row = 0; row < eyeSize(); ++row)
            memcpy(&_mirror[size_t(y + row) * PANEL_W + x],
                   &pixels[size_t(row) * width], size_t(width) * 2);
    }

private:
    TFT_eSPI &_tft;
    int _reserveTop = 0;     ///< Rows at the top the sketch owns
    int _reserveBottom = 0;  ///< Rows at the bottom the sketch owns
    int _nudge = 0; ///< Pixels each eye moves inwards from its default spot
    uint16_t *_mirror = nullptr;
    uint16_t _background = 0;
    bool _capturing = false;
};
