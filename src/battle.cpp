#include "battle.h"
#include <Arduino.h>

namespace {

// Ship class stats: hp, speed, reload seconds, weapon range, turn rate.
struct ClassStats {
  float hp, speed, reload, range, turn;
};
const ClassStats STATS[3] = {
  /* FIGHTER */ { 26.0f, 108.0f, 1.35f, 190.0f, 3.4f},
  /* CRUISER */ {110.0f,  62.0f, 2.40f, 300.0f, 1.5f},
  /* CAPITAL */ {420.0f,  33.0f, 3.60f, 420.0f, 0.6f},
};

inline float frand(float lo, float hi) {
  return lo + (float)random(0, 10000) / 10000.0f * (hi - lo);
}

inline float wrapAngle(float a) {
  while (a >  PI) a -= TWO_PI;
  while (a < -PI) a += TWO_PI;
  return a;
}

}  // namespace

void Battle::begin(uint32_t seed) {
  randomSeed(seed);
  for (auto &s : _ships)  s.alive  = false;
  for (auto &s : _shots)  s.alive  = false;
  for (auto &d : _debris) d.alive  = false;
  spawnFleets();
}

void Battle::spawnShip(int idx, int fleet, ShipClass cls) {
  Ship &s = _ships[idx];
  const ClassStats &st = STATS[cls];

  // Fleet 0 enters from the left, fleet 1 from the right.
  float side = (fleet == 0) ? 0.0f : 1.0f;
  float x = side ? frand(WORLD_W * 0.60f, WORLD_W * 0.78f)
                 : frand(WORLD_W * 0.22f, WORLD_W * 0.40f);
  float y = frand(WORLD_H * 0.25f, WORLD_H * 0.75f);

  s.pos   = {x, y};
  s.angle = side ? PI : 0.0f;
  s.vel   = {cosf(s.angle) * st.speed * 0.5f, sinf(s.angle) * st.speed * 0.5f};
  s.hpMax = st.hp;
  s.hp    = st.hp;
  s.cooldown = frand(0.0f, st.reload);
  s.fleet = fleet;
  s.cls   = cls;
  s.target = -1;
  s.alive = true;
}

void Battle::spawnFleets() {
  // Composition per fleet: mostly fighters, a few cruisers, one or two capitals.
  const int perFleet = MAX_SHIPS / 2;
  int idx = 0;
  for (int fleet = 0; fleet < 2; fleet++) {
    for (int i = 0; i < perFleet; i++, idx++) {
      ShipClass cls = FIGHTER;
      if (i < 2)      cls = CAPITAL;
      else if (i < 9) cls = CRUISER;
      spawnShip(idx, fleet, cls);
    }
  }
  _alive[0] = _alive[1] = perFleet;
}

void Battle::emit(EventKind kind, const Vec2 &at, uint8_t cls) {
  // Silently drop once full. A tick that produces more than this many events is
  // already a wall of noise; the extras would not be individually audible.
  if (_eventCount >= MAX_EVENTS) return;
  _events[_eventCount++] = {at, kind, cls};
}

void Battle::retarget(int idx) {
  Ship &s = _ships[idx];
  int   best = -1;
  float bestD2 = 1e18f;

  // Nearest live enemy. Scanning a stride of the array keeps this cheap while
  // still converging quickly, since retarget runs often across many ships.
  for (int i = 0; i < MAX_SHIPS; i++) {
    const Ship &o = _ships[i];
    if (!o.alive || o.fleet == s.fleet) continue;
    float d2 = (o.pos - s.pos).len2();
    if (d2 < bestD2) { bestD2 = d2; best = i; }
  }
  s.target = best;
}

void Battle::fire(const Ship &s, const Vec2 &aim) {
  for (auto &sh : _shots) {
    if (sh.alive) continue;
    bool heavy = (s.cls == CAPITAL);
    float speed = heavy ? 210.0f : 330.0f;
    sh.pos   = s.pos + aim * 9.0f;
    sh.vel   = aim * speed + s.vel * 0.35f;
    sh.life  = STATS[s.cls].range / speed;
    sh.fleet = s.fleet;
    sh.heavy = heavy;
    sh.alive = true;
    return;
  }
}

