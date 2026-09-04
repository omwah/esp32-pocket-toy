#include "renderer.h"
#include <Arduino.h>

namespace {

// Fleet colours: fleet 0 warm, fleet 1 cool, so the sides read apart instantly
// even at the smallest zoom where ships are single pixels.
const uint16_t FLEET_HULL[2]  = {0xFD20, 0x5D7F};  // amber / ice blue
const uint16_t FLEET_SHOT[2]  = {0xFFE0, 0x7FFF};  // yellow / white-blue
const uint16_t FLEET_TRAIL[2] = {0xA980, 0x2C5F};

inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Linear blend between two 565 colours. Used for the planet limb, where the
// edge pixel is partly planet and partly space.
inline uint16_t mix(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) |
                    ((int)(ag + (bg - ag) * t) << 5)  |
                     (int)(ab + (bb - ab) * t));
}

// Blend toward black; used for the planet terminator and debris fade.
inline uint16_t dim(uint16_t c, float f) {
  uint8_t r = ((c >> 11) & 0x1F) * f;
  uint8_t g = ((c >> 5)  & 0x3F) * f;
  uint8_t b = ( c        & 0x1F) * f;
  return (r << 11) | (g << 5) | b;
}

}  // namespace

bool Renderer::begin(TFT_eSPI &tft) {
  _tft = &tft;

  // 320x240x16bpp = 150 KB. This only fits because the board has PSRAM.
  _fb = new TFT_eSprite(&tft);
  _fb->setColorDepth(16);
  if (!_fb->createSprite(SCREEN_W, SCREEN_H)) {
    Serial.println("framebuffer allocation failed");
    return false;
  }

  makeStars();

  // Park the planet off to one side so the fleets have open space to fight in.
  _planetWorld  = {WORLD_W * 0.26f, WORLD_H * 0.34f};
  _planetRadius = 300.0f;
  return true;
}

void Renderer::makeStars() {
  for (int i = 0; i < NUM_STARS; i++) {
    Star &s = _stars[i];

    // Stars live directly in a screen-sized tile, not in world space. They are
    // a backdrop at effectively infinite distance, so their on-screen spacing
    // must not depend on the camera zoom at all.
    s.x = random(0, SCREEN_W);
    s.y = random(0, SCREEN_H);
    s.layer = random(0, 3);

    // Backdrop must stay well below weapons fire in brightness, or a
    // single-pixel tracer at wide zoom is indistinguishable from a star.
    uint8_t v = 26 + s.layer * 26 + random(0, 16);

    // Tint by scaling the channels of this star's own brightness. Writing the
    // dominant channel as an absolute value (255 - v * 0.2) instead produced
    // near-saturated red and blue pixels sitting at full brightness while every
    // other star was dim -- they read as floating coloured dots, and dimming v
    // could not fix them.
    int roll = random(0, 100);
    if (roll < 11)       s.colour = rgb(v * 0.72f, v * 0.80f, v);   // blue-white
    else if (roll < 20)  s.colour = rgb(v, v * 0.78f, v * 0.62f);   // warm
    else                 s.colour = rgb(v, v, v);
  }
}

void Renderer::drawStars(const Camera &cam) {
  // Translate the field by the camera position only. Projecting stars through
  // the camera the way world objects are projected makes their spacing scale
  // with the zoom, and since the automatic camera is always easing its zoom by
  // a little, the whole field visibly swims and re-spaces itself even when it
  // looks like nothing is moving. A backdrop must translate, never scale.
  //
  // Parallax factors are small: far layers barely shift, near ones drift a
  // little, which is what sells depth without the field reading as particles.
  const float PAR[3] = {0.006f, 0.016f, 0.032f};

  Vec2 c = cam.centre();

  for (const Star &s : _stars) {
    float p = PAR[s.layer];

    // Wrap into the tile. The seam is at the screen edge, so a star leaving one
    // side reappears on the other -- which is what an endless field looks like.
    float x = fmodf(s.x - c.x * p, (float)SCREEN_W);
    if (x < 0) x += SCREEN_W;
    float y = fmodf(s.y - c.y * p, (float)SCREEN_H);
    if (y < 0) y += SCREEN_H;

    _fb->drawPixel((int)x, (int)y,
                   s.layer == 2 ? s.colour : dim(s.colour, 0.7f));
  }
}

