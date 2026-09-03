# Space Battle Screensaver

A continuous fleet engagement for the Hosyond ESP32-S3 2.8" touch display: a
parallax starfield, a shaded planet, and two fleets that fight, take losses, and
receive reinforcements indefinitely.

The camera runs itself. It tracks the contested zone -- the ships actually within
weapons range of an enemy, rather than the midpoint between the two fleets, which
is usually empty space -- and cuts between a wide shot that frames the whole
engagement and a closer shot pushed into part of it.

## Controls

| Gesture | Effect |
|---|---|
| One finger, drag | Pan |
| Two fingers, pinch | Zoom (0.16x to 4.5x) |
| Two fingers, drag | Pan while zooming |
| No touch for 6 s | Automatic camera resumes |

## Build and flash

```
python3 -m venv .pio-venv && .pio-venv/bin/pip install platformio
.pio-venv/bin/pio run -t upload
```

The upload port is pinned by USB serial in `platformio.ini`, so it targets the
right board even though a second Espressif device is attached to this machine.

## Layout

| File | Contents |
|---|---|
| `src/main.cpp` | Setup and the frame loop |
| `src/battle.cpp` | Fleet AI, weapons, damage, reinforcements, camera targeting |
| `src/camera.cpp` | Pan, pinch-zoom, and the automatic shot framing |
| `src/renderer.cpp` | Starfield, planet, ships, shots, debris |
| `src/touch.cpp` | FT6336G driver |
| `include/config.h` | Screen, pin, and simulation constants |
| `DEVICE.md` | Board identity, verified pinout, and the build flags it needs |

## Performance

15-30 fps at 320x240, lowest when zoomed out with the planet filling the frame.
The frame is composed in a 150 KB PSRAM sprite and pushed in one SPI transfer.

Three constraints found by measurement, each with a comment at the relevant code:

- **SPI at 60 MHz.** At 80 MHz the panel drops bytes mid-frame and everything
  after the drop lands shifted, which looks like the whole screen jumping
  sideways. 40 MHz is stable but costs about a third of the framerate.
- **One transfer, not banded.** Splitting the push into bands with a yield
  between them tears: the frame is drawn once but reaches the panel in pieces
  separated in time, so moving sprites shear at the band boundaries.
- **Planet geometry stays fractional.** Rounding the centre or radius to whole
  pixels makes the disc step a pixel at a time as the zoom eases, which reads as
  the circumference pulsing. Terrain stability is handled instead by sampling the
  surface pattern on a fixed grid in planet space.

## Restoring the factory firmware

`factory_backup_16MB.bin` is a full dump of the flash as shipped. This board has
a single app partition and no OTA slot, so it is the only copy.

```
esptool --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:CE:4E:40-if00 \
        write-flash 0x0 factory_backup_16MB.bin
```
