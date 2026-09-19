# ESP32 Display Projects

Projects for the Hosyond ESP32-S3 2.8" touch display hardware.

## Projects

- [`projects/space-battle/`](projects/space-battle/) — continuous fleet-engagement screensaver
- [`projects/uncanny-eyes/`](projects/uncanny-eyes/) — asset-based animated eyes with touch and Wi-Fi controls
- [`projects/power-diagnostics/`](projects/power-diagnostics/) — temporary external-power signal probe

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

Shared board documentation is in [`HARDWARE.md`](HARDWARE.md). The factory firmware backup is in
[`hardware/hosyond-es3c28p/`](hardware/hosyond-es3c28p/).
