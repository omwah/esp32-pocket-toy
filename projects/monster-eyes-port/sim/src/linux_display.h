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

  LinuxDisplay() : Eyes_StripeDisplay(2, 16) {}

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

protected:
  // The logical size one eye may occupy; where it physically lands is decided
  // per eye in flushStripe(), exactly as the device backend does it.
  void panelSize(int *width, int *height) override {
    *width = 128;
    *height = 128;
  }

  void flushStripe(int eye, int x0, int width, uint16_t *pixels) override {
    if (!_fb)
      return;
    const int x = eyeX(eye & 1) + x0;
    // Row-major, `width` pixels per row, top row first -- the layout
    // TFT_eSPI::pushImage() expects, so the copy here is the same copy the
    // device makes into its capture mirror.
    for (int row = 0; row < eyeSize(); ++row)
      memcpy(&_fb[(size_t)(EYE_Y + row) * PANEL_W + x],
             &pixels[(size_t)row * width], (size_t)width * sizeof(uint16_t));
  }

private:
  uint16_t *_fb = nullptr;   ///< PANEL_W * PANEL_H pixels
  uint16_t _background = 0;  ///< Last clear() colour
};

#endif // _LINUX_DISPLAY_H_
