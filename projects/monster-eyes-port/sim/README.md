# Linux preview

`eye-sim` runs the eye renderer on a desktop, against a simulated 320x240
panel, so a texture, a colour or a config value can be tried in a second rather
than a flash cycle.

It is a preview, not a reimplementation. `Adafruit_Monster_Eyes.cpp`,
`Eyes_Assets.cpp` and `Eyes_Display.cpp` are compiled from `../lib` unmodified,
they parse the same `config.eye` with the same ArduinoJson, and they read the
same BMPs out of `../data`. What this directory adds is a display backend that
writes to memory instead of a panel, and a shim providing the handful of
Arduino core calls the library makes. Nothing under `../lib` or `../src` is
touched and the PlatformIO build is unaffected.

The panel geometry is copied from `src/composite_tft_display.h`: a 320x240
panel, two 128x128 eyes, left at x=18 and right at x=174, both at y=56. A pixel
on screen here is the pixel the device would light.

## Dependencies

| What | Package on Ubuntu/Debian | Why |
|---|---|---|
| CMake 3.16+ | `cmake` | Build system |
| A C++17 compiler | `g++` | Tested with GCC 15 |
| zlib | `zlib1g-dev` | Deflating the capture PNGs |
| SDL3 | `libsdl3-dev` | The preview window; optional |
| Dear ImGui | vendored, `third_party/imgui` | The config editor's widgets |
| ArduinoJson 7.x | fetched by PlatformIO | Parsing `config.eye` |

```sh
sudo apt install cmake g++ zlib1g-dev libsdl3-dev
```

SDL3 is optional. Without it the build still succeeds and headless capture
still works; only the window is missing, and CMake says so rather than failing.
libpng is deliberately not a dependency: the PNG writer in `src/capture.cpp`
emits the format directly through zlib, which everything already has.

Dear ImGui v1.92.9b is vendored under `third_party/imgui` (MIT; see
`VENDORED.md` there for the exact commit). Only the core sources and the SDL3
backends are included. It is compiled only when SDL3 is found, so a headless
build does not touch it.

ArduinoJson is header-only and builds natively, so the simulator parses
`config.eye` with the real parser rather than a lookalike. CMake finds the copy
PlatformIO fetched under `../.pio/libdeps/*/ArduinoJson/src`. If the firmware
has never been built, either run `pio pkg install -d ..` first or point CMake at
a copy with `-DARDUINOJSON_DIR=/path/to/ArduinoJson/src`.

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Running

Paths are relative to `projects/monster-eyes-port`, since `--assets` defaults to
`data`:

```sh
./sim/build/eye-sim --list             # Eye packages found
./sim/build/eye-sim --eye deer         # Open the preview window
./sim/build/eye-sim --eye deer --scale 4
```

### Overriding settings

`--set KEY=VALUE`, repeatable, changes any `config.eye` setting without editing
the package. Dotted keys reach into a block:

```sh
./sim/build/eye-sim --eye anime --set irisFlow=0.203 --set squint=0
./sim/build/eye-sim --eye hazel --set extensions.display.singleEye=true
./sim/build/eye-sim --eye cat --set irisColor=0xF800 --gif /tmp/cat.gif
```

The value is typed the way the file would spell it: `true` and `false` become
booleans, `0x`-prefixed values stay strings so the renderer's colour decoder
sees them as it would in a file, anything numeric becomes a number. The edit
goes through the same in-memory overlay the panel uses, so the package on disk
is only ever read, and the renderer parses a `config.eye` itself rather than
having values poked in behind it. This covers every setting the panel exposes.

### Keys

| Key | Action |
|---|---|
| arrows | Steer the gaze, in the direction the key points |
| `m` | Toggle mouse-driven gaze; the pupil follows the pointer |
| space | Blink |
| `g` / `b` | Toggle the gaze and blink animators |
| `[` / `]` | Previous / next eye package |
| `r` | Reload `config.eye` from disk |
| `c` | Capture 8 frames to `eye-capture-*` |
| tab | Toggle the status overlay |
| `p` | Toggle the config.eye editor |
| `i` | Open or close the bitmap editor window |
| `?` | Show the key list over the eye |
| `q`, escape | Quit |

