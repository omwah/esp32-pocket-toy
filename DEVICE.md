# Target Device — Hosyond ESP32-S3 2.8" 240x320 IPS Touchscreen (ES3C28P)

This is the board this project targets. A second Espressif board (an ESP32-C3) is
attached to the same machine and shares the same USB VID:PID, so **always address
the target by its stable serial-derived path, never by `/dev/ttyACM*`** — the ACM
numbering is assigned in enumeration order and swaps between reboots and replugs.

## Board identity

Sold as the **Hosyond ESP32-S3 Touchscreen Module, 2.8" 240x320 IPS LCD with WiFi
Bluetooth Capacitive Touch** (Amazon ASIN `B0FKG7WRWV`). The underlying board is the
**ES3C28P** ("2.8inch ESP32-S3 Display"), documented on LCD Wiki. `ES3N28P` is the
same board without the touch panel.

- Product page: <https://www.amazon.com/dp/B0FKG7WRWV>
- Vendor documentation: <https://www.lcdwiki.com/2.8inch_ESP32-S3_Display>
- Working ESPHome config for this exact board: <https://github.com/celer/esphome_esp32-s3-2.8-display>
- Board support package: <https://github.com/ngttai/esp32_s3_es3c28p>

The module is an ESP32-S3-WROOM-1 **N16R8** — 16 MB flash, 8 MB PSRAM. Vendor
documentation states the PSRAM is **OPI (octal)**, which resolves the open question
the eFuse read could not answer.

## Canonical port

```
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:CE:4E:40-if00
```

At the time of writing this resolves to `/dev/ttyACM1`.

## USB identity

| Attribute | Value |
|---|---|
| Vendor ID | `303a` (Espressif Systems) |
| Product ID | `1001` (USB JTAG/serial debug unit) |
| USB serial | `44:1B:F6:CE:4E:40` |
| Manufacturer string | `Espressif` |
| Product string | `USB JTAG/serial debug unit` |
| bcdDevice | `0101` |
| USB version / speed | 2.00 / 12 Mbit/s (full speed) |
| Interface | `if00`, CDC-ACM |
| Sysfs node | `/sys/bus/usb/devices/1-1` |
| Physical port path | `pci-0000:00:14.0-usb-0:1:1.0` (root hub port 1, direct — not behind a hub) |

The USB serial is the chip's base MAC address, so it is unique and permanent for
this board.

## Chip

| Attribute | Value |
|---|---|
| Chip | ESP32-S3 (QFN56), revision v0.2 |
| Cores | Dual core + LP core, 240 MHz |
| Radios | Wi-Fi, Bluetooth 5 (LE) |
| PSRAM | 8 MB embedded, vendor `AP_3v3` (AP Memory, 3.3 V), 85 °C grade — per eFuse `PSRAM_CAP`/`PSRAM_VENDOR` |
| Flash | 16 MB external, manufacturer `0x5e`, device `0x4018` |
| Flash mode per eFuse | quad (4 data lines), 3.3 V |
| Crystal | 40 MHz |
| PSRAM bus mode | **Octal (OPI)** per vendor documentation for the N16R8 module. The eFuse does not encode bus mode, so this comes from the board's published specification rather than from the chip. Build with octal PSRAM enabled. |
| USB mode | USB-Serial/JTAG (native peripheral, no external UART bridge) |
| Base MAC | `44:1b:f6:ce:4e:40` |

## Flash layout as found

The board shipped with firmware already on it. The partition table is an
Arduino-style single-OTA-slot layout sized for 4 MB, leaving the upper 12 MB of the
16 MB flash unmapped.

```
nvs        type=0x01 sub=0x02  off=0x009000    20K
otadata    type=0x01 sub=0x00  off=0x00e000     8K
app0       type=0x00 sub=0x10  off=0x010000  3072K
spiffs     type=0x01 sub=0x82  off=0x310000   896K
coredump   type=0x01 sub=0x03  off=0x3f0000    64K
```

There is no `app1` partition, so there is no OTA slot and no second copy of the
firmware — overwriting `app0` would destroy the factory firmware with no recovery
path.

