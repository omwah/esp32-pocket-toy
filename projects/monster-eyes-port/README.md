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
- Extends the renderer with `roll`, turning the whole eyeball about its own
  optic axis: pupil, iris and sclera together, with the eyelids left where they
  are, because in life the globe rotates inside them. The two eyes take
  opposite angles, which is what a grazing animal does as its head goes down --
  a goat counter-rotates each eye by 50 degrees or more to keep its slit pupil
  level with the horizon. The map is sampled through the rotation rather than
  rebuilt, so it costs two multiplies a pixel and nothing at all when the eye
  is level.
- Adds `extensions.animation.cyclovergence`, which rolls the eyes as the gaze
  goes down and leaves them level looking ahead or up. Real cyclovergence
  follows the head and there is no head here, so the gaze stands in for it:
  looking down is the grazing posture. The value is the angle at full downward
  gaze.
- Adds three extensions for eyes that are drawn rather than grown:
  `texturedPupil` fills the pupil from the iris texture's centre, so a pattern
  that runs to the middle closes over it instead of being cut out by a flat
  disc; `extensions.animation.gazeRange` scales how far the eye may look, since
  a drawing has a white to stay inside and a box to stay inside where an
  eyeball has neither; and `extensions.display.eyeGap` sets how far apart the
  two eyes sit, because how far apart a face wears them is the package's
  business and not the panel's.
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

The page has four tabs. Controls holds status, style selection, audio (mute,
play, and volume), screen brightness, cycling, and Wi-Fi setup. Packages holds
the package list and the upload form, so the default view stays short on a
phone. Screen captures the panel. Eye config edits the active package's
`config.eye`.

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

## Web config editor

The Eye config tab edits the active package's `config.eye`. It is a
hand-written copy of the simulator's panel (`sim/src/config_panel.cpp`) and the
two are meant to stay in step: the same sections in the same order, the same
controls in each section, and the same help text on each control. A setting
added to one belongs in the other, in `CONFIG_SECTIONS` in `web/app.js`. A `*`
after a name means the same thing in both places -- an extension of this fork
that a stock Adafruit Monster Eyes package will not understand.

Three things can be done with an edit:

- **Try it** applies it to the running eyes only. The text is held in RAM and
  handed to the renderer instead of the package's file, so the next reboot
  goes back to what is on the drive. Nothing is written.
- **Overwrite this package** replaces the package's `config.eye`.
- **Save as a new package** copies the package's bitmaps and sounds into a new
  one with this config, and switches to it. The original is untouched.

**Revert to file** drops an unsaved edit and rebuilds from the drive.

Saving rewrites the file from the parsed document, so comments and formatting
in the original `config.eye` are lost -- the simulator's Save As does the same.

The endpoints are:

- `GET /api/config` -- the text the eyes are running on, which is the unsaved
  edit if there is one. `X-Config-Unsaved: 1` says which it is.
- `POST /api/config/apply` -- body is the config; applied in RAM only.
- `POST /api/config/overwrite` -- body is the config; written to the package.
- `POST /api/config/save-as?id=...` -- body is the config; copies the package.
- `POST /api/config/revert` -- discard the unsaved config.

A config that will not load is rejected and the previous one is put back,
rather than leaving the panel blank.

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

`tools/upload_package.py <device-ip> <package>` does the start/file/commit
sequence from the command line. It paces its writes: the web server is polled
from the render loop and drains a socket in bursts between frames, so a whole
texture handed over in one write resets the connection, where the same bytes
sent in 4 KiB pieces go through in a few seconds.

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

Colours are read off photographs of a sika doe and a red deer. The pupil is
photographed as a dark blue-grey rather than black. Deer sclera is brown and
barely shows, so it is near black.

The iris used to be that flat brown with stripes of angular noise over it,
which read as a sunburst: a stripe of constant width running the whole depth of
the iris is not what a fibre looks like. It is now built by the same
feature-agglomeration model as the goat -- see `tools/README.md` -- with
coarser, fewer fibres, a weaker collarette and shallower crypts, because a
deer's iris is smoother and less combed than a goat's. It keeps its even
lighting: no darkening towards the rim and no shading from one side to the
other, which reads well on an eye this dark.

The eyelids are generated as well. The opening is an ellipse, which has a
vertical tangent at each corner, so the lids meet there roundly and the eye
keeps its width to the edge; the migrated pair tapered to a point instead.
Their closed edges are arcs rather than straight lines, because the renderer
interpolates lid shape between open and closed on every frame and a flat lid
closes like a shutter.

## The goat package

`data/eyes/goat` is generated too, for the same reason the deer package is:
what it carried before was a cat's eye under another name -- a vertical slit
in a grey-blue iris, neither of which a goat has.

```sh
python projects/monster-eyes-port/tools/make_goat_eye.py
```