// Surface colour at a normalised offset from the planet centre. Shared by the
// solid interior and the antialiased edge so both stay consistent.
static uint16_t planetShade(float dx, float dy) {
  const float LX = -0.60f, LY = -0.42f, LZ = 0.68f;

  float lat = fabsf(dy);
  bool  polar = lat > 0.80f;

  float nz = sqrtf(max(0.0f, 1.0f - dx * dx - dy * dy));
  float lam = dx * LX + dy * LY + nz * LZ;
  if (lam < 0) lam = 0;

  // Sample the terrain on a fixed grid in planet space. The surface pattern is
  // high frequency, so evaluating it at whatever fractional offset a pixel
  // happens to land on makes it crawl as the disc drifts across the screen.
  // Quantising here decouples the pattern from the screen grid, which is what
  // lets the geometry below stay fully fractional and scale smoothly.
  const float Q = 1.0f / 96.0f;
  float qx = roundf(dx / Q) * Q, qy = roundf(dy / Q) * Q;

  float n = sinf(qx * 5.3f + qy * 2.1f) * cosf(qy * 4.7f - qx * 1.9f)
          + 0.5f * sinf(qx * 11.7f - qy * 9.3f) * cosf(qy * 10.1f + qx * 7.7f);

  float band = 0.5f + 0.5f * sinf(dy * 7.5f);
  float rr, gg, bb;

  if (polar) {
    float t = (lat - 0.80f) / 0.20f;
    rr = 168 + 70 * t; gg = 186 + 62 * t; bb = 198 + 55 * t;
  } else if (n > 0.28f) {
    float h = (n - 0.28f) / 1.2f;
    rr =  58 + 108 * h; gg =  96 + 62 * h; bb = 44 + 30 * h;
  } else if (n > 0.16f) {
    rr = 30; gg = 92; bb = 118;
  } else {
    float d = (0.16f - n) * 0.5f;
    rr = 16 + 10 * d; gg = 46 + 26 * d; bb = 92 + 34 * d;
  }

  // Latitude banding tints the land and sea slightly.
  rr *= 0.85f + 0.30f * band;
  gg *= 0.85f + 0.30f * band;
  bb *= 0.85f + 0.30f * band;

  if (!polar && n <= 0.16f) {
    float spec = lam * lam * lam * lam;
    rr += 120 * spec; gg += 130 * spec; bb += 140 * spec;
  }

  float lit = 0.10f + 0.92f * lam;
  rr *= lit; gg *= lit; bb *= lit;

  // Atmospheric scattering, thickening toward the limb.
  float limb = dx * dx + dy * dy;
  if (limb > 0.55f) {
    float h = (limb - 0.55f) / 0.45f;
    h = h * h;
    // Haze on the day side, a faint cold glow carrying past the terminator.
    rr += 60 * h * lam + 10 * h;
    gg += 105 * h * lam + 18 * h;
    bb += 165 * h * lam + 34 * h;
  }

  return rgb((uint8_t)min(255.0f, rr), (uint8_t)min(255.0f, gg),
             (uint8_t)min(255.0f, bb));
}