**A full backup has been taken.** The entire 16 MB flash was dumped before any write:

```
factory_backup_16MB.bin   16777216 bytes
md5  e39630b4ac323ca3b42ad4f5e0b0cd06
```

Restore with:

```
esptool --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_44:1B:F6:CE:4E:40-if00 \
        write-flash 0x0 factory_backup_16MB.bin
```

87.5% of the flash reads as erased (`0xff`), consistent with a 4 MB partition layout
on a 16 MB part. Do not delete this file — it is the only copy of the stock firmware.

### Stock firmware provenance

The app descriptor at `0x10020` identifies the factory image:

```
project_name : arduino-lib-builder
version      : bb76cb1
built        : Mar 28 2025 06:13:03
idf_ver      : v5.4.1-1-g2f7dcd862a-dirty
```

This confirms the earlier inference from the partition names: the stock firmware is an
Arduino-framework build (ESP-IDF v5.4.1 underneath).

## Serial console

Resetting the board produces the ROM log below and nothing further on the USB CDC
port at 115200 baud:

```
ESP-ROM:esp32s3-20210327
rst:0x15 (USB_UART_CHIP_RESET),boot:0xa (SPI_FAST_FLASH_BOOT)
load:0x3fce2820,len:0x118c   load:0x403c8700,len:0xc24   load:0x403cb700,len:0x30e0
entry 0x403c88b8
```

Note what this does and does not prove. `0x403c88b8` is in the S3's IRAM range, not
the flash-mapped app IROM range (`0x42000000`+), so it is the entry point of the
*second-stage bootloader*, not of the application. The log therefore confirms only
the ROM → bootloader handoff. No `I (xx) boot:` bootloader messages and no
application output ever appear over USB. The evidence that the application actually
runs is separate and physical: the display reacts to reset (see below).

The application does not claim the USB CDC interface. While the app was running,
`/sys/bus/usb/devices/1-1/product` still read `USB JTAG/serial debug unit` — the ROM
peripheral's own descriptors, not a TinyUSB stack installed by firmware. This is
explained by the Arduino build (now confirmed from the app descriptor above) having
"USB CDC On Boot" disabled, which routes `Serial` to UART0 on GPIO43 (TX) / GPIO44
(RX) instead of to USB.

For our own firmware, either enable USB CDC on boot or attach a UART adapter to
GPIO43/44 to see application logs.

## Display

**Controller: ILI9341V**, 240x320 IPS, driven over SPI. The panel does not enumerate
over USB, so it was confirmed to belong to this board empirically — two reset pulses
five seconds apart, and the panel reacted to both. That is also the only evidence the
stock application runs at all, since it emits nothing on USB serial.

Pin assignments below are from the vendor documentation and have been **verified on
this physical board** — a test build drew correctly on every one of them.

| Signal | GPIO |
|---|---|
| SCLK | 12 |
| MOSI | 11 |
| MISO | 13 |
| CS | 10 |
| DC | 46 |
| RST | tied to the ESP32-S3 reset line — no separate GPIO |
| Backlight | 45 |

Note the display RST: there is no independent reset GPIO, so a TFT library must be
configured with `TFT_RST = -1` (or the equivalent) and must rely on the software
reset command instead. Configuring a real pin here drives an unrelated GPIO.

**The panel is inverted.** This IPS panel renders every colour as its exact
complement unless inversion is enabled — a black background comes up white and green
text comes up purple. Set `TFT_INVERSION_ON`. This is not optional and is the first
thing to check if colours look wrong.

`setRotation(1)` gives **landscape 320x240** with the origin at the top-left, verified
by corner markers. This is the orientation chosen for this project.

## Touch

**Controller: FT6336G**, capacitive, on I2C. Capacitive was confirmed by the user
from feel (light fingertip on bare glass, no pressure needed). The FT6336G reports up
to two simultaneous touch points, so **pinch-to-zoom is available** — this is what
makes the zoom-and-pan camera control feasible without a gesture workaround.

| Signal | GPIO |
|---|---|
| SDA | 16 |
| SCL | 15 |
| INT | 17 |
| RST | 18 |