void Battle::muzzleFlash(const Vec2 &at, const Vec2 &dir, float scale) {
  int n = (scale > 0.8f) ? 2 : 1;
  for (auto &d : _debris) {
    if (n <= 0) break;
    if (d.alive) continue;
    // Thrown forward in a narrow cone, unlike an explosion's radial burst.
    float a = atan2f(dir.y, dir.x) + frand(-0.35f, 0.35f);
    float v = frand(70.0f, 150.0f) * scale;
    d.pos     = at;
    d.vel     = {cosf(a) * v, sinf(a) * v};
    d.lifeMax = frand(0.035f, 0.075f);
    d.life    = d.lifeMax;
    d.size    = frand(0.8f, 1.4f) * scale;
    d.alive   = true;
    n--;
  }
}

void Battle::explode(const Vec2 &at, float scale) {
  int n = (int)(6 * scale);
  for (auto &d : _debris) {
    if (n <= 0) break;
    if (d.alive) continue;
    float a = frand(0, TWO_PI);
    float v = frand(18.0f, 95.0f) * scale;
    d.pos     = at;
    d.vel     = {cosf(a) * v, sinf(a) * v};
    d.lifeMax = frand(0.10f, 0.28f) * (0.5f + 0.6f * scale);
    d.life    = d.lifeMax;
    d.size    = frand(1.0f, 2.2f) * scale;
    d.alive   = true;
    n--;
  }
}

void Battle::updateAction() {
  // Frame the contested zone, not the overall centre of mass. Averaging every
  // ship puts the camera in the gap between two fleets that have not met yet;
  // weighting each ship by how close its nearest enemy is puts the camera on
  // the ships that are actually fighting.
  Vec2  sum{0, 0};
  float wsum = 0;

  for (int i = 0; i < MAX_SHIPS; i++) {
    const Ship &s = _ships[i];
    if (!s.alive) continue;

    // Distance to this ship's current target stands in for engagement range;
    // it is already computed by the AI, so this costs nothing extra.
    float engaged = 0.15f;
    if (s.target >= 0 && _ships[s.target].alive) {
      float d = (_ships[s.target].pos - s.pos).len();
      // Full weight inside 260 units, tapering to nothing by 900.
      engaged = (d < 260.0f) ? 1.0f
                             : fmaxf(0.0f, 1.0f - (d - 260.0f) / 640.0f);
    }

    float cls = (s.cls == CAPITAL) ? 4.0f : (s.cls == CRUISER ? 2.0f : 1.0f);
    float w = cls * (0.12f + engaged);
    sum += s.pos * w;
    wsum += w;
  }

  if (wsum <= 0) return;

  Vec2 target = sum * (1.0f / wsum);
  _action += (target - _action) * 0.02f;

  // Spread of the engaged ships about that centre, weighted the same way, so
  // the camera zooms to fit the fighting rather than the whole map.
  Vec2 var{0, 0};
  float vsum = 0;
  for (int i = 0; i < MAX_SHIPS; i++) {
    const Ship &s = _ships[i];
    if (!s.alive) continue;

    float engaged = 0.15f;
    if (s.target >= 0 && _ships[s.target].alive) {
      float d = (_ships[s.target].pos - s.pos).len();
      engaged = (d < 260.0f) ? 1.0f
                             : fmaxf(0.0f, 1.0f - (d - 260.0f) / 640.0f);
    }
    float cls = (s.cls == CAPITAL) ? 4.0f : (s.cls == CRUISER ? 2.0f : 1.0f);
    float w = cls * (0.12f + engaged);

    Vec2 d = s.pos - target;
    var += Vec2{d.x * d.x * w, d.y * d.y * w};
    vsum += w;
  }

  Vec2 sd{sqrtf(var.x / vsum), sqrtf(var.y / vsum)};
  _spread += (sd - _spread) * 0.02f;
}

