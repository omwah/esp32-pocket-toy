# esp32-pocket-toy

Projects for the Hosyond ESP32-S3 2.8" touch display hardware: a
battery-powered pocket toy that runs animated eyes, a screensaver, and
diagnostics.

## Projects

- [`projects/space-battle/`](projects/space-battle/) — continuous fleet-engagement screensaver
- [`projects/uncanny-eyes/`](projects/uncanny-eyes/) — asset-based animated eyes with touch and Wi-Fi controls
- [`projects/power-diagnostics/`](projects/power-diagnostics/) — temporary external-power signal probe
- [`projects/usb-hid-tester/`](projects/usb-hid-tester/) — USB keyboard tester with an impatient hippo image
- [`projects/monster-eyes-port/`](projects/monster-eyes-port/) — animated eyes: twenty-six swappable eye packages, touch and web control, desktop preview

## Hardware bought for this project

- **Display board** — [Hosyond ESP32-S3 Touchscreen Module, 2.8" 240x320 IPS LCD with
  Wi-Fi, Bluetooth and capacitive touch](https://www.amazon.com/dp/B0FKG7WRWV)
  (ASIN `B0FKG7WRWV`). The underlying board is the ES3C28P. Pinout, chip details and
  the quirks found on it are in [`HARDWARE.md`](HARDWARE.md).
- **Battery** — [JLJLUP 3.7 V 3000 mAh lithium-polymer cell](https://www.amazon.com/dp/B0FR8XSXX6)
  (ASIN `B0FR8XSXX6`). Single cell, JST 1.25 mm connector, with an integrated
  protection circuit.
- **Case** — [TRex Talk assistive communication device, CYD version](https://www.printables.com/model/1660846-trex-talk-assistive-communication-device-cyd-versi),
  a printable enclosure for 2.8" "Cheap Yellow Display" style ESP32 boards.

## Development environment

The environment definition is [`environment.yml`](environment.yml). The filename
`environment.yml` is the conventional spelling for a Conda or micromamba
environment definition. Create it once with:

```sh
micromamba create -f environment.yml
```

The environment is named `platformio`. To update an existing checkout after the
dependencies change, use:

```sh
micromamba install -n platformio -f environment.yml
```

Build a project from the repository root, for example:

```sh
micromamba run -n platformio pio run -d projects/space-battle -t upload
```

Shared board documentation is in [`HARDWARE.md`](HARDWARE.md), including how to
back up the stock firmware before the first write. The backup itself is not in
this repository -- it is a 16 MB flash dump of one particular board, and every
board needs its own.