The FT6336G answers at I2C address **0x38** (vendor `0x11`, chip `0x64`, firmware
`0x02`). Note there is a **second I2C device at 0x18** on the same bus — an audio
codec, not the touch controller. Code that takes the first address found by a bus
scan will bind to the wrong chip and read zeroes; address 0x38 explicitly.

The controller reports its coordinates in the panel's native **portrait** frame
(x in 0..239, y in 0..319) regardless of the display rotation set in software. For
`setRotation(1)` landscape, the mapping verified by corner taps is:

```
screen_x = raw_y
screen_y = 240 - raw_x
```

Touch data is read from register `0x02`: one status byte whose low nibble is the
number of active points, then six bytes per point (the 12-bit X and Y are split
across the first four of them). A hardware reset via GPIO18 (low 10 ms, then high,
then wait ~300 ms) is needed before the first read.

## Other peripherals

Present on the board, unused by this project so far, but worth knowing before
assigning any pin:

| Function | GPIO |
|---|---|
| SD card CLK / CMD | 38 / 40 |
| SD card DATA0-3 | 39 / 41 / 48 / 47 |
| I2S MCLK / BCLK / LRCK | 4 / 5 / 7 |
| I2S DOUT / DIN | 8 / 6 |
| Speaker enable | 1 |
| Battery ADC | 9 |
| UART0 TX / RX | 43 / 44 |
| BOOT button | 0 |

## Tooling

`esptool` v5.4.0 is installed in a throwaway virtualenv at `/tmp/esp_venv`
(`/tmp/esp_venv/bin/esptool`). This does not survive a reboot — reinstall with
`python3 -m venv <path> && <path>/bin/pip install esptool`, or install it
permanently once the build toolchain for this project is chosen.

The user is in the `dialout` group, so no `sudo` is needed for port access.

Two operational notes learned the hard way:

- The board **re-enumerates on reset**, which invalidates an open file handle to the
  port. A long `read-flash` that ends with the default reset behaviour can drop the
  port mid-operation. Pass `--after no-reset` for long reads.
- Only one process may hold the port. A second esptool against the same port fails
  with `[Errno 11] Could not exclusively lock port`.

## Do not confuse with

The other attached Espressif board, which must never be flashed by this project:

| Attribute | Value |
|---|---|
| Chip | ESP32-C3 (QFN32), revision v0.4, single core 160 MHz |
| USB serial / MAC | `D8:3B:DA:1D:13:30` |
| Flash | 4 MB embedded (XMC) |
| by-id path | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D8:3B:DA:1D:13:30-if00` |
| Currently | `/dev/ttyACM0`, sysfs `1-4.4.1.3`, behind a chain of hubs |

## Build configuration

PlatformIO, Arduino framework, in a project-local virtualenv at `.pio-venv`
(`.pio-venv/bin/pio`). Espressif32 platform 7.1.0, Arduino core 2.x.

Four settings were needed to get this board working, each fixing a real failure:

- **`board_build.arduino.memory_type = dio_opi`** — the PSRAM is octal. Without this
  the ROM probes it in quad mode and fails at boot with
  `E psram: PSRAM ID read error: 0x00ffffff, PSRAM chip not found or not supported,
  or wrong PSRAM line mode`, and `ESP.getFreePsram()` returns 0. With it, 8386295
  bytes of PSRAM are available.

- **`-D USE_HSPI_PORT=1`** — TFT_eSPI's default SPI port selection for the S3 is
  broken with this Arduino core. The library sets `SPI_PORT` to `FSPI`, but the core
  defines `FSPI` as 0 on the S3, while the SDK's `REG_SPI_BASE(i)` returns 0 for any
  `i < 2`. `SET_BUS_WRITE_MODE` therefore writes to address `0x10` and the firmware
  panics with `StoreProhibited` inside `TFT_eSPI::begin_tft_write()` on the very
  first `tft.init()`. `USE_HSPI_PORT` selects SPI3, which is a valid base address.

- **`-D TFT_INVERSION_ON=1`** — see the display section above.

- **`-D SPI_FREQUENCY=60000000`** — see "SPI clock ceiling" below. 80 MHz corrupts
  the frame transfer; 40 MHz is stable but costs roughly a third of the framerate.

Do not pass `-mfix-esp32-psram-cache-issue`; that flag is for the original ESP32
silicon and is not accepted for the S3.

## Lessons learned

Everything below was found by measurement on this board, not from documentation.
Each entry records the symptom first, because the symptom is what a future reader
will actually have in hand.

### SPI clock ceiling: 60 MHz

**Symptom:** the entire screen appears to jump sideways, intermittently, most
noticeably when a large object such as the planet is on screen.

**Cause:** at `SPI_FREQUENCY=80000000` the ILI9341V does not reliably latch every
byte of the frame transfer. A dropped byte shifts the pixel stream, so every pixel
after the drop lands one position early and the whole remainder of the frame is
displaced horizontally.

The important part of the diagnosis: a *whole-screen* horizontal displacement is a
transport fault, not a drawing fault. Drawing bugs move individual objects. If
everything moves together, suspect the link before auditing the renderer — the
first instinct here was to look at the planet's geometry code, which cost time.

60 MHz is stable and was measured at 18-30 fps. 40 MHz is also stable but drops to
15-22 fps. The vendor's own examples use 40 MHz, so 60 MHz is above the documented
rate and should be re-tested if the panel is ever swapped.

### Push the frame in one transfer, not in bands

**Symptom:** visible tearing, sprites shearing horizontally partway down the screen.

The first version of the renderer split the 150 KB sprite push into 30-row bands
with a `yield()` between them, to avoid starving the scheduler. That is what caused
the tearing: the frame is composed once, but reaches the panel in pieces separated
in time, so anything moving is drawn at a different position in each band.

At 60 MHz a full 320x240x16bpp frame transfers in roughly 20 ms, which is inside
both the task and interrupt watchdogs, so a single `pushSprite` is safe. Note that
this only became true after the clock was raised — the banding was originally added
because at 40 MHz with a slower renderer the blocking transfer *did* trip the
interrupt watchdog:

```
Guru Meditation Error: Core 0 panic'ed (Interrupt wdt timeout on CPU0)
```

If the frame time ever grows again, prefer DMA over re-introducing bands.

### Keep rendered geometry fractional

**Symptom:** the planet's circumference visibly pulses — growing and shrinking by a
pixel — while the camera zooms smoothly. Worst when zoomed out.

**Cause:** rounding the radius (or the centre) to whole pixels. As the zoom eases
continuously, a rounded radius steps in whole-pixel increments, and each step is a
visible change in the size of the disc. Zoomed out the effect is worse because one
pixel is a larger fraction of the total radius.

The fix has two halves, and both are needed:

1. Keep the centre and radius as floats, and antialias the limb by computing how
   much of each boundary pixel the disc actually covers. The edge can then move in
   fractions of a pixel.
2. Quantise the *terrain noise* sampling to a fixed grid in planet space. This is
   what the pixel-snapping was originally there to fix: a high-frequency surface
   pattern evaluated at whatever fractional offset each pixel lands on will crawl
   and shimmer as the disc drifts across the screen.

Snapping geometry to fix a sampling problem trades a shimmer for a pulse. Sample
stability and geometric smoothness are separate concerns and want separate fixes.

### Camera framing: track the contested zone

**Symptom:** the fleets drift out of frame and the screen shows empty starfield.

Two distinct mistakes here. First, a fixed default zoom of 1.0 shows a 320x240
window onto a 2400x1800 world — roughly 2% of it — so the camera could never frame
the battle no matter where it pointed. Any autonomous camera needs a zoom derived
from the extent of what it is framing, not a constant.

Second, the obvious target — the centre of mass of all ships — is usually the wrong
one. It sits in the gap *between* two fleets, which is empty space. Weighting each
ship by how close its nearest enemy is puts the camera on the ships that are
actually fighting. The same weights drive a weighted standard deviation, which
gives the zoom something to fit. A standard deviation is preferable to a bounding
box because one ship fleeing toward the map edge should not pull the whole view out.

### Verify pins with a shape, not a colour

The first smoke test drew colour bars, corner dots, and text. That combination
immediately identified an inverted panel: the user reported black-on-white with
every colour reading as its exact complement, which is unambiguous — a wiring or
controller mismatch produces noise or nothing, not a perfect inversion.

Corner markers of *different colours* in *known corners* also settle rotation and
mirroring in a single glance. Both were worth the few minutes to write.

### Silence on USB serial is not evidence of a dead board

The stock firmware boots and runs but prints nothing over USB, because it was built
with USB CDC on boot disabled (see the serial console section). The only way to
confirm it was running was physical: reset the board twice, spaced apart, and have
the user confirm the panel reacted both times.

When the display is the only output, `rst:` reason codes in the ROM log plus a
human watching the screen are the debugging channel.

### Take the flash backup before the first write

This board has a single `app0` partition and no OTA slot, so the factory firmware
has no second copy. The full 16 MB dump was taken before anything was flashed, and
it is the only copy that will ever exist. Two practical notes for repeating it:

- Pass `--after no-reset`. The board re-enumerates on reset, which invalidates the
  open port handle; a long read that ends in the default reset can lose the port
  mid-operation and produce no file.
- The dump takes about two minutes. Run it in the background, but verify the output
  file exists and has the expected size afterwards — the first attempt failed
  silently and left no file at all.

### A backdrop must translate, never scale

**Symptom:** the starfield swims — stars visibly drift and re-space themselves
relative to each other, reading as floating particles rather than as a distant
backdrop. Present even when the camera appears to be sitting still.

**Cause:** the stars were projected through the same `toScreen` transform as ships
and shots, so their on-screen spacing scaled with the zoom. The automatic camera
eases its zoom continuously, including a slow breathing term that never settles, so
the field was permanently rescaling by small amounts.

Stars belong at effectively infinite distance. They now live in a screen-sized tile
and are translated by the camera position alone, with a small parallax factor per
depth layer and a wrap at the screen edge. Nothing about them depends on zoom.

The general rule: anything meant to read as *far away* must not share the world
projection. Scaling is what tells the eye an object has a position in the scene.

### Compute tint by scaling brightness, not by absolute values

**Symptom:** a scattering of saturated red and blue dots sitting at full brightness
against an otherwise dim starfield, easily mistaken for lingering particles.

**Cause:** the tinted-star colours set their dominant channel to an absolute value,
`255 - v * 0.2`. With `v` in the intended dim range of 26-68 that evaluates to
241-250 — nearly maximum. Worse, the expression is inverted: *lowering* the overall
brightness `v` made those stars brighter, so the earlier change that dimmed the
field made these particular stars stand out more.

Tints should scale a value's own brightness (`rgb(v * 0.72f, v * 0.80f, v)`), so
brightness and hue stay independent and a later brightness change behaves as
expected.

### Particle lifetime is set by event rate, not by how it looks in isolation

**Symptom:** the screen accumulates a drifting haze of dim dots that outlasts the
events producing them.

A 0.4-1.3 s debris life looks fine for a single explosion. With ~200 ships firing
every 1-4 s, and a spark burst on every non-lethal hit, that is hundreds of live
particles at any moment. Three changes fixed it:

- Cut lifetimes to 0.10-0.28 s for explosion debris and 0.035-0.075 s for muzzle
  flash.
- Fade on a steep curve (fifth power) and shrink the particle quadratically, so it
  is small and nearly black for most of its life rather than a lingering dim dot.
- Remove the per-hit spark entirely. Only ship deaths spawn debris now; the tracer
  disappearing already reads as a hit.

Budget particles against the rate at which they are spawned, not against how a
single one looks.

### Silhouette, not colour, distinguishes ship classes

The first ships were all the same triangle, scaled by class and tinted by fleet.
They were unreadable in a crowd. Giving each class its own outline — swept dart,
prow-and-sponsons, long slab hull with lit windows — plus a lit side and a shadowed
side made classes identifiable at a glance.

Below about three pixels the silhouette is dropped for a marker whose pixel count
encodes the class. That is also what keeps the wide shots affordable: the expensive
per-class geometry only runs when it is large enough to see.
