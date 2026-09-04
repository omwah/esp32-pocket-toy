#pragma once
#include "config.h"
#include <stdint.h>

// Sound effects for the battle, synthesised on the fly and mixed into an I2S
// stream feeding the board's ES8311 codec.
//
// Nothing is sampled or stored: each effect is a short procedural voice, which
// keeps the whole system to a few hundred bytes of state and avoids putting
// audio assets in flash.
enum SfxKind : uint8_t {
  SFX_LASER = 0,   // fighter and cruiser weapons
  SFX_CANNON,      // capital ship heavy round
  SFX_HIT,         // shot striking a hull
  SFX_EXPLOSION,   // ship destroyed
  SFX_RUMBLE,      // low bed under a capital ship dying
};

class Audio {
public:
  bool begin();

  // Trigger an effect. `pan` is -1 (left) to +1 (right), `gain` scales level.
  // Safe to call from the simulation at any rate: if every voice is busy the
  // quietest one is replaced, so a burst of events cannot starve later ones.
  void play(SfxKind kind, float gain = 1.0f, float pan = 0.0f);

  // Fill and push one buffer. Call once per frame.
  void update();

  bool present() const { return _ok; }

  // Muting also disables the power amplifier, which removes the idle hiss the
  // FM8002E puts on the speaker -- worth it for something that runs unattended.
  void setMuted(bool m);
  bool muted() const { return _muted; }

private:
  struct Voice {
    SfxKind  kind;
    uint32_t t;        // samples elapsed
    uint32_t len;      // total samples
    float    freq;     // current oscillator frequency
    float    freqK;    // per-sample frequency multiplier (sweep)
    float    phase;
    float    gainL, gainR;
    uint32_t noise;    // LFSR state
    bool     active;
  };

  Voice _voices[AUDIO_VOICES];
  bool  _ok = false;
  bool  _muted = false;

  bool writeReg(uint8_t reg, uint8_t val);
  bool initCodec();
  int  allocVoice(float gain);
};