void Renderer::drawPlanet(const Camera &cam) {
  Vec2  c = cam.toScreen(_planetWorld);
  float r = _planetRadius * cam.zoom();

  // Cheap reject: entirely off screen.
  if (c.x + r < 0 || c.x - r > SCREEN_W || c.y + r < 0 || c.y - r > SCREEN_H) return;
  if (r < 1.5f) { _fb->drawPixel((int)lroundf(c.x), (int)lroundf(c.y),
                                 rgb(60, 90, 130)); return; }

  // Centre and radius are both kept fractional. Rounding either one makes the
  // disc step by a whole pixel as the zoom eases, which reads as the
  // circumference pulsing -- most obvious zoomed out, where a pixel is a large
  // share of the radius. Terrain stability is handled in planetShade instead.
  const float cx = c.x, cy = c.y;

  int y0 = max(0, (int)floorf(cy - r)), y1 = min(SCREEN_H - 1, (int)ceilf(cy + r));

  for (int y = y0; y <= y1; y++) {
    float dy = ((float)y - cy) / r;
    if (fabsf(dy) > 1.0f) continue;

    float halfF = sqrtf(max(0.0f, 1.0f - dy * dy)) * r;
    float leftF = cx - halfF, rightF = cx + halfF;

    int xa = (int)ceilf(leftF), xb = (int)floorf(rightF);

    // Antialias the two boundary pixels by how much of each the disc covers.
    // This is what lets the planet grow and shrink smoothly: without it the
    // edge can only move in whole pixels, so the circumference visibly steps
    // as the zoom eases -- worst when zoomed out, where one pixel is a large
    // fraction of the radius.
    int el = xa - 1, er = xb + 1;
    if (el >= 0 && el < SCREEN_W) {
      float cov = (float)xa - leftF;
      if (cov > 0.01f)
        _fb->drawPixel(el, y, mix(TFT_BLACK,
                                  planetShade(((float)el - cx) / r, dy),
                                  min(1.0f, cov)));
    }
    if (er >= 0 && er < SCREEN_W) {
      float cov = rightF - (float)xb;
      if (cov > 0.01f)
        _fb->drawPixel(er, y, mix(TFT_BLACK,
                                  planetShade(((float)er - cx) / r, dy),
                                  min(1.0f, cov)));
    }

    xa = max(0, xa);
    xb = min(SCREEN_W - 1, xb);
    if (xa > xb) continue;

    // Shade in short runs anchored to the planet centre, not the span start,
    // so the run phase does not flip as the disc moves.
    const int RUN = 2;
    int icx = (int)lroundf(cx);
    int xs = icx + ((xa - icx) / RUN) * RUN;
    if (xs > xa) xs -= RUN;

    for (int x = xs; x <= xb; x += RUN) {
      uint16_t col = planetShade(((float)x - cx) / r, dy);
      int px = max(x, xa);
      int w  = min(x + RUN, xb + 1) - px;
      if (w > 0) _fb->drawFastHLine(px, y, w, col);
    }
  }
}

