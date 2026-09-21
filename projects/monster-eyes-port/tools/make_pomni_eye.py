#!/usr/bin/env python3
"""Generate the artwork for the Pomni eye package.

Pomni, from The Amazing Digital Circus, has eyes drawn rather than grown: a
white disc inside a heavy black outline, three straight lashes off the rim,
and an iris that is a six-wedge pinwheel of red and blue behind its own black
ring. Nothing about it is photographic, so this generator is all hard edges
and flat colour, where the goat's is fibres and grain.

WHICH TEXTURE HOLDS WHAT MATTERS, and it is the whole design here. The
eyeball's silhouette does not move on screen; what moves when the eye looks
about is the iris, sliding around inside it. So:

  * the sclera holds everything that must stay put -- her face, the lashes,
    the outline round the eye, and the white of the eye;
  * the iris holds only the pinwheel and its own black ring, which is what
    should move.

Painting the outline into the iris texture, which is the obvious thing to do
when the iris is made to cover the whole eye, makes the outline wander with
the gaze: measured at 25 px of travel on a 128 px eye, which reads as the eye
sliding off the face rather than looking anywhere.

Depths below are in each texture's own space -- 0 at its outer edge, 1 at its
inner one -- and they are not linear in screen pixels, because the map's outer
rows are the part of the sphere curving away. They were calibrated by
rendering banded textures and measuring where each band landed, for this
package's geometry (eyeRadius 125, irisRadius 45, coverage 0.6, eye 63 px):

    sclera depth 0.25 -> r 62     0.50 -> r 52     0.75 -> r 37
           depth 0.375-> r 58     0.625-> r 45     0.875-> r 28

The bands are the reference frames' proportions -- an eye 73 px in radius, a
5 px outline, an iris disc 40 px across -- put through that.

Run from anywhere; assets are written next to this script in
../data/eyes/pomni/.
"""

import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from eye_textures import elliptical_eyelids, write_bmp1, write_bmp24

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "eyes" / "pomni"

# The sclera carries the drawing, so it is authored far larger than an
# eyeball's flat white would need: the renderer caps it at 32 KB and this sits
# just inside, at 256 * 48 pixels of two bytes each once loaded. The radial
# rows are what matter here. At 24 the outline came out a single row thick,
# which rendered as a hairline with the lashes apparently floating clear of
# it; every band in this drawing needs a few rows to read as a band.
SCLERA_W, SCLERA_H = 256, 48
IRIS_W, IRIS_H = 240, 60
LID_SIZE = 240

# Sclera bands, outer edge inwards. The whole drawing sits well inside the
# eyeball rather than out at its rim, and that margin is the point: the sclera
# is painted on the eyeball, so it shifts a few pixels as the eye looks about,
# and lashes drawn any nearer the edge are cropped by the render box at the
# far end of a glance. Ten pixels of face outside the longest lash covers the
# travel measured here.
#
# Calibrated at irisRadius 42, in an eye 63 px in radius:
#
#     depth 0.42 -> r 59     0.63 -> r 48     0.83 -> r 35
#     depth 0.50 -> r 55     0.71 -> r 43     0.92 -> r 29
#     depth 0.58 -> r 51     0.75 -> r 40     0.96 -> r 26
FACE_BAND = 0.597         # Her face, and the lashes: r 63-50
RING_INNER = 0.660        # Black outline round the eye: r 50-46
# Inside that, white all the way to the iris at r 25. The outline is a
# twelfth of the eye's radius, as the reference frames have it.

# Three lashes, pointing OUT: they live in the face band, outside the eye's
# outline, which is why the face band is there at all. They are drawn at the
# BOTTOM of the texture, at angle 0.5, because the renderer already turns eye
# 0's iris half a turn -- the mirroring that makes a pair look like a pair --
# and the sclera start angle is set to match in config.eye, so this puts them
# up on the eye to the right of the panel and down on the eye to the left,
# which is how Pomni wears them.
# Spread wide enough to read as three separate lashes at 128 px: an eighth of
# the way round the eye between the outer two, which is about what the
# reference frames show.
LASH_ANGLES = (0.5 - 0.062, 0.5, 0.5 + 0.062)
# And splayed: each lash leans a little further out as it goes, the way a fan
# of lashes does, rather than three parallel spokes.
LASH_SPLAY = 0.008
LASH_ROOT = 0.620         # Into the outline, so they join it with no seam
LASH_TIP = 0.417          # Out to r 59, with the rest of the face beyond
LASH_WIDTH = 0.0090       # Finer than the outline, as a lash is

