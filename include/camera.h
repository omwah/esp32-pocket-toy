#pragma once
#include "config.h"
#include "vec2.h"

// Maps world coordinates to screen pixels. Pans and zooms under touch control,
// and drifts on its own when the user has been idle.
class Camera {
public:
  void begin();
  // actionSpread is the world-space extent of the fighting; the camera picks a
  // zoom that frames it, so the battle stays on screen without user input.
  void update(float dt, uint32_t nowMs, const Vec2 &actionCentre,
              const Vec2 &actionSpread);

  // Touch gestures. The controller reports at most two points.
  void onTouch(int count, const Vec2 &p0, const Vec2 &p1, uint32_t nowMs);
  void onRelease(uint32_t nowMs);

  Vec2 toScreen(const Vec2 &world) const {
    return {(world.x - _centre.x) * _zoom + SCREEN_W * 0.5f,
            (world.y - _centre.y) * _zoom + SCREEN_H * 0.5f};
  }

  float zoom()  const { return _zoom; }
  Vec2  centre() const { return _centre; }

  // Generous margin so sprites that straddle the edge still draw.
  bool visible(const Vec2 &s, float margin = 24.0f) const {
    return s.x > -margin && s.x < SCREEN_W + margin &&
           s.y > -margin && s.y < SCREEN_H + margin;
  }

private:
  Vec2  _centre{WORLD_W * 0.5f, WORLD_H * 0.5f};
  float _zoom = ZOOM_DEFAULT;

  // Gesture state.
  bool  _dragging = false;
  bool  _pinching = false;
  Vec2  _lastP0, _lastP1;
  float _pinchStartDist = 0;
  float _pinchStartZoom = 1;
  uint32_t _lastTouchMs = 0;

  // Autonomous camera work.
  float _driftPhase   = 0;
  float _holdTimer    = 0;   // seconds left on the current shot
  bool  _closeUp      = false;
  Vec2  _shotOffset{0, 0};   // where in the battle this shot is pointed

  void clamp();
};