void Renderer::drawShip(const Ship &s, const Camera &cam) {
  Vec2 p = cam.toScreen(s.pos);
  if (!cam.visible(p)) return;

  float z = cam.zoom();
  uint16_t hull  = FLEET_HULL[s.fleet];
  uint16_t light = mix(hull, rgb(255, 255, 255), 0.45f);   // lit upper surfaces
  uint16_t shade = dim(hull, 0.42f);                       // shadowed side
  uint16_t glow  = FLEET_TRAIL[s.fleet];

  float len = (s.cls == CAPITAL ? 34.0f : s.cls == CRUISER ? 17.0f : 8.0f) * z;

  // Below a few pixels there is no silhouette to read. Draw a bright marker
  // instead, sized by class, so fleet strength still reads when zoomed out.
  if (len < 3.0f) {
    _fb->drawPixel((int)p.x, (int)p.y, light);
    if (s.cls != FIGHTER) {
      _fb->drawPixel((int)p.x + 1, (int)p.y, hull);
      _fb->drawPixel((int)p.x, (int)p.y + 1, hull);
    }
    if (s.cls == CAPITAL) _fb->drawPixel((int)p.x + 1, (int)p.y + 1, hull);
    return;
  }

  float ca = cosf(s.angle), sa = sinf(s.angle);
  // Ship-local coordinates: +x forward, +y to starboard.
  auto B = [&](float fx, float fy) -> Vec2 {
    return {p.x + fx * ca - fy * sa, p.y + fx * sa + fy * ca};
  };
  auto tri = [&](Vec2 a, Vec2 b, Vec2 c, uint16_t col) {
    _fb->fillTriangle(a.x, a.y, b.x, b.y, c.x, c.y, col);
  };

  float L = len * 0.5f;          // half length
  float W = len * 0.30f;         // half beam

  switch (s.cls) {
    case FIGHTER: {
      // Swept dart: a narrow fuselage with wings raked back from the nose.
      Vec2 nose = B(L, 0);
      Vec2 wl   = B(-L * 0.55f,  W * 1.35f);
      Vec2 wr   = B(-L * 0.55f, -W * 1.35f);
      Vec2 tl   = B(-L * 0.15f,  W * 0.30f);
      Vec2 tr   = B(-L * 0.15f, -W * 0.30f);

      tri(nose, wl, tl, shade);      // starboard wing, away from the light
      tri(nose, wr, tr, light);      // port wing, lit
      tri(nose, tl, tr, hull);       // fuselage
      break;
    }

    case CRUISER: {
      // Angular hull with a forward prow and a blocky engine section, so it
      // reads as a warship rather than a scaled-up fighter.
      Vec2 nose = B(L, 0);
      Vec2 sl   = B(L * 0.25f,  W);
      Vec2 sr   = B(L * 0.25f, -W);
      Vec2 al   = B(-L * 0.75f,  W * 0.78f);
      Vec2 ar   = B(-L * 0.75f, -W * 0.78f);

      tri(nose, sl, sr, hull);       // prow
      tri(sl, sr, ar, hull);         // midships
      tri(sl, ar, al, shade);
      // Lit strip along the port flank picks out the hull edge.
      _fb->drawLine(nose.x, nose.y, sr.x, sr.y, light);
      _fb->drawLine(sr.x, sr.y, ar.x, ar.y, light);

      if (len > 11) {
        // Dorsal spine and a pair of gun sponsons.
        Vec2 t1 = B(-L * 0.70f, 0), t2 = B(L * 0.30f, 0);
        _fb->drawLine(t1.x, t1.y, t2.x, t2.y, light);
        Vec2 g1 = B(L * 0.05f,  W * 1.15f), g2 = B(L * 0.05f, -W * 1.15f);
        _fb->drawPixel((int)g1.x, (int)g1.y, light);
        _fb->drawPixel((int)g2.x, (int)g2.y, light);
      }
      break;
    }

    case CAPITAL:
    default: {
      // Long slab hull, flared stern, hangar bays down the flanks. Silhouette
      // is deliberately rectangular so it never reads as a big fighter.
      Vec2 nose = B(L, 0);
      Vec2 bl   = B(L * 0.55f,  W * 0.62f);
      Vec2 br   = B(L * 0.55f, -W * 0.62f);
      Vec2 ml   = B(-L * 0.35f,  W);
      Vec2 mr   = B(-L * 0.35f, -W);
      Vec2 sl   = B(-L, W * 0.80f);
      Vec2 sr   = B(-L, -W * 0.80f);

      tri(nose, bl, br, light);      // prow, catching the light
      tri(bl, br, mr, hull);
      tri(bl, mr, ml, hull);
      tri(ml, mr, sr, shade);        // stern section in shadow
      tri(ml, sr, sl, shade);

      if (len > 14) {
        // Hull plating: a bright dorsal line and dark flank seams.
        Vec2 d1 = B(L * 0.85f, 0), d2 = B(-L * 0.90f, 0);
        _fb->drawLine(d1.x, d1.y, d2.x, d2.y, light);
        _fb->drawLine(bl.x, bl.y, ml.x, ml.y, dim(hull, 0.28f));
        _fb->drawLine(br.x, br.y, mr.x, mr.y, dim(hull, 0.28f));

        // Lit windows along the superstructure.
        for (int k = 0; k < 4; k++) {
          float fx = L * (0.30f - k * 0.28f);
          Vec2 w1 = B(fx,  W * 0.42f), w2 = B(fx, -W * 0.42f);
          _fb->drawPixel((int)w1.x, (int)w1.y, rgb(255, 236, 190));
          _fb->drawPixel((int)w2.x, (int)w2.y, rgb(255, 236, 190));
        }
      }
      break;
    }
  }

  // Engine glow, sized by class and brighter under acceleration.
  float thrust = fminf(1.0f, s.vel.len() / 90.0f);
  float er = (s.cls == CAPITAL ? 0.16f : s.cls == CRUISER ? 0.13f : 0.10f) * len;
  Vec2 ex = B(-L * 1.02f, 0);

  if (er >= 1.2f) {
    _fb->fillCircle((int)ex.x, (int)ex.y, (int)er, dim(glow, 0.55f + 0.45f * thrust));
    _fb->drawPixel((int)ex.x, (int)ex.y, mix(glow, rgb(255, 255, 255), 0.6f));
    if (s.cls != FIGHTER) {
      // Twin engines on the larger hulls.
      Vec2 e1 = B(-L * 1.02f,  W * 0.45f), e2 = B(-L * 1.02f, -W * 0.45f);
      _fb->fillCircle((int)e1.x, (int)e1.y, (int)fmaxf(1.0f, er * 0.6f), dim(glow, 0.8f));
      _fb->fillCircle((int)e2.x, (int)e2.y, (int)fmaxf(1.0f, er * 0.6f), dim(glow, 0.8f));
    }
  } else {
    _fb->drawPixel((int)ex.x, (int)ex.y, glow);
  }

  // Battle damage: hull darkens and flickers as it takes hits, so a ship about
  // to die is visible without needing the health bar.
  float f = s.hp / s.hpMax;
  if (f < 0.35f && len > 6) {
    if ((millis() >> 6) % 3 == 0)
      _fb->fillCircle((int)p.x, (int)p.y, (int)fmaxf(1.0f, len * 0.13f),
                      rgb(255, 150, 40));
  }

  // Health bar only at close zoom, where there is room for it to be legible.
  if (z > 1.8f && f < 1.0f) {
    int bw = (int)(len * 0.8f);
    int bx = (int)(p.x - bw / 2), by = (int)(p.y - len * 0.72f);
    _fb->drawFastHLine(bx, by, bw, rgb(70, 22, 22));
    _fb->drawFastHLine(bx, by, (int)(bw * f),
                       f > 0.5f ? rgb(60, 230, 80) : rgb(250, 170, 45));
  }
}

