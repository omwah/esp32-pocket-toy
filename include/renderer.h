#pragma once
#include <TFT_eSPI.h>
#include "battle.h"
#include "camera.h"
#include "config.h"

// Draws the scene into an off-screen sprite, then pushes one finished frame to
// the panel. Drawing direct to the display would tear and flicker badly at these
// object counts.
class Renderer {
public:
  bool begin(TFT_eSPI &tft);
  void draw(const Battle &b, const Camera &cam);

private:
  TFT_eSPI       *_tft = nullptr;
  TFT_eSprite    *_fb  = nullptr;

  // Parallax starfield. Coordinates are pixels within a screen-sized tile, not
  // world units: the field translates with the camera but never scales with it.
  struct Star { float x, y; uint8_t layer; uint16_t colour; };
  Star _stars[NUM_STARS];

  Vec2  _planetWorld;
  float _planetRadius = 0;

  void makeStars();
  void drawStars(const Camera &cam);
  void drawPlanet(const Camera &cam);
  void drawShip(const Ship &s, const Camera &cam);
};
