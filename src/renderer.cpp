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
    s.x = random(0, (long)WORLD_W * 2) - WORLD_W * 0.5f;
    s.y = random(0, (long)WORLD_H * 2) - WORLD_H * 0.5f;
    s.layer = random(0, 3);

    // Far stars are dimmer, near stars brighter and occasionally tinted.
    uint8_t v = 70 + s.layer * 60 + random(0, 30);
    if (random(0, 9) == 0)      s.colour = rgb(v, v * 0.8f, 255 - v * 0.2f);
    else if (random(0, 11) == 0) s.colour = rgb(255 - v * 0.15f, v * 0.85f, v * 0.7f);
    else                         s.colour = rgb(v, v, v);
  }
}

void Renderer::drawStars(const Camera &cam) {
  // Parallax: distant layers shift less than the world does.
  const float PAR[3] = {0.18f, 0.38f, 0.66f};

  for (const Star &s : _stars) {
    float p = PAR[s.layer];
    Vec2 world{s.x, s.y};
    Vec2 sc = cam.toScreen(world);

    // Re-project with reduced parallax about the screen centre.
    sc.x = (sc.x - SCREEN_W * 0.5f) * p + SCREEN_W * 0.5f;
    sc.y = (sc.y - SCREEN_H * 0.5f) * p + SCREEN_H * 0.5f;

    // Wrap so the field never runs out as the camera travels.
    float x = fmodf(sc.x, (float)SCREEN_W); if (x < 0) x += SCREEN_W;
    float y = fmodf(sc.y, (float)SCREEN_H); if (y < 0) y += SCREEN_H;

    if (s.layer == 2) _fb->drawPixel((int)x, (int)y, s.colour);
    else              _fb->drawPixel((int)x, (int)y, dim(s.colour, 0.75f));
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
  uint16_t hull = FLEET_HULL[s.fleet];

  // Size in pixels by class, scaled by zoom.
  float len = (s.cls == CAPITAL ? 30.0f : s.cls == CRUISER ? 15.0f : 7.0f) * z;

  // Below a couple of pixels there is no shape to draw -- just a dot, which is
  // what keeps the wide zoom-out cheap with hundreds of ships on screen.
  if (len < 2.5f) { _fb->drawPixel((int)p.x, (int)p.y, hull); return; }
  if (len < 5.0f) {
    _fb->drawPixel((int)p.x, (int)p.y, hull);
    _fb->drawPixel((int)p.x + 1, (int)p.y, dim(hull, 0.6f));
    return;
  }

  float ca = cosf(s.angle), sa = sinf(s.angle);
  auto body = [&](float fx, float fy) -> Vec2 {
    return {p.x + fx * ca - fy * sa, p.y + fx * sa + fy * ca};
  };

  float w = len * 0.38f;
  Vec2 nose = body(len * 0.5f, 0);
  Vec2 la   = body(-len * 0.5f,  w);
  Vec2 lb   = body(-len * 0.5f, -w);

  _fb->fillTriangle(nose.x, nose.y, la.x, la.y, lb.x, lb.y, hull);

  // Engine glow trailing the hull.
  Vec2 tail = body(-len * 0.55f, 0);
  _fb->drawPixel((int)tail.x, (int)tail.y, FLEET_TRAIL[s.fleet]);

  // Capitals get a spine and a hull outline so they read as the big ships.
  if (s.cls == CAPITAL && len > 12) {
    _fb->drawLine(nose.x, nose.y, tail.x, tail.y, dim(hull, 1.4f > 1 ? 1.0f : 1.0f));
    _fb->drawTriangle(nose.x, nose.y, la.x, la.y, lb.x, lb.y, dim(hull, 0.55f));
  }

  // Damage bar, only when close enough for it to mean anything.
  if (z > 1.6f && s.hp < s.hpMax) {
    int bw = (int)(len * 0.8f);
    int bx = (int)(p.x - bw / 2), by = (int)(p.y - len * 0.75f);
    float f = s.hp / s.hpMax;
    _fb->drawFastHLine(bx, by, bw, rgb(60, 20, 20));
    _fb->drawFastHLine(bx, by, (int)(bw * f), f > 0.5f ? rgb(40, 220, 60)
                                                       : rgb(240, 160, 40));
  }
}

void Renderer::draw(const Battle &b, const Camera &cam) {
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
    uint16_t c = dim(rgb(255, 190 * f, 70 * f), f);
    float r = db[i].size * z * f;
    if (r < 1.2f) _fb->drawPixel((int)p.x, (int)p.y, c);
    else          _fb->fillCircle((int)p.x, (int)p.y, (int)r, c);
  }

  // Shots.
  const Shot *sh = b.shots();
  for (int i = 0; i < MAX_SHOTS; i++) {
    if (!sh[i].alive) continue;
    Vec2 p = cam.toScreen(sh[i].pos);
    if (!cam.visible(p, 8)) continue;
    uint16_t c = FLEET_SHOT[sh[i].fleet];

    // Draw as a short streak along the direction of travel.
    Vec2 back = sh[i].pos - sh[i].vel.norm() * (sh[i].heavy ? 16.0f : 9.0f);
    Vec2 q = cam.toScreen(back);
    if (z < 0.8f) _fb->drawPixel((int)p.x, (int)p.y, c);
    else {
      _fb->drawLine(q.x, q.y, p.x, p.y, c);
      if (sh[i].heavy && z > 1.2f)
        _fb->drawLine(q.x, q.y + 1, p.x, p.y + 1, dim(c, 0.6f));
    }
  }

  // Ships on top.
  const Ship *s = b.ships();
  for (int i = 0; i < MAX_SHIPS; i++)
    if (s[i].alive) drawShip(s[i], cam);

  // One continuous transfer. Splitting this into bands with a yield between
  // them tears visibly: the simulation is drawn once but the panel receives it
  // in pieces separated in time, so fast sprites shear across band boundaries.
  // At 80 MHz the whole 150 KB frame goes out in about 15 ms, comfortably
  // inside both watchdogs, so there is no reason to break it up.
  _fb->pushSprite(0, 0);
}
