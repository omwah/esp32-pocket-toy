# External-power diagnostics

Temporary firmware for finding out whether this board exposes USB or
external-power status on a GPIO, so `monster-eyes-port` can report something
better than `externalPower: "unknown"`.

## Why v1 found nothing

The first version streamed pin states over USB serial and left the correlation
to whoever read the log. That cannot work. The cable being pulled *is* the
serial link, so the one condition worth measuring is the one condition with no
way to report itself. Its notes record a single USB-connected baseline and
nothing else.

It also read candidate pins as plain floating inputs. A floating input reads as
whatever the last charge on it happened to be, so a stable-looking value proves
nothing.

## How v2 works

The panel runs the test and prompts for each step, because the screen still
works when the cable does not.

1. **Ready** — checks a battery is connected and reading above 3.3 V. Without
   one, pulling USB just switches the board off.
2. **Baseline** — 8 s with USB connected.
3. **UNPLUG USB NOW** — waits for the cable to actually come out.
4. **On battery** — 15 s counting down, with a "do not plug in yet" reminder.
5. **PLUG USB BACK IN** — waits for the cable to return.

Steps 2-5 run twice. A pin that follows the cable once could be coincidence;
twice is a signal. The verdict then appears on screen and the full log is sent
over serial.

Each candidate pin is read three ways per sample: pulled up, pulled down, and
floating. A pin something actually drives holds its level against both pulls; an
unconnected one just follows whichever pull is on. That distinction is what v1
was missing.

Candidate pins are `2, 3, 6, 14, 21, 38, 39, 40, 41, 42, 47, 48` — v1 tested
only the first five of those. Flash, PSRAM, USB, display, touch, audio, the
battery divider, BOOT and UART0 are excluded. Ground truth for "USB present"
comes from `HWCDC::isPlugged()`, which follows USB start-of-frame packets from
a host.

## Running it

```sh
micromamba run -n platformio pio run -d projects/power-diagnostics -t upload
```

Follow the prompts on the device. Then collect the log:

```sh
micromamba run -n platformio python projects/power-diagnostics/tools/power_dump.py
```

That prints the device's own verdict, a per-pin summary computed independently
on the host, and writes the raw CSV to `last_run.csv`.

Serial commands while the firmware is running: `dump` replays the log, `restart`
clears it and starts the sequence again.

## Reading the result

A pin that reports `TRACKS USB POWER` was stable and different in the two
conditions across both passes — that is the signal to wire into
`monster-eyes-port`. `unstable` means it moved around within one condition, so
it is noise. `no change` means it is not it.

If nothing tracks the cable, the board most likely does not bring an
external-power signal out to a free pin, and the battery divider reading is the
only thing that shifts — charging pins it near the charger's output voltage.
That is a crude indicator, not a clean one.

The summary also reports whether the board reset during a run. If it did, the
board cannot ride out losing USB, which is itself worth knowing.

## Result, 2026-09-19

No pin tracks external power. Every candidate held the same state with the
cable in and out, across both passes:

| state | pins |
| --- | --- |
| driven high | 38, 39, 40, 41, 47, 48 |
| driven low | 6 |
| floating | 2, 3, 14, 21, 42 |

Those twelve are everything this board leaves unassigned, so the conclusion is
that the hardware does not bring an external-power signal out to a GPIO at all.

The one quantity that does follow the cable is the battery divider on GPIO 9:
about **4106 mV with USB connected, 4047 mV on battery**, a repeatable step of
roughly 60 mV with a full battery. On reconnection the divider moved a sample
*before* `HWCDC::isPlugged()` did, since that detector has to wait for USB
enumeration. The step size depends on charge state, so a fixed threshold is no
good; a jump detector would work, and would still miss a charger connected
while the battery is already full and the charger has stopped pushing current.

The practical answer for `monster-eyes-port` is therefore `HWCDC::isPlugged()`
in firmware — no extra hardware, immediately available, and honest about what
it measures: a USB *host* is connected. A dumb wall charger sends no
start-of-frame packets and would read as no external power.

The raw log of the run is in `last_run.csv`.

## Afterwards

This firmware replaces whatever is on the board. Restore the application with:

```sh
micromamba run -n platformio pio run -d projects/monster-eyes-port -t upload
```

A preliminary v1 run found I²C devices only at `0x18` (ES8311) and `0x38`
(FT6336G), so there is no separate I²C charger to interrogate.