void Renderer::drawMuteButton(bool muted, float alpha) {
  if (alpha <= 0.01f) return;

  const int cx = MUTE_ICON_X, cy = MUTE_ICON_Y;

  // Dark disc behind the icon so it stays legible over the planet or a bright
  // patch of fighting, rather than only over empty space.
  uint16_t plate = mix(TFT_BLACK, rgb(22, 26, 36), alpha);
  _fb->fillCircle(cx, cy, 15, plate);
  _fb->drawSmoothCircle(cx, cy, 15, mix(TFT_BLACK, rgb(70, 80, 100), alpha),
                        plate);

  uint16_t fg = mix(TFT_BLACK, muted ? rgb(230, 90, 80) : rgb(210, 225, 245),
                    alpha);

  // Speaker: a small box for the driver, then a cone flaring outward to the
  // right. The cone's wide edge must be on the right -- putting the apex there
  // instead draws a right-pointing triangle, which reads as a play arrow.
  _fb->fillRect(cx - 11, cy - 3, 5, 7, fg);                          // driver box
  _fb->fillTriangle(cx - 7, cy, cx - 2, cy - 7, cx - 2, cy + 7, fg);  // cone

  if (muted) {
    // Slash across the icon.
    _fb->drawLine(cx - 9, cy + 9, cx + 9, cy - 9, fg);
    _fb->drawLine(cx - 9, cy + 8, cx + 9, cy - 10, fg);
  } else {
    // Two arcs radiating from the cone. TFT_eSPI measures arc angles with 0 at
    // the bottom, increasing anticlockwise: 90 is left, 180 up, 270 right. So
    // the right-hand side is 270, and spanning 300..60 puts the waves under the
    // icon instead of beside it.
    // Centred on the cone mouth, and sized so the outer arc stays clear of the
    // 15 px plate edge: at radius 9 from cx - 1 the furthest pixel is cx + 8.
    _fb->drawSmoothArc(cx - 1, cy, 6, 5, 230, 310, fg, plate, true);
    _fb->drawSmoothArc(cx - 1, cy, 9, 8, 240, 300,
                       mix(TFT_BLACK, rgb(150, 170, 200), alpha), plate, true);
  }
}

