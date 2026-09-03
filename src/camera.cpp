#include "camera.h"
#include <Arduino.h>

namespace {
// Cheap deterministic angle from the drift phase, so shot framing varies
// without needing a random source in the camera.
inline float frandPhase(float p) { return fmodf(p * 7.31f, TWO_PI); }
}  // namespace

void Camera::begin() {
  _centre = {WORLD_W * 0.5f, WORLD_H * 0.5f};
  _zoom = ZOOM_DEFAULT;
  // Backdate the last touch so the automatic camera runs from the first frame.
  _lastTouchMs = 0 - IDLE_RESUME_MS;
  _holdTimer = 0;
}

void Camera::clamp() {
  if (_zoom < ZOOM_MIN) _zoom = ZOOM_MIN;
  if (_zoom > ZOOM_MAX) _zoom = ZOOM_MAX;

  // Keep the view inside the world, but if the world is smaller than the
  // viewport at this zoom, centre it instead of fighting the limits.
  float halfW = SCREEN_W * 0.5f / _zoom;
  float halfH = SCREEN_H * 0.5f / _zoom;

  if (halfW * 2 >= WORLD_W) _centre.x = WORLD_W * 0.5f;
  else _centre.x = constrain(_centre.x, halfW, WORLD_W - halfW);

  if (halfH * 2 >= WORLD_H) _centre.y = WORLD_H * 0.5f;
  else _centre.y = constrain(_centre.y, halfH, WORLD_H - halfH);
}

void Camera::onTouch(int count, const Vec2 &p0, const Vec2 &p1, uint32_t nowMs) {
  _lastTouchMs = nowMs;

  if (count >= 2) {
    float d = (p1 - p0).len();
    if (!_pinching) {
      // Start of a pinch: remember the reference span and zoom.
      _pinching = true;
      _dragging = false;
      _pinchStartDist = d > 1.0f ? d : 1.0f;
      _pinchStartZoom = _zoom;
      _lastP0 = (p0 + p1) * 0.5f;
    } else {
      _zoom = _pinchStartZoom * (d / _pinchStartDist);

      // Two-finger drag pans as well, so zoom and pan compose naturally.
      Vec2 mid = (p0 + p1) * 0.5f;
      Vec2 delta = mid - _lastP0;
      _centre += Vec2{-delta.x / _zoom, -delta.y / _zoom};
      _lastP0 = mid;
    }
    clamp();
    return;
  }

  // Single finger: drag to pan.
  _pinching = false;
  if (!_dragging) {
    _dragging = true;
    _lastP0 = p0;
  } else {
    Vec2 delta = p0 - _lastP0;
    _centre += Vec2{-delta.x / _zoom, -delta.y / _zoom};
    _lastP0 = p0;
    clamp();
  }
}

void Camera::onRelease(uint32_t nowMs) {
  if (_dragging || _pinching) _lastTouchMs = nowMs;
  _dragging = false;
  _pinching = false;
}

void Camera::update(float dt, uint32_t nowMs, const Vec2 &actionCentre,
                    const Vec2 &actionSpread) {
  if (_dragging || _pinching) return;
  if (nowMs - _lastTouchMs < IDLE_RESUME_MS) return;

  _driftPhase += dt * 0.11f;

  // Cut between two kinds of shot so the view neither sits static nor wanders
  // off the fighting: a wide one that frames the whole engagement, and a close
  // one that pushes in on part of it.
  _holdTimer -= dt;
  if (_holdTimer <= 0) {
    _closeUp = !_closeUp;
    _holdTimer = _closeUp ? 7.0f : 9.0f;

    if (_closeUp) {
      // Aim somewhere inside the fighting, not at its exact centre.
      float a = frandPhase(_driftPhase);
      _shotOffset = {cosf(a) * actionSpread.x * 0.45f,
                     sinf(a * 1.7f) * actionSpread.y * 0.45f};
    } else {
      _shotOffset = {0, 0};
    }
  }

  // Zoom that fits the action, with margin so ships are not clipped at the edge.
  // Guard the spread against tiny values, which would demand absurd zoom.
  float spreadW = fmaxf(actionSpread.x, 60.0f) * 2.6f;
  float spreadH = fmaxf(actionSpread.y, 60.0f) * 2.6f;
  float fitZoom = fminf(SCREEN_W / spreadW, SCREEN_H / spreadH);

  float zTarget = _closeUp ? fitZoom * 2.3f : fitZoom;
  zTarget *= 1.0f + 0.06f * sinf(_driftPhase * 0.9f);  // slow breathing
  zTarget = constrain(zTarget, ZOOM_MIN, ZOOM_MAX);

  Vec2 target = actionCentre + _shotOffset;

  // Frame-rate independent easing. The pan is slower than the zoom so the
  // motion reads as deliberate camera work rather than snapping.
  _centre += (target - _centre) * (1.0f - expf(-dt * 0.9f));
  _zoom   += (zTarget - _zoom)  * (1.0f - expf(-dt * 0.7f));

  clamp();
}
