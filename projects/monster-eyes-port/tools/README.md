# Generating eye package artwork

Scripts here write the BMPs a package needs straight into `data/eyes/<id>/`.
They need Python with numpy and nothing else; every one of them can be run from
anywhere and writes relative to its own location.

```sh
python projects/monster-eyes-port/tools/make_goat_eye.py
python projects/monster-eyes-port/tools/make_deer_eye.py
python projects/monster-eyes-port/tools/make_sauron_eye.py
```

[`../EYES.md`](../EYES.md) describes the four packages these scripts
build and what was measured to build them; this file is about the machinery.

`tools/eye_textures.py` is the shared module. `make_goat_eye.py` is the worked
example of using it, and is the one to copy when starting a new package;
`make_deer_eye.py` uses it too, with parameters of its own. `make_pomni_eye.py`
takes only the plumbing -- the BMP writers and the eyelids -- because a drawn
cartoon eye has no anatomy to synthesise. `make_sauron_eye.py` still carries
its own copy of the helpers, for the same reason and because regenerating it to
prove a refactor would change a bitmap that is already shipped and validated.

## Texture space

Both textures are sampled in polar space. **X is the angle around the eye**, 0
to 1 and wrapping; **Y runs inwards**, so row 0 of the iris is its rim and the
last row is the pupil edge, while row 0 of the sclera is the rim of the eyeball
and its last row meets the iris.

Authoring straight into that space is what keeps a feature honest: a band of
brightness ending at some depth arrives on screen as a ring, and a stripe at
some angle arrives as a spoke pointing straight out from the pupil. Nothing
needs unwrapping, and nothing leans or spirals unless it was written to.

Two things follow that are easy to get wrong:

- **Where the top of the eye is on the X axis must be measured, not assumed.**
  It depends on the renderer's angle convention. Paint a band at a known angle,
  render it, and see where it lands: for this renderer a band at 0.25 came out
  on the right of the eye and one at 0.75 on the left, which puts the top at
  0.0. `TOP_ANGLE` in `make_goat_eye.py` records that.
- **No pupil belongs in an iris texture.** The renderer builds the pupil,
  including a slit at either orientation and with either kind of end, from
  `slitPupilRadius`, `slitPupilHorizontal` and `slitPupilRounded`. The deer
  package's original artwork faked a horizontal pupil by painting black lobes
  into the texture, and that is what a generated one exists to stop doing.

## The fibre model

`synthesise_iris()` implements a published model rather than an invented one:

> Samir Shah and Arun Ross, "Generating Synthetic Irises by Feature
> Agglomeration", *ICIP 2006*.
> <https://www.cse.msu.edu/~rossarun/pubs/ShahIrisSynthesis_ICIP2006.pdf>

> Andrew Lefohn, Brian Budge, Peter Shirley, Richard Caruso and Erik Reinhard,
> "An Ocularist's Approach to Human Iris Synthesis", *IEEE Computer Graphics
> and Applications* 23(6), 2003.

Shah and Ross build an iris as a background texture with anatomical features
agglomerated onto it in turn. Lefohn et al. contribute the other half of the
idea: an eye as stacked semi-transparent layers rather than one painted image.
The stages, in the order the code applies them:

| Stage | What the paper says | How it is done here |
|---|---|---|
| Fibre field | Furrows run from the pupil outwards, interlaced | A smooth periodic field of angular lean, `drift` columns per row |
| Background | Markov random field seeded with a patch of a real iris | Line integral convolution of coarse noise along the fibre field |
| Radial furrows | Radial lines, control points randomly perturbed, interpolated with periodic cubic splines, textured by LIC | Gaussian ridges along the fibre field, each with its own width, strength and reach, positions drawn at random |
| Collarette | A zig-zag circle 20–30 px out from the pupil; furrows are kept only within it | `collar_at` with a spline wobble; the furrows fade out at it, and it lifts as a ridge |
| Concentric furrows | Arcs of circles just inside the iris rim, darker than the background | 3–5 darker arcs of limited angular extent |
| Crypts | 1–10 perturbed-spline blobs on the collarette periphery, darker, then smoothed | 4–10 Gaussian blobs with a spline-wobbled width |
| Surface | Noise and light reflection | Pigment flecks, pixel grain, a cross-fibre softening pass |