void Renderer::draw(const Battle &b, const Camera &cam, bool muted,
                    float buttonAlpha) {
  _fb->fillSprite(TFT_BLACK);

  drawStars(cam);
  drawPlanet(cam);

  float z = cam.zoom();

  // Debris under everything else.
  const Debris *db = b.debris();
  for (int i = 0; i < MAX_DEBRIS; i++) {
    if (!db[i].alive) continue;
    Vec2 p = cam.toScreen(db[i].pos);
    if (!cam.visible(p, 6)) continue;
    float f = db[i].life / db[i].lifeMax;

    // Cool from white through orange to deep red, and fade on a cubic curve so
    // the last part of the life is nearly invisible. A linear fade leaves dim
    // red dots visibly hanging in space well after the event.
    uint16_t hot = (f > 0.55f) ? mix(rgb(255, 170, 60), rgb(255, 255, 235),
                                     (f - 0.55f) / 0.45f)
                               : mix(rgb(180, 40, 10), rgb(255, 170, 60),
                                     f / 0.55f);
    uint16_t c = dim(hot, f * f * f * f * f);

    float r = db[i].size * z * f * f;
    if (r < 1.2f) _fb->drawPixel((int)p.x, (int)p.y, c);
    else          _fb->fillCircle((int)p.x, (int)p.y, (int)r, c);
  }

  // Shots. Drawn as a tapered tracer: a white-hot head that reads as a bolt of
  // energy, fading into the firing fleet's colour along the tail. A flat
  // single-colour pixel at this size is indistinguishable from a star, which is
  // why even the smallest tracer keeps a two-pixel head.
  const Shot *sh = b.shots();
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (!sh[i].alive) continue;
    Vec2 p = cam.toScreen(sh[i].pos);
    if (!cam.visible(p, 10)) continue;

    uint16_t tail = FLEET_SHOT[sh[i].fleet];
    uint16_t head = sh[i].heavy ? rgb(255, 244, 214) : rgb(255, 255, 255);

    float trailLen = sh[i].heavy ? 26.0f : 15.0f;
    Vec2 back = sh[i].pos - sh[i].vel.norm() * trailLen;
    Vec2 q = cam.toScreen(back);

    Vec2 d = p - q;
    float pix = d.len();

    if (pix < 1.5f) {
      // Too small for a streak, but still brighter than any star.
      _fb->drawPixel((int)p.x, (int)p.y, head);
      continue;
    }

    // Fade the streak from the fleet colour at the tail to white at the head.
    int steps = (int)fminf(pix, 12.0f);
    for (int k = 0; k < steps; k++) {
      float t = (float)k / (float)(steps - 1 > 0 ? steps - 1 : 1);
      Vec2 a = q + d * t;
      _fb->drawPixel((int)a.x, (int)a.y, mix(dim(tail, 0.45f), head, t * t));
    }

    // Heavy capital rounds get width so they read as a different weapon class.
    if (sh[i].heavy && pix > 3.0f) {
      Vec2 n = Vec2{-d.y, d.x}.norm();
      _fb->drawLine(q.x + n.x, q.y + n.y, p.x + n.x, p.y + n.y, dim(tail, 0.7f));
      _fb->drawLine(q.x - n.x, q.y - n.y, p.x - n.x, p.y - n.y, dim(tail, 0.7f));
      _fb->drawPixel((int)p.x, (int)p.y, head);
    }
  }

  // Ships on top.
  const Ship *s = b.ships();
  for (int i = 0; i < MAX_SHIPS; i++)
    if (s[i].alive) drawShip(s[i], cam);

  drawMuteButton(muted, buttonAlpha);

  // One continuous transfer. Splitting this into bands with a yield between
  // them tears visibly: the simulation is drawn once but the panel receives it
  // in pieces separated in time, so fast sprites shear across band boundaries.
  // At 80 MHz the whole 150 KB frame goes out in about 15 ms, comfortably
  // inside both watchdogs, so there is no reason to break it up.
  _fb->pushSprite(0, 0);
}
