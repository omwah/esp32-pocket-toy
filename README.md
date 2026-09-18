# ESP32 Display Projects

Projects for the Hosyond ESP32-S3 2.8" touch display hardware.

## Projects

- [`projects/space-battle/`](projects/space-battle/) — continuous fleet-engagement screensaver
- [`projects/uncanny-eyes/`](projects/uncanny-eyes/) — asset-based animated eyes with touch tracking

PlatformIO is installed in the `platformio` micromamba environment.
Build a project from the repository root, for example:

```sh
micromamba run -n platformio pio run -d projects/space-battle -t upload
```

Shared board documentation is in [`HARDWARE.md`](HARDWARE.md). The factory firmware backup is in
[`hardware/hosyond-es3c28p/`](hardware/hosyond-es3c28p/).
