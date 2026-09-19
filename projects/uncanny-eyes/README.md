# Uncanny Eyes for the Hosyond ES3C28P

Animated eyes for the Hosyond ESP32-S3 2.8-inch touch display. The eyes roam,
blink, track touches, and cycle through ten designs using Adafruit's original
editable PNG artwork.

The animation is an original hardware-specific adaptation inspired by
[Adafruit Uncanny Eyes](https://github.com/adafruit/Uncanny_Eyes), written by
Phil Burgess for Adafruit Industries and released under the MIT license. It
retains the original sclera, iris, and eyelid artwork while adapting rendering,
gaze, touch input, and the display backend to TFT_eSPI on this ESP32-S3.

## Build and flash

From the repository root:

```sh
micromamba run -n platformio pio run -d projects/uncanny-eyes -t upload
```

The upload port is pinned in `platformio.ini`. Board details and recovery
instructions are in [`../../HARDWARE.md`](../../HARDWARE.md).

## Controls

- No input: autonomous gaze and blinking
- Touch/drag: both eyes follow the contact point (where supported by the style)
- Tap: select the next style and briefly show the battery percentage
- Press and hold without moving for 2 seconds: deep sleep
- BOOT button: wake from deep sleep

The styles follow the original collection: Hazel, Dragon, No Sclera,
Goat/Krampus, Newt, Terminator, Cartoon Cat, Owl, Nauga, and Realistic Deer.
Owl and Nauga retain the original designs' fixed gaze. Realistic Deer replaces
Adafruit's abstract cartoon Doe with a brown textured iris and the broad,
horizontal pupil characteristic of deer.

Each style occasionally plays a short recorded sound at a randomized 15–45
second interval. Samples and licensing details are documented in
[`audio/README.md`](audio/README.md).

## Implementation

A 320x240 RGB565 framebuffer is allocated in PSRAM and transferred as one SPI
operation to avoid tearing. Editable source artwork lives under `assets/`.
`tools/generate_eye_assets.py` converts those PNGs to efficient RGB565 and
threshold tables under ignored `generated/` files before each build. The PNGs,
not opaque C arrays, remain the source of truth.
