#pragma once

#include <Adafruit_Monster_Eyes.h>
#include <TFT_eSPI.h>

// Monster Eyes backend for two square eyes on one 320x240 TFT_eSPI panel.
class CompositeTftDisplay : public Eyes_StripeDisplay {
public:
  explicit CompositeTftDisplay(TFT_eSPI &tft)
      : Eyes_StripeDisplay(2, 16), _tft(tft) {}

  bool begin() override { return true; }
  void clear(uint16_t color) override { _tft.fillScreen(color); }

protected:
  void panelSize(int *width, int *height) override {
    // Report the logical size available to one eye. Physical placement is
    // handled per-eye in flushStripe().
    *width = 128;
    *height = 128;
  }

  void flushStripe(int eye, int x0, int width, uint16_t *pixels) override {
    const int x = (eye == 0 ? 18 : 174) + x0;
    const int y = 56;
    _tft.pushImage(x, y, width, eyeSize(), pixels);
  }

private:
  TFT_eSPI &_tft;
};
