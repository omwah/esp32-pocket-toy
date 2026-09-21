#!/usr/bin/env python3
"""Generate the iris and sclera artwork for the goat eye package.

Drawn from photographs of domestic goats (Jo Naylor's close-up on Wikimedia
Commons, CC BY 2.0, and "The eye of a male goat", CC BY-SA) rather than from
the grey-blue artwork the package carried before, which was a cat's eye under
another name: it had a vertical slit and no warmth in it at all.

What the photographs show:

- The pupil is a wide horizontal bar with blunt ends, reaching most of the way
  across the iris. It is the feature that says goat, and the renderer draws it
  from `slitPupilHorizontal` and `slitPupilRounded`, so no pupil is painted
  into this texture.
- The iris is light brown, browner towards a distinct dark limbal ring, with
  fine radial fibres and broad mottling but none of the deep crypts a human
  eye has. The base ramp below is duller than the colour measured mid-iris on
  purpose: the fibres put the light back, and starting at the measured value
  ends up reading as yellow.
- The lid shades the upper third of the iris a stop darker, with the sky
  reflected cool in the cornea above the pupil.
- There is effectively no white. The globe is iris nearly edge to edge, and
  what shows beyond it is dark wet tissue, so the sclera here is near black
  with a brown cast.

The fibre pattern is not invented: tools/eye_textures.py implements the
feature-agglomeration model of Shah and Ross (ICIP 2006) and this script
supplies the goat's parameters. See that module for the citations and for what
each parameter does.

The eyelids hold the OPENING rather than the lid, which keeps the eye a wide
oval with round corners. That needs "tracking": false in config.eye -- the
note there says why.

Run from anywhere; assets are written next to this script in
../data/eyes/goat/.
"""

import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from eye_textures import (elliptical_eyelids, angular_noise, lid_shading, ramp,
                          supersampled, synthesise_iris, write_bmp1,
                          write_bmp24)

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "eyes" / "goat"

# Same budget reasoning as the other generated packages: the renderer caps the
# sclera at 4096 bytes and gives the iris whatever heap is left, so author both
# at the size that survives rather than letting the loader point-sample them.
IRIS_W, IRIS_H = 480, 120
SCLERA_W, SCLERA_H = 64, 32
SCLERA_SS = 8
LID_SIZE = 240

# A goat's eye is wider and flatter than a deer's, but the opening is kept
# narrower than the eyeball on purpose: the iris is smaller than the eyeball so
# the eye has room to look around, and a wider opening would show that spare
# sclera as a dark band down each side even with the gaze centred. At 0.86 the
# lids sit on the iris at rest, and the dark corner only appears when the eye
# actually looks that way -- which is what the photographs show.
LID_HALF_WIDTH = 0.86
LID_HALF_HEIGHT = 0.60

# Where the top of the eye falls along the texture's X axis. Measured, not
# assumed: a band painted at 0.25 came out on the right of the rendered eye and
# one at 0.75 on the left, which puts the top at 0.0 and the bottom at 0.5.
TOP_ANGLE = 0.0

# Light brown, not yellow. Read down through the photographs: a dark ring at
# the rim, then brown, warming towards the pupil.
IRIS_RAMP = [
    (0.00, (46, 31, 19)),     # Limbal ring, where the iris meets the lids
    (0.09, (74, 50, 30)),     # Still ring: it is a band, not a hairline
    (0.22, (140, 106, 68)),
    (0.45, (164, 128, 84)),   # Body colour
    (0.80, (172, 136, 90)),
    (1.00, (156, 120, 76)),   # Deeper again at the pupil edge
]

SCLERA_RAMP = [
    (0.00, (10, 7, 5)),       # Outer edge of the eyeball
    (0.55, (20, 13, 9)),
    (1.00, (33, 22, 14)),     # Meeting the iris
]


def iris_texture(seed=20260920):
    rng = np.random.default_rng(seed)
    rgb = synthesise_iris(rng, IRIS_W, IRIS_H, IRIS_RAMP)

    x = (np.arange(IRIS_W) + 0.5) / IRIS_W
    y = (np.arange(IRIS_H) + 0.5) / IRIS_H
    angles, rows = np.meshgrid(x, y)
    rgb *= lid_shading(angles, rows, TOP_ANGLE)

    return np.clip(rgb, 0, 255)


def sclera_texture(seed=11):
    rng = np.random.default_rng(seed)
    w, h = SCLERA_W * SCLERA_SS, SCLERA_H * SCLERA_SS
    x = (np.arange(w) + 0.5) / w
    y = (np.arange(h) + 0.5) / h
    angles, rows = np.meshgrid(x, y)
    rgb = ramp(SCLERA_RAMP, rows)
    mottle = 1.0 + 0.30 * (angular_noise(rng, angles, 23) - 0.5)
    return supersampled(np.clip(rgb * mottle[..., None], 0, 255), SCLERA_SS)


def main():
    write_bmp24(OUT / "iris.bmp", iris_texture())
    write_bmp24(OUT / "sclera.bmp", sclera_texture())
    upper, lower = elliptical_eyelids(LID_SIZE, LID_HALF_WIDTH, LID_HALF_HEIGHT)
    write_bmp1(OUT / "upper.bmp", upper)
    write_bmp1(OUT / "lower.bmp", lower)
    for name in ("iris.bmp", "sclera.bmp", "upper.bmp", "lower.bmp"):
        print(f"{OUT / name}: {(OUT / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