`?` (or `/`, since which one SDL reports depends on the layout) dims the eye and
lists the keys over it; `?` or escape dismisses it. The list and `--help` are
generated from the same table in `src/main.cpp`, so they cannot drift apart.

`r` rebuilds the renderer rather than patching it: the polar maps, the eye size
and the texture budget are all derived from the config being reread, so there is
no honest way to change one in place. Edit `data/eyes/<id>/config.eye`, press
`r`, and the result is on screen.

## The config.eye editor

`--panel`, or `p` in the window, opens an editor to the right of the display and
a help strip beneath it. Move a slider and the eye changes as you look at it.
The eye stays where it is and keeps its integer scale; the window simply grows
right and down to make room.

```sh
./sim/build/eye-sim --eye hazel --panel
./sim/build/eye-sim --eye hazel --panel --panel-width 460
```

It is grouped as geometry, pupil, display, colours, animation, rotation and
iris flow. Within each section the extensions -- the settings marked with a
star, which a stock Monster Eyes package will not understand -- come last, so
what upstream has is what you read first.
Texture paths and the per-eye `left`/`right` blocks are not exposed; edit those
in the file.

The Rotation section ends with `roll`, which turns the whole eyeball about its
own optic axis rather than spinning a texture within it: pupil, iris and sclera
go round together and the lids stay put. With two eyes showing they roll
opposite ways, which is the cyclovergence a goat or a horse uses to keep its
slit pupil level as its head goes down to graze. A per-eye `roll` in the
`left`/`right` blocks overrides the mirrored pair.

`cyclovergence`, at the end of the Animation section, is the same rotation
driven by the gaze instead of held fixed: the eyes roll as the gaze goes down
and sit level looking ahead or up, the value being the angle at full downward
gaze. It is behaviour rather than geometry, which is why it lives with the
animators and writes `extensions.animation.cyclovergence`.

The Animation section writes `extensions.animation.autoGaze` and `autoBlink`,
which hold the eye still or stop it blinking, and carries `gazeMax` alongside
them since that is how long the eye waits between movements. Those are read by the renderer,
not by the simulator, so a package saved with them set behaves the same way on
the board. `--no-auto-gaze` and `--no-auto-blink` still override the file, but
only when actually given: without them the config decides. The `g` and `b` keys
toggle the animators for the session, and an edit re-applies the config, so a
keypress does not outlive the next slider move.

The package being edited is a drop-down at the top of the panel, so any style is
one click away; `[` and `]` still step through them one at a time. Switching
discards unsaved edits, as `r` does.

Hovering a control explains it in a strip along the bottom of the preview, not
in a tooltip at the pointer: a tooltip does not wrap, so anything long enough to
be useful runs off the edge of the screen. The strip is as wide as the eye, so a
sentence takes a line or two rather than five, and the panel keeps that space
for controls. The window grows downward to make room, so the preview itself is
never covered.

**Nothing is written anywhere until you press Save.** The renderer has no way to
take settings other than by reading a config.eye — `begin()` parses the file and
sizes the polar maps and textures from it, and any setter called beforehand is
overwritten by the parse — so applying a slider means producing a config.eye and
rebuilding.

That file never exists on disk. `FFat` is part of the shim, so it can serve a
path from memory: the editor hands it the serialised document, and the next
`begin()` parses it through `fmemopen()` without anything touching the
filesystem. Only the config is shadowed, so the bitmaps still load from `data/`
as usual, and clearing the overlay — on `r`, on a package change, or on Revert —
hands the real file straight back. There is no scratch directory and nothing to
clean up.

### Config space, not screen space

