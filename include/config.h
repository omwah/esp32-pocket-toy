// Board and simulation constants for the space battle screensaver.
// Hardware details and how they were verified live in DEVICE.md.
#pragma once

#include <stdint.h>

// ---- Display ----
static constexpr int SCREEN_W = 320;   // landscape, setRotation(1)
static constexpr int SCREEN_H = 240;

// ---- Touch (FT6336G) ----
static constexpr uint8_t TOUCH_I2C_ADDR = 0x38;  // 0x18 is the audio codec, not touch
static constexpr int TOUCH_SDA = 16;
static constexpr int TOUCH_SCL = 15;
static constexpr int TOUCH_INT = 17;
static constexpr int TOUCH_RST = 18;

// ---- Audio (ES8311 codec + FM8002E amplifier) ----
static constexpr uint8_t ES8311_I2C_ADDR = 0x18;  // shares the touch I2C bus
static constexpr int I2S_MCLK = 4;
static constexpr int I2S_BCLK = 5;
static constexpr int I2S_LRCK = 7;
static constexpr int I2S_DOUT = 8;
static constexpr int I2S_DIN  = 6;
static constexpr int SPK_ENABLE = 1;   // LOW enables the amplifier, HIGH mutes

static constexpr int   AUDIO_RATE   = 16000;   // Hz
static constexpr int   AUDIO_VOICES = 6;       // simultaneous effects

// ---- Simulation scale ----
// World units are arbitrary; the camera maps them to pixels.
static constexpr int   MAX_SHIPS      = 220;   // total across both fleets
static constexpr int   MAX_SHOTS      = 320;
static constexpr int   MAX_DEBRIS     = 140;
static constexpr int   NUM_STARS      = 190;

static constexpr float WORLD_W        = 2400.0f;
static constexpr float WORLD_H        = 1800.0f;

// ---- Mute button ----
// Bottom-left corner, clear of the top corners where the camera puts most of
// the action. The touch target is deliberately larger than the drawn icon.
static constexpr int MUTE_ICON_X = 20;
static constexpr int MUTE_ICON_Y = 216;
static constexpr int MUTE_HIT_R  = 30;

// A press only counts as a button tap if it stays within this many pixels and
// is released within this long; anything else is a camera pan.
static constexpr float TAP_SLOP_PX = 14.0f;
static constexpr uint32_t TAP_MAX_MS = 450;

// The button fades out when unused so it does not sit on the artwork forever.
static constexpr uint32_t BUTTON_VISIBLE_MS = 4000;
static constexpr uint32_t BUTTON_FADE_MS    = 900;

// ---- Camera ----
static constexpr float ZOOM_MIN       = 0.16f;  // whole battlefield in frame
static constexpr float ZOOM_MAX       = 4.50f;  // close enough to see a single ship
static constexpr float ZOOM_DEFAULT   = 1.00f;

// Seconds of no touch before the camera resumes its automatic drift.
static constexpr uint32_t IDLE_RESUME_MS = 6000;
