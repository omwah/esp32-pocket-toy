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

// ---- Simulation scale ----
// World units are arbitrary; the camera maps them to pixels.
static constexpr int   MAX_SHIPS      = 220;   // total across both fleets
static constexpr int   MAX_SHOTS      = 320;
static constexpr int   MAX_DEBRIS     = 140;
static constexpr int   NUM_STARS      = 190;

static constexpr float WORLD_W        = 2400.0f;
static constexpr float WORLD_H        = 1800.0f;

// ---- Camera ----
static constexpr float ZOOM_MIN       = 0.16f;  // whole battlefield in frame
static constexpr float ZOOM_MAX       = 4.50f;  // close enough to see a single ship
static constexpr float ZOOM_DEFAULT   = 1.00f;

// Seconds of no touch before the camera resumes its automatic drift.
static constexpr uint32_t IDLE_RESUME_MS = 6000;
