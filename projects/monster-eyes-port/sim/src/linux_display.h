/**
 * @file linux_display.h
 * @brief Monster Eyes backend that draws into host memory instead of a panel.
 *
 * Geometry is copied deliberately from src/composite_tft_display.h rather than
 * invented: the same 320x240 panel, the same 128x128 logical size per eye, the
 * same origins. Anything the simulator shows at a given pixel is what the
 * device would put at that pixel.
 *
 * The device backend keeps a mirror only while a capture is armed, because on
 * an ESP32 a second 150 KiB buffer is a real cost and the panel cannot be read
 * back. Here the framebuffer IS the display, so it is always live and there is
 * nothing to arm.
 */

#ifndef _LINUX_DISPLAY_H_
#define _LINUX_DISPLAY_H_

#include <Adafruit_Monster_Eyes.h>

#include <stdlib.h>
#include <string.h>

/**
 * @brief Two square eyes on one simulated 320x240 panel.
 */
class LinuxDisplay : public Eyes_StripeDisplay {
public:
  static constexpr int PANEL_W = 320; ///< Panel width, as on the device
  static constexpr int PANEL_H = 240; ///< Panel height, as on the device
  static constexpr int EYE_Y = 56;    ///< Top of both eyes in panel pixels

  /** @brief Left edge of an eye. @param eye Index. @return Panel X. */
  static constexpr int eyeX(int eye) { return eye ? 174 : 18; }

  // The gap those default positions leave between the two 128 px squares.
  static constexpr int DEFAULT_GAP = 174 - (18 + 128);

  LinuxDisplay() : Eyes_StripeDisplay(2, 16) {}

  // Mirrors CompositeTftDisplay: a package may ask for one eye filling the
  // panel instead of two side by side.
  bool setEyeCount(uint8_t eyes) override {
    if (eyes != 1 && eyes != 2)
      return false;
    if (eyes != _numEyes) {
      _numEyes = eyes;
      _panelW = 0; // Ask panelSize() again; the layout differs
    }
    return true;
  }

  // How far apart the pair sits. Each eye moves half the difference from the
  // default, so they close on the middle of the panel rather than drifting off
  // one side of it. Mirrors CompositeTftDisplay.
  bool setEyeGap(int gap) override {
    if (_numEyes != 2)
      return false;
    if (gap == EYE_GAP_DEFAULT) { // Back to the panel's own layout
      _nudge = 0;
      return true;
    }
    // Negative gaps overlap the two squares, which is how a face whose eyes
    // nearly touch is laid out: a drawn eye leaves cream margin inside its
    // square, and where the squares overlap both write that same background.
    // Half a square is as far as that can sensibly go.
    if (gap < -(eyeSize() ? eyeSize() : 128) / 2)
      gap = -(eyeSize() ? eyeSize() : 128) / 2;
    const int limit = 18 * 2 + DEFAULT_GAP; // Eyes meet the panel edges
    if (gap > limit)
      gap = limit;
    _nudge = (DEFAULT_GAP - gap) / 2;
    return true;
  }

  ~LinuxDisplay() override { free(_fb); }

  bool begin(void) override {
    if (_fb)
      return true;
    _fb = (uint16_t *)calloc((size_t)PANEL_W * PANEL_H, sizeof(uint16_t));
    return _fb != nullptr;
  }

  void clear(uint16_t color) override {
    _background = color;
    if (!_fb)
      return;
    for (size_t i = 0; i < (size_t)PANEL_W * PANEL_H; ++i)
      _fb[i] = color;
  }

  /** @brief The panel's pixels, native-endian RGB565.
   *  @return Framebuffer, or NULL before begin(). */
  const uint16_t *framebuffer(void) const { return _fb; }

  /**
   * @brief Rows at the top and bottom the renderer must not paint.
   *
   * Mirrors CompositeTftDisplay, where the sketch reserves the header and
   * footer while its controls are showing. One eye is as tall as the panel, so
   * without this the eye repaints those rows every frame and anything drawn
   * over it flickers.
   *
   * @param top    Rows reserved at the top.
   * @param bottom Rows reserved at the bottom.
   */
  void setReservedRows(int top, int bottom) {
    _reserveTop = top;
    _reserveBottom = bottom;
  }

protected:
  // The logical size one eye may occupy; where it physically lands is decided
  // per eye in flushStripe(), exactly as the device backend does it.
  void panelSize(int *width, int *height) override {
    if (_numEyes == 1) {
      *width = PANEL_W;
      *height = PANEL_H;
      return;
    }
    *width = 128;
    *height = 128;
  }

  void flushStripe(int eye, int x0, int width, uint16_t *pixels) override {
    if (!_fb)
      return;
    const int x = (_numEyes == 1)
                      ? originX() + x0
                      : eyeX(eye & 1) + ((eye & 1) ? -_nudge : _nudge) + x0;
    const int y0 = (_numEyes == 1) ? originY() : EYE_Y;
    // Row-major, `width` pixels per row, top row first -- the layout
    // TFT_eSPI::pushImage() expects, so the copy here is the same copy the
    // device makes into its capture mirror.
    const int top = _reserveTop;
    const int bottom = PANEL_H - _reserveBottom;
    for (int row = 0; row < eyeSize(); ++row) {
      const int y = y0 + row;
      if (y < top || y >= bottom)
        continue;
      memcpy(&_fb[(size_t)y * PANEL_W + x], &pixels[(size_t)row * width],
             (size_t)width * sizeof(uint16_t));
    }
  }

private:
  uint16_t *_fb = nullptr;   ///< PANEL_W * PANEL_H pixels
  int _nudge = 0;            ///< Pixels each eye moves in from its default
  int _reserveTop = 0;       ///< Rows at the top the host owns
  int _reserveBottom = 0;    ///< Rows at the bottom the host owns
  uint16_t _background = 0;  ///< Last clear() colour
};

#endif // _LINUX_DISPLAY_H_