The panel edits what the file means, which is not what the renderer ends up
using. `begin()` rescales `eyeRadius`, `irisRadius`, `slitPupilRadius` and
`fixate` from the config's own coordinate space into the actual eye size — a
config saying `eyeRadius: 125` with no `displaySize` is read as a 240px space
and becomes 53 on a 128px eye. So the panel shows 125, and a value the file
omits falls back to the library's default rather than to whatever the running
renderer settled on. Showing the fitted number would be wrong twice over: it is
not what the file says, and touching the control would write it back, baking one
display's scaling into the package.

### Saving

Type a name and press Save. That writes `data/eyes/<name>/config.eye` and copies
the bitmaps beside it, so the result is a complete package that loads like any
other; it appears in `[` / `]` straight away. An existing name is refused rather
than overwritten, and the package you started from is never modified. **Revert
to file** throws the edits away and rereads the original.

The saved JSON is the original document with the edited keys replaced, so
`extensions` — the block carrying the device's audio — and any other key the
renderer does not parse survive untouched. Two things do not survive: comments,
because the JSON parser does not preserve them, and key order, which is
rewritten. Colours are written as `"0xF800"` strings, which round-trip exactly;
an `[r, g, b]` array cannot represent every RGB565 value.

Applying an edit rebuilds the renderer, since the polar maps and texture budget
are sized from the config. The gaze, blink phase and iris dilation are carried
across, so the eye keeps looking where it was instead of re-centring on every
slider movement. A rebuild costs under a millisecond and does not accumulate
memory — 2000 edit-and-rebuild cycles move RSS by about 140 KiB, the same as
200, which is the allocator settling rather than a leak.

## The bitmap editor

`--images`, or `i` in the preview, opens a paint window with a drop-down of
every `.bmp` in the package. It is a real second window: move it, resize it, put
it on another monitor. `i` again, or closing it, puts it away. Paint and the eye changes as you
watch. Tools are pencil, eraser, fill, eyedropper, line, rectangle and Bezier, with a
brush size, zoom, and undo and redo. Each says what it is for on hover.

Edits go the same way config edits do: the image is encoded back to a BMP in
memory and served through the FFat overlay, so nothing under `data/` is written.
Save As writes the edited bitmaps into the new package and copies the rest, so
what is saved is what you were looking at. **Revert** drops one image's edits
and rereads its file.

**Edits survive switching eyes.** Both the config and the bitmap edits are held
by device path, which carries the package id, so one package's edits are inert
while another is loaded and come back into force on returning to it — step
through several eyes and come back, and the work is still there. Nothing is
written to disk either way. `r` is the way to throw an eye's edits away and
reread its files, and it only affects the package you are on.

It has its own SDL window, renderer and ImGui context, since ImGui's
multi-viewport support is docking-branch only and one context holds one window's
input. Events are routed to a context by the window id they carry, so typing in
the editor never reaches the preview's keys.

### The two kinds of image, and why it matters

The palette follows what the file can hold, and the two are not the same job.

**Eyelids** (`upper.bmp`, `lower.bmp`) are 1-bit, always 240x240, so there are
two colours and no others. More to the point, `bmpLoadEyelid` reads only the
**topmost and bottommost lit pixel of each column** and discards the rest: the
image is a silhouette envelope, not a picture, and a hole punched in the middle
of a lid changes nothing at all. For an upper lid the top edge of the lit band
is where the lid sits fully open and the bottom edge where it sits fully shut,
reversed for a lower lid, with the config's `squint` deciding where between them
it rests, so a lid is drawn in two steps: stroke its edge with the Bezier tool,
then flood fill the side that should be solid. The stroke alone is not enough —
a bare line puts the open and shut positions on the same row, which pins the lid
still.

**Iris and sclera** are 24-bit and **polar**: x is the angle around the eye,
0 to 1023 across the width, and y is distance out from the pupil. They are not
pictures of an eye, so painting one is not painting on the eye — the preview
beside the editor is how you tell what a stroke did. The renderer converts to
RGB565 as it loads, so the picker's colours are shown already rounded to what
will survive, and the swatch row offers the colours the image actually uses.