**Line integral convolution** is the step worth understanding, because it is
what makes fibres look like fibres. The intensity at a point is a low-pass
filtered integral of a noise image taken *along* the furrow, so the result is
fine across a fibre and smooth down its length — exactly the anisotropy a
brushed texture has and isotropic noise does not. The paper filters with a
kernel of L = 25 pixels either way. Here the field is known analytically
instead of traced, so the streamline is walked a row at a time with the
angular wander accumulating as it goes.

Two departures from the paper, and they apply to any package built on this:

1. Its background comes from a Markov random field seeded with a patch of a
   real iris. There is rarely a real iris of the right animal to seed with, so
   the LIC field itself is the background; it carries the same directional
   grain.
2. The paper works on an unwrapped image of a photographed eye. This works in
   the texture space the renderer already samples, so there is no unwrapping
   and no rewrapping.

## Parameters

`IRIS_DEFAULTS` in `eye_textures.py` holds every knob with a note on what it
does; `synthesise_iris(rng, w, h, ramp, **overrides)` takes any of them and
raises on a name it does not know, so a typo fails loudly instead of silently
doing nothing. The ones that change the character most:

- `drift` — how far a fibre leans over the depth of the iris. Past about 1
  column per row the texture stops reading as a combed mesh and starts reading
  as wood grain.
- `lic_coarse` — columns per noise cell. 1 gives hairlines; a photographed iris
  is built of plumes several pixels wide, so the goat uses 3.
- `furrows`, `furrow_width`, `furrow_amount` — how many fibres, how wide, how
  strong. Many narrow fibres read as brushed metal; a few wide ones read as
  wood. 55 at 0.6–2.6 is a goat.
- `collar_at`, `smooth_zone` — where the pupillary zone ends. Inside the
  collarette an iris is smooth; the fibrous stroma shows outside it.
- `crypts`, `concentric` — how pitted and how ringed.

## Colour

Give `synthesise_iris()` a ramp from the rim (0) to the pupil edge (1), and
**keep it duller than the colour measured in a photograph**. The fibres put
light back into the texture, so a base that starts at the measured value ends
up reading bright and, for a warm iris, yellow. The goat's body colour is
(164, 128, 84) against a photograph that measures about (180, 143, 91).

A dark limbal ring at the rim is worth having even where a photograph barely
shows one: it is what separates the iris from the lids at the eye's edge.

## Eyelids

`elliptical_eyelids()` writes lids that hold the **opening** rather than the
lid, which gives a wide oval with round corners instead of an almond that
tapers to a point. Adafruit's own bitmaps hold the lid, painted from the frame
edge down to a curve, so both lids retract clear of the eye.

The cost of the opening convention is that the lids sit inside the eye even
wide open, so a package using them **must set `"tracking": false`**. Tracking
slides the lids with the gaze, one opening as the other closes, and against an
opening-shaped pair it drags them across the middle of the eye.

The loader reads the topmost and bottommost lit pixel of every column, so every
column needs at least one lit pixel or the lid tears open there. The closed
edge is an arc rather than a straight line, because the renderer interpolates
lid shape every frame and a flat lid closes like a shutter.

## Sizes

The renderer caps the sclera at 4096 bytes and gives the iris whatever heap is
left, so author both at the size that survives rather than letting the loader
point-sample them: 480×120 for the iris and 64×32 for the sclera are what the
generated packages use. `supersampled()` reduces an oversized sclera by
averaging, which keeps its mottling from aliasing.

## Starting a new package

1. Copy `make_goat_eye.py`, point `OUT` at the new package directory.
2. Set the colour ramps, the lid proportions and any `synthesise_iris()`
   overrides. Run it.
3. Write `config.eye`: pupil shape and size, `tracking: false` if the lids came
   from `elliptical_eyelids()`, and the texture filenames.
4. Look at it: `./sim/build/eye-sim --eye <id>` opens the preview, and
   `--frames N --out <path>` writes PNGs for comparing against a photograph.
   `--set KEY=VALUE` tries a config change without editing the file.
5. Put it on a device with `python tools/upload_package.py <device-ip> <id>`.
