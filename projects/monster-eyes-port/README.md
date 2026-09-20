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
- Extends the renderer with `slitPupilHorizontal`, laying the slit pupil across
  the eye rather than up it, and `slitPupilRounded`, which blunts its ends
  instead of bringing them to a point -- between them, the bar a deer, goat or
  horse has rather than the lens a cat has. The slit radius is measured along
  the slit whichever way it lies.
- Discovers package directories under `/eyes`; eye names and paths are not
  compiled into firmware.
- Resolves package-relative asset paths and reconstructs the renderer safely
  when switching packages.
- Tracks touch position. Tap once to reveal controls, then use previous, flip,
  and next along the bottom; tap above the controls to blink.
- Shows battery and Wi-Fi state in the controls and enters deep sleep after a
  stationary two-second press; only the BOOT button is configured to wake it.
  The Wi-Fi icon is a switch: tapping it turns the radio on or off, and the
  state is stored in NVS. While the radio is on and associated, the IP address
  stays on screen under the package name; a greyed icon with a slash means the
  radio is off. Style cycling and the serial console keep working with Wi-Fi
  off, and `wifi on` over serial brings it back.
- Provides runtime Wi-Fi provisioning, NVS credential storage, serial Wi-Fi
  commands, and a web UI for style selection, filtering, and persistent cycling.
- Persists the current package, per-package enabled state, and package order in
  NVS. Navigation skips disabled packages and prevents disabling the final
  enabled package. The web UI provides ordering controls.
- Accepts `previous`, `next`, and `wifi on` / `wifi off` commands over the
  serial console.
- Drives the ES8311 codec from a dedicated core-0 I2S task, discovers optional
  package WAV sounds, and persists mute state and output level. The overlay and
  web UI provide mute controls; the overlay uses a speaker icon with a slash
  only when muted. The web UI adds a volume slider covering -40 dB to 0 dB of
  ES8311 DAC gain, with 0% silent; the level is stored in NVS.

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

## Web interface

The web UI source is `web/index.html`, `web/style.css`, and `web/app.js` —
ordinary files that can be opened in a browser and edited normally. They are
not what ships: `tools/build_web.py` runs as a PlatformIO `pre:` script, inlines
the stylesheet and script into a single document, gzips it, and writes
`generated/web_page.h`, which the firmware serves from flash with
`Content-Encoding: gzip`. One document matters because the web server is polled
from the render loop, so extra asset requests would stall the eye animation.

To work on the layout without flashing anything, run

```sh
python projects/monster-eyes-port/tools/serve_web.py
```

and open `http://localhost:8000`. It serves `web/` alongside a fake device API.

The page has two tabs. Controls holds status, style selection, audio (mute,
play, and volume), screen brightness, cycling, and Wi-Fi setup. Packages holds
the package list and the upload form, so the default view stays short on a
phone.

Audio is controlled through `POST /api/audio/mute` (`muted`),
`POST /api/audio/volume` (`volume`, 0-100), and `POST /api/audio/play`, which
starts the next sound in the active package and steps a cursor so repeated
presses walk the whole package rather than replaying one file. Automatic
playback stays random. Play answers 409 when the codec is missing, audio is
muted, or the package has no sounds.

The status JSON reports `externalPower` as `usb` or `battery`. This board
brings no external-power signal out to a GPIO -- every unassigned pin holds the
same state with the cable in and out, which `projects/power-diagnostics`
measured -- so the value comes from the USB peripheral's own start-of-frame
detection. It therefore means "a USB host is connected", and a dumb wall
charger reads as `battery`.

Screen brightness is `POST /api/display/brightness` (`brightness`, 0-100),
reported back in the status JSON. The backlight is driven by an LEDC PWM
channel on `TFT_BL` and the level is stored in NVS, so it survives a reboot.
Values below 5% are clamped up: a screen dark enough to look broken would hide
the control that turns it back up. Deep sleep hands the pin back to plain GPIO
before driving it dark.

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

Two things about the Arduino filesystem API shape this code, and both were
found the hard way when the first upload of every session failed:

- `File::size()` cannot be trusted on a file that has just been opened for
  writing. The VFS only re-stats a file once something has been written to it,
  so until then the handle reports whatever uninitialised stat data it was
  built with. Staged bytes are counted as they arrive instead.
- `FFat.exists()` answers false for a directory, so it cannot be used to ask
  whether a package is present. Publishing, renaming and deleting a package all
  open the path and ask the handle whether it is a directory.

Staging directories are siblings of the live packages (`/eyes/.stage-<token>`,
with `/eyes/.backup-<token>` for the package being replaced) so that publishing
is a rename within one directory. The package scanner skips names beginning
with a dot, and any left over from an interrupted upload are removed at boot.

## The deer package

`data/eyes/deer` is the one migrated style whose artwork is generated rather
than carried over, because the original faked its horizontal pupil by painting
two black lobes into the bottom rows of the iris texture, at the angles left
and right of centre -- with a round pupil that is the only way to widen one
sideways. The renderer builds the slit itself now, so the texture is iris all
the way down and the fibres reach the pupil edge:

```sh
python projects/monster-eyes-port/tools/make_deer_eye.py
```

The pupil is a bar with blunt ends (`slitPupilHorizontal` and
`slitPupilRounded`), kept well short of the iris so it reads as a rounded
oval. Its height is not set directly: `pupilMax` picks which contour of the
morph from iris circle to bar the pupil edge lands on, so thinning the bar
means lowering it.

