# External-power diagnostics

Temporary read-only firmware for identifying whether the ES3C28P exposes USB or
external-power status through a GPIO or an I²C power-management device. It does
not drive candidate GPIOs or enable their pull resistors.

Build, upload, and monitor:

```sh
micromamba run -n platformio pio run -d projects/power-diagnostics -t upload
micromamba run -n platformio pio device monitor -d projects/power-diagnostics
```

Record output under each condition:

1. Battery only
2. USB only, with the battery disconnected
3. Battery and USB together
4. Neither source, if a bench supply makes that test practical

Repeat cable insertion several times and test while the battery is both charging
and full. A valid external-power indication must follow USB/external power, not
battery voltage or charge state. Candidate pins remain high-impedance inputs.
GPIOs assigned to flash/PSRAM, USB, SD, display, touch, audio, BOOT, and known
strapping functions are intentionally excluded.

A preliminary USB-connected run found I²C devices only at `0x18` (ES8311) and
`0x38` (FT6336G), so no separate I²C charger was detected. Its stable baseline
was GPIO 2/3/6/14/21 low, GPIO 47/48 high, and approximately 4.086 V on the
battery divider. This single condition does **not** identify an external-power
signal; the disconnected test conditions above still require physical cable and
battery changes.

The production application reports external power as `unknown` until a signal
is verified. Restore Uncanny Eyes afterward with:

```sh
micromamba run -n platformio pio run -d projects/uncanny-eyes -t upload
```