# Iris bands, outer edge inwards: the pinwheel's own ring, then the wedges.
IRIS_RING = 0.14
WEDGES = 6                # Three red and three blue, alternating
WEDGE_PHASE = 0.5 / WEDGES  # A wedge boundary at the top, as the show has it

RED = (222, 46, 62)
BLUE = (104, 106, 205)
WHITE = (252, 251, 249)   # The eyeball, a shade brighter than her face
FACE = (246, 243, 239)    # And the face, which the sketch also clears to
INK = (18, 14, 20)


def past(rows, edge, softness):
    """0 outside `edge` (towards the rim), 1 inside it, blended between."""
    return np.clip((rows - edge) / softness + 0.5, 0.0, 1.0)


def over(base, colour, t):
    """Paint `colour` where t is 0, keep `base` where it is 1."""
    return base * t[..., None] + np.array(colour, dtype=float) * (1.0 - t)[..., None]


def grid(width, height):
    x = (np.arange(width) + 0.5) / width
    y = (np.arange(height) + 0.5) / height
    return np.meshgrid(x, y)


def sclera_texture():
    """Face, lashes, outline and the white of the eye: everything that stays."""
    angles, rows = grid(SCLERA_W, SCLERA_H)
    soft = 1.2 / SCLERA_H

    rgb = np.broadcast_to(np.array(WHITE, dtype=float),
                          angles.shape + (3,)).copy()
    rgb = over(rgb, INK, past(rows, RING_INNER, soft))
    rgb = over(rgb, FACE, past(rows, FACE_BAND, soft))

    # Lashes: straight, so in polar space they are simply narrow bands at
    # fixed angles, rooted at the outline and tapering as they go out.
    lash = np.zeros_like(angles)
    for at in LASH_ANGLES:
        along = np.clip((rows - LASH_TIP) / (LASH_ROOT - LASH_TIP), 0.0, 1.0)
        # Lean away from the middle one, most at the tip and nothing at the
        # root, so the three fan out instead of running parallel.
        lean = np.sign(at - 0.5) * LASH_SPLAY * (1.0 - along)
        d = np.abs((angles - (at + lean) + 0.5) % 1.0 - 0.5)
        width = LASH_WIDTH * (0.25 + 0.75 * along)
        edge = np.clip((width - d) / (0.9 / SCLERA_W) + 0.5, 0.0, 1.0)
        # Between the root and the tip and nowhere else. Without the tip test
        # the taper bottoms out at a quarter width and the lash carries on to
        # the rim of the eyeball, where the render box crops it.
        span = (rows >= LASH_TIP) & (rows < LASH_ROOT)
        lash = np.maximum(lash, edge * span)
    rgb = over(rgb, INK, 1.0 - lash)

    return np.clip(rgb, 0, 255)


def iris_texture():
    """The pinwheel and its ring: the part that moves when she looks about."""
    angles, rows = grid(IRIS_W, IRIS_H)
    soft = 1.2 / IRIS_H

    wedge = np.floor(((angles + WEDGE_PHASE) % 1.0) * WEDGES).astype(int)
    rgb = np.where((wedge % 2 == 0)[..., None],
                   np.array(RED, dtype=float), np.array(BLUE, dtype=float))
    rgb = over(rgb, INK, past(rows, IRIS_RING, soft))
    return np.clip(rgb, 0, 255)


def main():
    write_bmp24(OUT / "iris.bmp", iris_texture())
    write_bmp24(OUT / "sclera.bmp", sclera_texture())
    # A round opening, wide enough to clear the lashes: Pomni's eye is a
    # circle and no lid shows until she blinks, when the lids come down over
    # it in her own skin colour.
    upper, lower = elliptical_eyelids(LID_SIZE, 1.0, 1.0)
    write_bmp1(OUT / "upper.bmp", upper)
    write_bmp1(OUT / "lower.bmp", lower)
    for name in ("iris.bmp", "sclera.bmp", "upper.bmp", "lower.bmp"):
        print(f"{OUT / name}: {(OUT / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