## Looping GIFs

```sh
./sim/build/eye-sim --eye hazel --gif /tmp/hazel.gif
```

writes a GIF that loops with no visible join, with the blinks and glances left
in. There is no arithmetic that gives one: gaze and blink are randomly timed,
so no fixed frame count repeats. What there is instead is a finite amount of
state, so the eye does come round to exactly where it was — the simulator
renders until a frame is **bit-identical** to an earlier one and takes
everything between the two. Because that is an exact repeat of the whole
framebuffer, the wrap is not a small jump that has been smoothed over; it is
the step the animation would have taken anyway.

| Flag | |
|---|---|
| `--gif PATH` | Where to write it |
| `--gif-seconds N` | Longest loop to accept, default 6. The longest that fits wins; a GIF is a fat format and half a minute of eye runs to tens of megabytes |
| `--gif-search N` | Frames to look through for the repeat, default 900 |
| `--gif-scale N` | Whole-pixel magnification, default 2 |
| `--gaze-return N` | Let the gaze wander as usual, then lead it home over the last N frames so the loop closes |
| `--gaze-tour N` | Walk the gaze round a circle every N frames instead of letting it wander |
| `--gaze-tour-radius N` | How far the tour reaches, 0 to 1, default 0.7 |

Frames carry only what changed since the one before, inside the smallest
rectangle that holds it, with the rest transparent so the previous pixels show
through. An eye is a small moving thing on a large still background, so most of
the picture is sent once rather than thirty times a second — between a third and
seven eighths off, depending on how much of the frame moves.

`--fps` sets the frame rate as usual. The encoder is written into the simulator
rather than shelled out to ffmpeg, so a GIF needs no second toolchain
installed, and it is deterministic: the same command gives the same bytes. That
last part is load-bearing. One palette is built from every frame at once and
there is no dithering, because anything that varies per frame puts a visible
seam back into a loop that was exact — ordered dithering is the usual culprit,
and it made an early version of this shimmer at the join.

Different packages settle into different loops: an eye with no lids and no
tracking repeats in under a second, while one that blinks needs a few.

### Why a loop rarely contains a glance

The autonomous gaze picks random positions, so it never returns to precisely
one it has held. An exact repeat can therefore only ever sit inside a stretch
where the eye is still — which is why sauron, whose only other motion is the
iris flow, loops in 23 frames with the eye locked forward, and why raising
`--gif-seconds` does not help: there is no longer repeat to find, at any seed.

**`--gaze-return N` keeps the eye's own glances.** The gaze is left to wander
and then led back where it started over the last N frames, which is a thing eyes
do anyway:

```sh
./sim/build/eye-sim --eye sauron --gaze-return 30 --gif-seconds 10 \
    --gif /tmp/sauron.gif
```

Both ends have to be the same settled eye, so the loop is pinned to the centre
at its start and led back to the centre at its end, with the middle left alone.
The length is not free either: it has to be one where the clock-driven part --
the iris flow -- comes round, and that is found by running a still eye and
noting which frames repeat, rather than assumed to be a tidy period, because it
is not. Raise `--gif-seconds` if it reports that nothing lines up.

Two details it has to handle, both of which showed up as a dozen stubborn
pixels along the eyelid: the lids follow the gaze through a filter, so the eye
holds still for the last two thirds of the return while they settle; and the
pupil's dilation is a random walk, so it is pinned for the length of the loop.

**So a GIF never shows the eye dilating.** With an ordinary pupil that is easy
to miss. With `irisDilation`, where the dilation resizes the whole iris disc,
it is the difference between an eye that breathes and one that stares: Pomni's
disc runs 26 to 41 px on screen and sits at 35 in a GIF. Nothing is broken when
that happens, and `--pupil N` will at least hold it at a size of your choosing.
Letting it wander and leading it home over the last frames, the way the gaze is
handled, would fix it properly and has not been done.