void Battle::update(float dt) {
  _alive[0] = _alive[1] = 0;
  _eventCount = 0;

  // --- Ships ---
  for (int i = 0; i < MAX_SHIPS; i++) {
    Ship &s = _ships[i];
    if (!s.alive) continue;
    _alive[s.fleet]++;

    const ClassStats &st = STATS[s.cls];

    // Reacquire when the target is gone, or occasionally to keep formations fluid.
    if (s.target < 0 || !_ships[s.target].alive || random(0, 220) == 0) retarget(i);

    if (s.target >= 0) {
      const Ship &t = _ships[s.target];
      Vec2 to = t.pos - s.pos;
      float dist = to.len();
      Vec2 dir = to.norm();

      // Turn toward the target at a class-limited rate.
      float want = atan2f(dir.y, dir.x);
      float diff = wrapAngle(want - s.angle);
      float step = st.turn * dt;
      s.angle += constrain(diff, -step, step);

      // Close to weapons range, then hold station and circle.
      Vec2 heading{cosf(s.angle), sinf(s.angle)};
      float standoff = st.range * 0.7f;
      float throttle = (dist > standoff) ? 1.0f : -0.25f;
      s.vel += heading * (st.speed * throttle * dt * 2.2f);

      // Light drag keeps speeds bounded without a hard clamp.
      s.vel = s.vel * (1.0f - 0.9f * dt);

      s.cooldown -= dt;
      if (s.cooldown <= 0 && dist < st.range && fabsf(diff) < 0.5f) {
        s.cooldown = st.reload * frand(0.8f, 1.25f);
        // Lead the target slightly so shots are not always trailing.
        Vec2 lead = (t.pos + t.vel * (dist / 330.0f)) - s.pos;
        Vec2 aim = lead.norm();
        fire(s, aim);
        // A brief flash at the muzzle so the shot visibly originates from a
        // ship. Kept very short-lived: at this rate of fire anything lingering
        // accumulates into a field of stray dots.
        muzzleFlash(s.pos + aim * 10.0f, aim, s.cls == CAPITAL ? 1.0f : 0.45f);
        emit(s.cls == CAPITAL ? EV_FIRE_HEAVY : EV_FIRE_LIGHT, s.pos, s.cls);
      }
    }

    s.pos += s.vel * dt;

    // Soft bounds: steer back rather than teleporting.
    if (s.pos.x < 40)            s.vel.x += 90 * dt;
    if (s.pos.x > WORLD_W - 40)  s.vel.x -= 90 * dt;
    if (s.pos.y < 40)            s.vel.y += 90 * dt;
    if (s.pos.y > WORLD_H - 40)  s.vel.y -= 90 * dt;
  }

  // --- Shots ---
  for (auto &sh : _shots) {
    if (!sh.alive) continue;
    sh.pos += sh.vel * dt;
    sh.life -= dt;
    if (sh.life <= 0) { sh.alive = false; continue; }

    // Collision against enemy ships. Radius scales with hull size.
    for (int i = 0; i < MAX_SHIPS; i++) {
      Ship &s = _ships[i];
      if (!s.alive || s.fleet == sh.fleet) continue;
      float r = (s.cls == CAPITAL) ? 15.0f : (s.cls == CRUISER ? 8.0f : 5.0f);
      if ((s.pos - sh.pos).len2() > r * r) continue;

      s.hp -= sh.heavy ? 14.0f : 3.0f;
      sh.alive = false;
      if (s.hp <= 0) {
        s.alive = false;
        explode(s.pos, s.cls == CAPITAL ? 3.0f : (s.cls == CRUISER ? 1.8f : 1.0f));
        emit(EV_DEATH, s.pos, s.cls);
      } else {
        emit(EV_HIT, s.pos, s.cls);
      }
      break;
    }
  }

  // --- Debris ---
  for (auto &d : _debris) {
    if (!d.alive) continue;
    d.pos += d.vel * dt;
    d.vel = d.vel * (1.0f - 1.4f * dt);
    d.life -= dt;
    if (d.life <= 0) d.alive = false;
  }

  updateAction();

  // Feed reinforcements in so the battle never runs dry. A screensaver that
  // resolves in half a minute and then blanks is not much of a screensaver.
  _respawnTimer += dt;
  if (_respawnTimer > 1.2f) {
    _respawnTimer = 0;
    for (int fleet = 0; fleet < 2; fleet++) {
      // Reinforce harder when a side is losing, so neither is ever wiped out.
      int deficit = (MAX_SHIPS / 2) - _alive[fleet];
      if (deficit <= 0) continue;
      int wave = (deficit > 24) ? 3 : 1;

      for (int w = 0; w < wave; w++) {
        for (int i = 0; i < MAX_SHIPS; i++) {
          if (_ships[i].alive) continue;
          // Capitals are rare; the losing side gets one occasionally.
          ShipClass cls = FIGHTER;
          int roll = random(0, 100);
          if (deficit > 30 && roll < 4)      cls = CAPITAL;
          else if (roll < 18)                cls = CRUISER;
          spawnShip(i, fleet, cls);
          break;
        }
      }
    }
  }
}
