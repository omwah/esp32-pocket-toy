# Monster Eyes renderer migration

Experimental migration of `uncanny-eyes` to Adafruit's Monster Eyes renderer.
It is kept separate until the renderer, runtime style loading, touch, audio,
Wi-Fi, and overlays reach feature parity with the production application.

## Current milestone

- Uses the upstream Monster Eyes animation and polar-map renderer.
- Implements a `TFT_eSPI` stripe backend for two 128x128 eyes on one 320x240
  ILI9341 panel.
- Mounts the existing PlatformIO FATFS partition through `FFat`.
- Reads and applies package `config.eye` files through the Monster Eyes JSON parser.
- Loads standard 24-bit texture BMPs and 1-bit eyelid BMPs from FATFS.
- Includes all 14 upstream M4 EYES packages and scales their geometry at runtime.
- Adds directly installable packages for all ten production Uncanny Eyes styles,
  generated from their editable artwork and including package-local sounds.
- Discovers package directories under `/eyes`; eye names and paths are not
  compiled into firmware.
- Resolves package-relative asset paths and reconstructs the renderer safely
  when switching packages.
- Tracks touch position. Tap once to reveal controls, then use previous, flip,
  and next along the bottom; tap above the controls to blink.
- Shows battery and Wi-Fi state in the controls and enters deep sleep after a
  stationary two-second press; only the BOOT button is configured to wake it.
- Provides runtime Wi-Fi provisioning, NVS credential storage, serial Wi-Fi
  commands, and a web UI for style selection, filtering, and persistent cycling.
- Persists the current package, per-package enabled state, and package order in
  NVS. Navigation skips disabled packages and prevents disabling the final
  enabled package. The web UI provides ordering controls.
- Accepts `previous` and `next` commands over the serial console.
- Drives the ES8311 codec from a dedicated core-0 I2S task, discovers optional
  package WAV sounds, and persists mute state. The overlay and web UI provide
  mute controls; the overlay uses a speaker icon with a slash only when muted.

Optional package sounds are declared relative to `config.eye`:

```json
"extensions": {
  "audio": {
    "sounds": ["growl.wav", "bark.wav"],
    "minInterval": 12000,
    "maxInterval": 30000
  }
}
```

PCM WAV files may be mono or stereo, 8-bit or 16-bit. Playback uses the sample
rate stored in each WAV file. Packages without this extension remain silent.

## Web package management

The web UI can upload or atomically replace packages, reorder them, download
individual package files, rename inactive packages, and delete inactive
packages. Uploads are written to `/eyes/.staging`, validated, and renamed into
place only after the complete package passes JSON, BMP, and WAV validation.
Interrupted staging data is removed at boot. Package IDs are limited to letters,
numbers, underscores, and hyphens; the active and final package cannot be
deleted.

The corresponding local-network API endpoints are:

- `POST /api/packages/order`
- `POST /api/packages/upload/start`
- `POST /api/packages/upload/file?token=...`
- `POST /api/packages/upload/commit`
- `GET /api/packages/download?id=...&path=...`
- `POST /api/packages/rename`
- `POST /api/packages/delete`

Package files are limited to 1 MiB each and 3 MiB per staged package. Uploads
accept `config.eye`, 24-bit uncompressed texture BMPs, 1-bit uncompressed eyelid
BMPs, and PCM WAV files.

## Migration validation

`validation/migrated-styles.json` records the accepted result and intentional
renderer differences for all 23 production styles. Validate the complete
manifest and every referenced package asset with:

```sh
python projects/monster-eyes-port/tools/validate_migration.py
```

The vendored Monster Eyes core is based on Adafruit Monster Eyes 1.0.0 commit
`adc06f7` and retains its MIT license. Display backends unrelated to this board
were removed. `Eyes_Assets.cpp` was adapted to use ESP32 `FFat`; USB mass-storage
mode is disabled because assets will ultimately be managed by the web UI.

## Build and flash

```sh
micromamba run -n platformio pio run -d projects/monster-eyes-port -t uploadfs
micromamba run -n platformio pio run -d projects/monster-eyes-port -t upload
```

To restore the production application and its asset filesystem:

```sh
micromamba run -n platformio pio run -d projects/uncanny-eyes -t uploadfs
micromamba run -n platformio pio run -d projects/uncanny-eyes -t upload
```

## Migration status

The renderer migration work is complete. The experimental project retains its
own deployment target until final on-device acceptance, after which it can
replace the production application without changing the package filesystem.