`--gaze-tour N` instead walks the gaze round a circle every N frames, which
gives a shorter file and a tidier motion at the cost of the eye's own
behaviour. A circuit repeats, so the ordinary search finds the loop:

```sh
./sim/build/eye-sim --eye sauron --set irisFlowSpeed=1.0 \
    --gaze-tour 60 --gif-seconds 2 --gif /tmp/sauron.gif
```

The loop is as long as it takes the tour and the iris flow to line up, so it
pays to make the flow period a round number. Sauron's default `irisFlowSpeed` of
1.3 gives a 769 ms cycle, which only meets a 2-second tour after ten seconds and
twenty-three megabytes; setting the speed to 1.0 makes both a whole number of
seconds and the loop comes out at two.

## Capturing frames for an agent

```sh
./sim/build/eye-sim --eye deer --frames 30 --out /tmp/deer
```

That writes `/tmp/deer-000.png` through `/tmp/deer-029.png` at the panel's own
320x240 regardless of `--scale`, a `/tmp/deer-NNN.json` beside each one, and
`/tmp/deer.json` listing the whole sequence. No window is opened, so it works
over SSH and in CI.

The sidecar says what the animator was doing at the instant the pixels were
drawn, which is usually the faster question to answer:

```json
{
  "frame": 2,
  "timeUs": 66666,
  "eye": "deer",
  "eyeSize": 128,
  "gaze": {"x": 0.0905, "y": -0.0247},
  "gazeMap": {"x": 129.456, "y": 125.058},
  "blinkPhase": 1.0000,
  "irisFraction": 0.9196,
  "pupil": 0.5069,
  "renderMs": 0.000,
  "transferMs": 0.000,
  "wallMs": 0.083,
  "frameRate": 30.00,
  "autoGaze": true,
  "autoBlink": true
}
```

`blinkPhase` is 0 open and 1 fully shut. `renderMs` and `transferMs` are the
library's own figures and are not useful here: the library measures them with
`micros()`, which under the virtual clock only moves between frames, and a
memory backend has no bus to push pixels down. `wallMs` is the real one — host
time actually spent on that frame, which is what answers whether a change made
the renderer slower.

### Which way the eye looks

The simulator talks in screen space throughout: `+X` is right and `+Y` is up as
you see it. The arrows point where they are drawn, the pupil follows the mouse,
and `--gaze 1,0` looks right.

`Adafruit_Monster_Eyes::setGaze()` is the opposite on both axes, and
`src/main.cpp` inverts it in one place (`setScreenGaze`). `_frameEyeX/_frameEyeY`
are the point the renderer *samples from* in the polar map rather than where the
pupil is drawn, so raising them slides the sampling window one way and the pupil
the other. Measured on the cat package against a centred pupil at x=87.8,
y=121.3:

| call | pupil lands at | which is |
|---|---|---|
| `setGaze(+1, 0)` | x 75.9 | left |
| `setGaze(-1, 0)` | x 98.1 | right |
| `setGaze(0, +1)` | y 144.6 | down |
| `setGaze(0, -1)` | y 94.9 | up |

The doc comment on `setGaze()` claims the reverse (`-1.0 hard left to 1.0 hard
right`, `-1.0 down to 1.0 up`). Nothing in the firmware calls it —
`MonsterController::setGaze()` has no callers — so the mismatch had never been
exercised. **If you wire up gaze control on the device, invert it there too, or
trust the table above over the header.** The library is left unmodified.

Capture sidecars record both: `gazeScreen` is what you see, `gazeLibrary` is
what the library holds.

### One eye or two

The panel's **Display** section has `singleEye` and `side`, so this can be tried
without hand-editing the file. A package that sets `extensions.display.singleEye`
gets one eye filling the panel — 240px centred, against two 128px eyes side by side — and the preview
shows it exactly as the device would, because the layout lives in the display
backend and both backends implement it the same way. `side` picks which eye it
is. Captures are of the whole 320x240 panel either way, so nothing about the
capture options changes.

### Frame rate