It is the worked example for `tools/eye_textures.py`, the shared artwork
module, and the script to copy when generating a package of your own.

The pupil is the feature that says goat: a wide horizontal bar with blunt ends
(`slitPupilHorizontal` and `slitPupilRounded`), reaching most of the way across
the iris, which is what separates it from the deer's shorter oval. As with the
deer, its height comes from `pupilMax` rather than being set directly.

Colours are read off photographs of domestic goats: light brown through the
body of the iris, browner towards a distinct dark limbal ring. The base is
duller than the raw mid-iris sample on purpose, because the fibres put the
light back and a base that starts at the measured value ends up reading as
yellow. The lid shades the upper third a stop darker. There is effectively no
white -- the globe is iris nearly edge to edge -- so the sclera is near black
with a brown cast. The pupil is flatly black.

The fibre pattern follows a published model rather than an invented one: Shah
and Ross, *Generating Synthetic Irises by Feature Agglomeration* (ICIP 2006),
with Lefohn et al., *An Ocularist's Approach to Human Iris Synthesis* (IEEE
CG&A 2003), for the layered view of an eye. The implementation is shared, in
`tools/eye_textures.py`; **`tools/README.md` documents the model, every
parameter, and how to start a new package from it**.

Where the top of the eye falls along the texture's angular axis was measured
rather than assumed: a band painted at 0.25 came out on the right of the
rendered eye and one at 0.75 on the left, so the top is 0.0.

The eyelids are generated as the deer's are, an elliptical opening with
`tracking` off, and give the eye its wide, flat oval.

The iris is deliberately smaller than the eyeball (100 against 125) so the eye
has somewhere to travel when it looks around. Filling the eyeball looked right
in a still frame and moved the pupil one pixel over 150 frames: with a bar
pupil spanning the iris there is nothing left to see move. The lid opening was
narrowed to match (0.86 of the half-width), so the spare sclera does not show
as a dark band down each side at rest -- it appears as a dark corner only when
the eye actually looks that way, which is what the photographs show.

`gazeMax` is raised to five seconds because a goat holds its gaze and turns its
head. Its one dramatic eye movement is not lateral at all: each eye
counter-rotates about its own optic axis by 50 degrees or more as the head goes
down to graze, keeping the slit level with the horizon. The package asks for
that with `extensions.animation.cyclovergence: 50`, which the renderer ties to
downward gaze, since a head is the one thing this toy has not got.

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

## One big eye instead of two

A package can ask for a single eye filling the panel rather than the usual
pair:

```json
{
  "extensions": {
    "display": {
      "singleEye": true,
      "side": "right"
    }
  }
}
```

Two 128px eyes sit side by side on the 320x240 panel; one eye instead takes the
panel's short side, 240px, centred — about twice the lit area. `side` picks
which eye it is, `"left"` or `"right"`, and decides which of the config's `left`
and `right` blocks applies; it defaults to the left.

The renderer asks the display backend to rearrange itself once the config has
been read, since how many eyes there are is a property of the package rather
than of the sketch that built the backend. A backend that cannot oblige keeps
the pair and says so in the startup log, so a package asking for this still runs
on hardware that cannot do it. The sketch keeps one backend across style
changes, so switching from a single-eye package back to an ordinary one puts the
pair back.

Because the eye is nearly twice the size, its polar maps are roughly four times
the area. `begin()` already steps the eye size down when the tables or the
texture budget will not fit, so on a constrained board a single eye simply comes
out smaller rather than failing.

## Holding the eye still

A package can switch off either autonomous behaviour from its `config.eye`,
under the `extensions` block the audio settings already live in:

```json
{
  "extensions": {
    "animation": {
      "autoGaze": false,
      "autoBlink": false
    }
  }
}
```

`autoGaze: false` stops the eye wandering, so it holds whatever direction it was
last pointed; `autoBlink: false` means it never blinks on its own. Both default
to on, and a key that is absent leaves the behaviour alone, so every existing
package is unaffected.

This is read by the renderer itself rather than by the sketch, so it applies
identically on the board and in the Linux preview. Pointing the gaze
deliberately still works with `autoGaze` off — that is the point of it — and
`blink()` still blinks an eye whose `autoBlink` is off.

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

`--panel` (or `p` in the window) opens a live `config.eye` editor to the right of
the display, with a style drop-down and a help strip under the preview: move a
slider and the eye changes as you watch. The edited config
is served to the renderer from memory rather than written out, so nothing under
`data/` is touched and no scratch files exist; Save writes a new package,
copying the bitmaps and preserving keys the renderer does not parse, such as
`extensions`.

Needs `cmake`, `g++`, `zlib1g-dev` and, for the window, `libsdl3-dev`. Dear
ImGui is vendored under `sim/third_party/`. See `sim/README.md` for the keys,
the capture options and what the shim covers.

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