Colours are read off photographs of a sika doe and a red deer as a profile
down through the eye: a nearly black limbal ring, brown through the body of
the iris, and warmer tones below where the light falls. The pupil is
photographed as a dark blue-grey rather than black. Deer sclera is brown and
barely shows, so it is near black.

The eyelids are generated as well. The opening is an ellipse, which has a
vertical tangent at each corner, so the lids meet there roundly and the eye
keeps its width to the edge; the migrated pair tapered to a point instead.
Their closed edges are arcs rather than straight lines, because the renderer
interpolates lid shape between open and closed on every frame and a flat lid
closes like a shutter.

## The Eye of Sauron package

`data/eyes/sauron` is an original package rather than a migrated style. Its
artwork is generated too:

```sh
python projects/monster-eyes-port/tools/make_sauron_eye.py
```

The flames are drawn straight into the renderer's polar texture space, where
the horizontal axis is the angle around the eye and the vertical axis is the
distance in from the rim. Every term in the generator is a function of the
angle alone scaled by a function of the distance alone, so a tongue of flame
arrives on the screen pointing straight out from the pupil: nothing leans,
curls or spirals. `irisSpin` and `scleraSpin` are both zero, so the fire never
turns around the iris either -- it only reaches outward. The one movement it
has comes from the pupil: dilating it rescales the iris texture radially, and
the narrow `pupilMin`/`pupilMax` range makes that read as the fire surging in
and out.

Layout and palette were measured off the reference footage ring by ring and
sector by sector around the pupil: a white-hot collar on the pupil edge, the
fire at its hottest a little way out from it and spent by the rim, and the
flames to the left and right of the pupil burning far cooler than those above
and below it -- deep blood red against yellow-white. That cool wedge belongs
to the inner half of the fire in the footage, so the generator eases it off
again towards the tips, where the sideways flames are the ones that throw the
furthest. The sclera is a dim ember dying before the eyeball's rim, and
`backColor` is black, so the eye reads as fire floating in the dark.

The package carries no eyelid bitmaps at all. A missing lid loads as "fully
out of the way" for both its open and its closed position, so the blink timer
still runs but moves nothing: the Eye does not blink and no lid ever crosses
the fire.

The fire moves by `irisFlow` rather than by spinning, so the flames lick
outward and never travel around the iris. The iris is nearly the whole
eyeball, which leaves the sclera as no more than a dim ember at the rim.

Both this package and the renderer settings behind it were tuned against
screenshots pulled off the board with `GET /api/frame`, not against the
generator's own preview: at 128 px an eye loses detail the preview keeps, and
three things that looked right at 240 px -- the brightness of the middle, the
depth of the dark flanks and the raggedness of the flame tips -- did not
survive the trip.

## Radial flow

`irisFlow` animates an iris without turning it. The renderer already had
`irisSpin`, which rotates the texture, and for anything whose pattern means
something -- fire reaching outward, spokes, rays -- rotation is exactly the
wrong motion.

Flow instead samples each pixel a little nearer to or further from the pupil
than it actually sits, on a wave that travels out from the pupil. Heat drawn
at one depth in the texture shows up at another, so tongues of flame stretch
outward and sink back. Three keys control it:

- `irisFlow`: peak shift as a fraction of the iris depth. 0, the default,
  leaves the texture sampled where it sits.
- `irisFlowSpeed`: wave crests leaving the pupil per second.
- `irisFlowWaves`: crests between the pupil and the rim.

The eye is divided into 64 sectors, each with its own phase, so crests do not
arrive everywhere at once -- without that it ripples like a pond rather than
burning. The wave is a skewed sine that rises fast and falls back slowly,
because fire throws material out and lets it sink. The shift fades to nothing
at the pupil, so a hot collar around the pupil does not wobble, and the pupil
test uses the undisplaced row, so the pupil's own edge never moves.

Per pixel this is a table lookup, a multiply and a divide.

## Screenshots

`GET /api/frame` returns a screenshot of the panel as a 320x240 24-bit BMP, and
the web interface shows it under a Screen tab with a capture button, a live
mode and a download link.

The panel cannot be read back: MISO is not dependable on this board and an
ILI9341 returns mangled 18-bit data anyway. Instead the display backend keeps a
mirror of the panel in PSRAM, and the request arms it, draws one frame, and
serves the mirror. The render loop is the only writer and the request runs on
that same task, so there is nothing to synchronise and nothing to copy on a
frame nobody asked about.

What it captures is the two eyes and the background they sit on. The status
icons and the touch controls are drawn straight to the TFT by the sketch, so
they never pass through the backend and are not in the picture.

## Linux preview

`sim/` builds `eye-sim`, a desktop preview that runs this renderer against a
simulated 320x240 panel. The library sources are compiled from `lib/`
unmodified against a small Arduino shim, so what it shows is what the device
would draw; only the display backend is different. It also captures frame
sequences as PNGs with JSON state sidecars, for analysis without hardware.

```sh
cmake -B projects/monster-eyes-port/sim/build projects/monster-eyes-port/sim
cmake --build projects/monster-eyes-port/sim/build -j
cd projects/monster-eyes-port && ./sim/build/eye-sim --eye deer
```

Needs `cmake`, `g++`, `zlib1g-dev` and, for the window, `libsdl3-dev`. See
`sim/README.md` for the keys, the capture options and what the shim covers.

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