`--fps` (default 30) sets the virtual clock's step and caps the window loop.
Both matter, because **the iris animator is driven by a frame counter, not by
the clock**: `updateIris()` walks a 128-frame fractal cycle one step per
rendered frame, with no time term in it at all. On the device that cycle takes
several seconds, because the SPI panel is the bottleneck. Drawing into memory
there is no bottleneck, so an uncapped host loop free-runs into the thousands of
frames per second and the same cycle finishes in milliseconds — the pupils
visibly pulsate.

So the window is paced. The gaze and blink animators are driven by `micros()`
and would not care, but the iris is only meaningful at roughly the rate the
hardware achieves. Raise `--fps` to see the eye move faster; `--fps 0` uncaps the
window for profiling, and pulsating pupils are the expected result. Captures are
always stepped, falling back to 30 when the window is uncapped, because there is
no reproducible sequence without a fixed step.

### Reproducibility

Captures are deterministic. `millis()` and `micros()` come from a virtual clock
that advances by exactly `1/--fps` per frame, and `random()` is a xorshift
seeded only from `--seed`, so the same command gives byte-identical PNGs on any
machine. Two builds can be diffed frame by frame.

Useful consequences:

```sh
# The same moment every time, 40 frames in
./sim/build/eye-sim --eye deer --frames 1 --skip 40 --out /tmp/settled

# A different but equally repeatable animation
./sim/build/eye-sim --eye deer --frames 30 --seed 99 --out /tmp/other

# Just geometry, with the animators out of the way
./sim/build/eye-sim --eye deer --frames 1 --no-auto-gaze --no-auto-blink \
    --out /tmp/static
```

The window is the exception: it runs on the host clock, so the frame rate in the
overlay is the one really being achieved (which, being paced, should sit at
`--fps`). Pressing `c` there switches to the
virtual clock for the length of the capture, so frames taken from the window are
as reproducible as frames taken from the command line.

## How the shim works

The simulator compiles the library with `-DARDUINO_ARCH_ESP32`, so
`Eyes_Platform.h` and `Eyes_Assets.cpp` take the same preprocessor branches they
take on the board. The headers those branches reach for are supplied by `shim/`:

| Shim | Stands in for | Real or stub |
|---|---|---|
| `Arduino.h` / `.cpp` | The core: clock, random, `Print`/`Stream`, GPIO | Real |
| `FFat.h` / `.cpp` | The FAT asset filesystem | Real, over a host directory |
| `esp_heap_caps.h` | ESP-IDF's capability allocator | Real, over `malloc` |
| `SdFat_Adafruit_Fork.h` | SdFat | Stub |
| `Adafruit_SPIFlash.h` | The flash chip driver | Stub |
| `Adafruit_TinyUSB.h` | USB mass storage | Stub |
| `SPI.h` | The SPI bus | Empty |

The last four are stubs because on ESP32 the assets come from FFat and those
libraries are unused — but the USB drive-mode code that references them is not
behind an `#if`, so the types have to exist. Every stub method fails, which is
the honest answer on a host with no flash chip, and the simulator never calls
that code.

`FFat` is the one that does real work. It resolves the device's absolute paths
(`/eyes/deer/config.eye`) against `--assets`, and refuses a path that tries to
climb out of that root with `..`, so a hostile config cannot read arbitrary
files. It can also serve a path from memory, which is how the editor applies a
change without writing a file.

Heap figures are fiction: the shim reports 8 MB free, so the texture loader
never decimates and the preview shows the eye at full resolution. That is
deliberately not what the device does — with 16 MB of flash but a much smaller
internal heap, the ESP32 may load a coarser texture than you see here. The
preview answers "is this eye right"; the device answers "does it fit".

## Limitations

Only the panel is simulated. Touch, audio, the web interface, package upload and
the status icons the sketch draws straight to the TFT are all outside the
renderer and are not here. The status icons in particular never pass through the
display backend, so they are absent from the preview for the same reason they
are absent from the device's own `/api/frame` screenshots.
