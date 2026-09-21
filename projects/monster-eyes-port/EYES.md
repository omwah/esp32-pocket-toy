# The eye packages

Twenty-six of them live in `data/eyes/`. Most came from Adafruit; four are drawn
here by a script, and those four are most of this file, because they are the
ones with decisions in them worth recording.

[`tools/README.md`](tools/README.md) documents the shared iris model and the
texture space every generated package is authored in.

## Where they came from

**Thirteen are Adafruit's own Monster Eyes artwork**, from
[`M4_Eyes/eyes`](https://github.com/adafruit/Adafruit_Learning_System_Guides/tree/main/M4_Eyes/eyes),
MIT-licensed by Adafruit Industries: `anime`, `big_blue`, `demon`, `doom-red`,
`doom-spiral`, `fish_eyes`, `fizzgig`, `hypno_red`, `reflection`, `skull`,
`snake_green`, `spikes` and `toonstripe`. Their iris textures were resized for
this panel and their wide spherical sclera maps projected into square ones; the
`config.eye` files are Adafruit's, which is why those packages read differently
from the ones written here -- `eyelidIndex` and the rest.

**Eight came from [Adafruit Uncanny Eyes](https://github.com/adafruit/Uncanny_Eyes)**
by Phil Burgess, also MIT, by way of the sibling `projects/uncanny-eyes`, where
they are maintained as editable PNGs: `hazel`, `cat`, `dragon`, `newt`,
`no_sclera`, `nauga`, `owl` and `terminator`. `owl` is the odd one -- upstream
ships no conversion directory for it, so its eyelids were rebuilt from the
arrays in `owlEye.h` and its flat sclera and iris replaced with feathered tissue
and a golden iris.

**`big_anime` is a design of its own**, drawn for the sibling project rather
than taken from either upstream: a large eye with a sapphire iris and oversized
catchlights. This renderer draws it without the production version's glints.

**Four are generated here**: `deer`, `goat`, `sauron` and `pomni`. The deer and
goat replaced converted artwork that was wrong about the animal; Sauron and
Pomni are new. The rest of this file is about those four.

Sounds are package-local, and mostly CC0 or CC BY; the table in
`projects/uncanny-eyes/audio/README.md` lists each one's source and licence.
`validation/migrated-styles.json` records, package by package, which of the
above it is and what was accepted as different from the artwork it started
from.

## Deer

`data/eyes/deer` is generated rather than painted. The artwork it replaced
faked its horizontal pupil by putting two black lobes into the bottom rows of
the iris texture, at the angles left and right of centre -- with a round pupil
that is the only way to widen one sideways. The renderer builds the slit itself,
so the texture is iris all the way down and the fibres reach the pupil edge:

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
keeps its width to the edge; the almond it replaced tapered to a point.
Their closed edges are arcs rather than straight lines, because the renderer
interpolates lid shape between open and closed on every frame and a flat lid
closes like a shutter.

## Goat

`data/eyes/goat` is generated too, for the same reason the deer is: the artwork
it started from was a cat's eye under another name -- a vertical slit in a
grey-blue iris, neither of which a goat has.

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

## The Eye of Sauron

`data/eyes/sauron` is an original package. Its artwork is generated too:

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

## Pomni

`data/eyes/pomni` is Pomni from The Amazing Digital Circus: a white disc in a
heavy black outline, three straight lashes off the rim, and a six-wedge red and
blue pinwheel for an iris, which is the pupil as well.

```sh
python projects/monster-eyes-port/tools/make_pomni_eye.py
```

It is the one drawn package rather than a grown one, and nearly every choice
follows from that. Which texture holds what is the whole design: the eyeball's
silhouette stays put on screen while the iris slides about inside it, so the
sclera carries everything that must not move -- her face, the lashes, the
outline and the white -- and the iris carries only the pinwheel and its ring.
Painting the outline into the iris, which is the obvious thing to do when the
iris covers the whole eye, made it wander 25 px on a 128 px eye as the gaze
moved, which reads as the eye sliding off her face.

Depth in a polar texture is not linear in screen pixels, because the map's
outer rows are the part of the sphere curving away. The bands were laid out
against a measured table rather than by eye: a banded texture was rendered and
each band's radius read off. `make_pomni_eye.py` carries the table it was laid
out against, and it has to be redone if `irisRadius` changes.

The package leans on four of the renderer's extensions, three of which exist
because of it:

- `irisDilation` -- the pinwheel IS the pupil, so dilation resizes the disc
  rather than opening a hole in it, which would squeeze the wedges outward and
  swallow their ring.
- `extensions.animation.gazeRange` -- at full travel the pinwheel slid out of
  its own white and the artwork reached the edge of the box it is drawn in.
- `extensions.display.eyeGap` -- Pomni wears her eyes close together, closer
  than the panel's own layout puts them.
- `autoBlink: false` -- her lids are her own skin colour, which is right on a
  face and wrong on a panel showing nothing but two eyes, where a blink reads
  as the eyes vanishing.

The gap and the gaze range work against each other, and both work against the
lashes: the drawing must stay inside its own square and out of the other eye's.
Those limits were measured, not guessed, over runs of several hundred frames --
ink that stops dead at the seam between the two squares is the failure to look
for, and a bounding box cannot see it, because the cut takes a bite out of the
middle of an edge without changing the extent.
