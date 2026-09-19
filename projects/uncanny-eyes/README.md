# Uncanny Eyes for the Hosyond ES3C28P

Animated eyes for the Hosyond ESP32-S3 2.8-inch touch display. The eyes roam,
blink, track touches, and cycle through eleven designs using Adafruit's original
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

## Wi-Fi setup and web controls

Wi-Fi credentials are provisioned at runtime and stored in the ESP32's NVS.
They are not compiled into the firmware or kept in this repository.

On first boot, the device creates an access point named `UncannyEyes-XXXXXX`.
Use PlatformIO's serial monitor to read its device-specific password and setup
address:

```sh
micromamba run -n platformio pio device monitor -d projects/uncanny-eyes
```

The setup SSID and password also remain visible on the display's second status
line while provisioning is active. Join that access point, open the displayed
address (normally `http://192.168.4.1/`), and submit the local Wi-Fi network
name and password. The device saves them, restarts, and removes the setup line.
Once connected, the serial monitor reports its network address. Hold
the BOOT button while resetting to reopen provisioning mode.

The web interface can select a style, move to the previous or next style,
mute/unmute audio, and enable automatic style cycling with a configurable
interval. It reports the active Manual/Cycle mode, current style, battery,
Wi-Fi state, and external-power state. External power is reported as `unknown`
until a board signal is verified with
[`../power-diagnostics/`](../power-diagnostics/).

Serial commands can be entered in the PlatformIO monitor:

```text
wifi status
wifi provision
wifi reconnect
wifi clear
confirm wifi clear
```

`wifi clear` deliberately requires the confirmation command. Provisioning
credentials are never printed or returned by the web API. NVS is persistent but
is not encrypted by default, so someone with physical flash access may still be
able to recover stored credentials. The HTTP interface is intended for a
trusted local network. When the touch controls are visible, a Wi-Fi icon appears
left of the battery percentage: green means connected, yellow means configured
but disconnected, and a red crossed icon means not yet provisioned. Tap the
Wi-Fi icon to toggle the device IP address on the second status line.

## Controls

- No input: autonomous gaze and blinking
- Touch/drag: both eyes follow the contact point (where supported by the style)
- Tap while controls are hidden: show controls and battery percentage
- Bottom-left `<` button: previous style
- Bottom-center FLIP button: rotate display and touch controls 180 degrees
- Bottom-right `>` button: next style
- Upper-left SOUND/MUTED button: toggle sound; gray NO SND indicates unavailable audio
- Press and hold without moving for 2 seconds: deep sleep
- BOOT button: wake from deep sleep

The available styles are listed below. A check mark indicates that the style
has a recorded sound. The original eye styles come from Adafruit Uncanny Eyes;
Big Anime is local artwork; and the remaining styles are adapted from Adafruit
Monster Eyes.

| Style | Origin | Sound |
|---|---|:---:|
| Hazel | Adafruit Uncanny Eyes | ✓ |
| Dragon | Adafruit Uncanny Eyes | ✓ |
| No Sclera | Adafruit Uncanny Eyes | ✓ |
| Goat/Krampus | Adafruit Uncanny Eyes | ✓ |
| Newt | Adafruit Uncanny Eyes | ✓ |
| Terminator | Adafruit Uncanny Eyes | ✓ |
| Cartoon Cat | Adafruit Uncanny Eyes | ✓ |
| Owl | Adafruit Uncanny Eyes + local artwork | ✓ |
| Nauga | Adafruit Uncanny Eyes | ✓ |
| Realistic Deer | Adafruit Uncanny Eyes + local artwork | ✓ |
| Big Anime | Local artwork | ✓ |
| Big Blue | Adafruit Monster Eyes |  |
| Demon | Adafruit Monster Eyes |  |
| Doom Red | Adafruit Monster Eyes |  |
| Doom Spiral | Adafruit Monster Eyes |  |
| Fish | Adafruit Monster Eyes |  |
| Fizzgig | Adafruit Monster Eyes |  |
| Hypno Red | Adafruit Monster Eyes |  |
| Reflection | Adafruit Monster Eyes |  |
| Skull | Adafruit Monster Eyes |  |
| Snake Green | Adafruit Monster Eyes |  |
| Spikes | Adafruit Monster Eyes |  |
| Toon Stripe | Adafruit Monster Eyes |  |

The styles with sound occasionally play a short recorded sample at a randomized
15–45 second interval. The added Monster Eyes styles currently display `NO SND`.
Samples and licensing details are documented in [`audio/README.md`](audio/README.md).

## Implementation

A 320x240 RGB565 framebuffer is allocated in PSRAM and transferred as one SPI
operation to avoid tearing. Editable source artwork lives under `assets/`.
`tools/generate_eye_assets.py` converts those PNGs to efficient RGB565 and
threshold tables under ignored `generated/` files before each build. The PNGs,
not opaque C arrays, remain the source of truth.

Unlike the original Adafruit Monster Eyes firmware, which loads `config.eye`
and bitmap assets from a FAT/CIRCUITPY filesystem at runtime, this project
compiles the generated artwork directly into the firmware. The Monster Eyes
textures were resized or projected for this renderer, and the existing smooth
eyelid maps are used for progressive blinking. No device filesystem setup is
required after uploading the firmware.
