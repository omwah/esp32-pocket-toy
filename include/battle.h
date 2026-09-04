#pragma once
#include "config.h"
#include "vec2.h"

enum ShipClass : uint8_t { FIGHTER = 0, CRUISER = 1, CAPITAL = 2 };

// Something worth hearing. The simulation records these each tick; main drains
// the list and turns them into sound, so Battle stays independent of audio.
enum EventKind : uint8_t { EV_FIRE_LIGHT = 0, EV_FIRE_HEAVY, EV_HIT, EV_DEATH };

struct BattleEvent {
  Vec2      pos;
  EventKind kind;
  uint8_t   cls;    // ship class involved, for weighting
};

static constexpr int MAX_EVENTS = 24;

struct Ship {
  Vec2  pos, vel;
  float angle    = 0;      // facing, radians
  float hp       = 0;
  float hpMax    = 1;
  float cooldown = 0;
  int16_t target = -1;     // index into ships, -1 if none
  uint8_t fleet  = 0;      // 0 or 1
  uint8_t cls    = FIGHTER;
  bool    alive  = false;
};

struct Shot {
  Vec2  pos, vel;
  float life  = 0;         // seconds remaining
  uint8_t fleet = 0;
  bool  heavy = false;     // capital ship fire: bigger, slower, harder hitting
  bool  alive = false;
};

struct Debris {
  Vec2  pos, vel;
  float life = 0, lifeMax = 1;
  float size = 1;
  bool  alive = false;
};

// The whole simulation. Fixed-size pools, no allocation after begin().
class Battle {
public:
  void begin(uint32_t seed);
  void update(float dt);

  // Centre of mass of everything still fighting -- the camera follows this.
  Vec2 actionCentre() const { return _action; }

  // Weighted spread of the fighting about that centre, in world units. The
  // camera uses this to choose a zoom that frames the battle by itself.
  Vec2 actionSpread() const { return _spread; }

  const Ship   *ships()  const { return _ships; }
  const Shot   *shots()  const { return _shots; }
  const Debris *debris() const { return _debris; }

  int aliveCount(int fleet) const { return _alive[fleet]; }

  // Events from the last update(). Valid until the next one.
  const BattleEvent *events() const { return _events; }
  int eventCount() const { return _eventCount; }

private:
  Ship   _ships[MAX_SHIPS];
  Shot   _shots[MAX_SHOTS];
  Debris _debris[MAX_DEBRIS];

  BattleEvent _events[MAX_EVENTS];
  int   _eventCount = 0;

  Vec2  _action{WORLD_W * 0.5f, WORLD_H * 0.5f};
  Vec2  _spread{WORLD_W * 0.25f, WORLD_H * 0.25f};
  int   _alive[2] = {0, 0};
  float _respawnTimer = 0;

  void spawnFleets();
  void spawnShip(int idx, int fleet, ShipClass cls);
  void fire(const Ship &s, const Vec2 &aim);
  void explode(const Vec2 &at, float scale);
  void muzzleFlash(const Vec2 &at, const Vec2 &dir, float scale);
  void retarget(int idx);
  void emit(EventKind kind, const Vec2 &at, uint8_t cls);
  void updateAction();
};
