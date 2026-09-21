#!/usr/bin/env python3
"""Generate the iris and sclera artwork for the deer eye package.

The renderer samples both textures in polar space: X is the angle around the
eye and Y runs inwards, so row 0 of the iris is its rim and the last row is
the pupil edge, while row 0 of the sclera is the rim of the eyeball and its
last row meets the iris.

The package used to fake a horizontal pupil by painting two black lobes into
the bottom rows of the iris, at the angles left and right of centre: with a
round pupil that is the only way to widen it sideways. The renderer now builds
a horizontal slit itself (`slitPupilHorizontal` in config.eye), so the texture
no longer carries a pupil at all and is free to be iris all the way down --
which is what lets the fibres run right up to the pupil edge the way they do
in a real eye.

The iris used to be one flat brown with stripes of angular noise over it,
which read as a sunburst rather than as an eye: a stripe of constant width
running the whole depth of the iris is not what a fibre looks like. It is now
built by tools/eye_textures.py, which implements the feature-agglomeration
model of Shah and Ross (ICIP 2006) -- see that module for the citations. The
same model draws the goat.

Deer-specific choices, against that model's defaults, which were tuned on the
goat:

  * a browner, darker body colour, measured in photographs of a sika doe and
    a red deer;
  * coarser, softer fibres, and fewer of them: a deer's iris is smoother than
    a goat's and its stroma less combed;
  * a weaker collarette and shallower crypts, for the same reason;
  * no lid shading. The original artwork deliberately had no darkening towards
    the rim and no shading from one side to the other, and that reads well on
    a dark eye where the goat's does not.

The pupil is photographed as a dark blue-grey rather than black. Deer sclera
barely shows and is brown rather than white, so it is near black here.

The eyelids are generated here too, and they break with Adafruit's own
convention deliberately. Their bitmaps hold the LID: white from the edge of
the frame down to a curve, which makes the open position the frame edge, so
both lids retract clear of the eye and the eyeball shows as a full circle.
These hold the OPENING: an ellipse, so the eye keeps a deer's oval shape with
round ends rather than the points the migrated almond came to.

The cost of that choice is that these lids sit inside the eye even wide open,
so the package must set "tracking": false. Tracking slides the lids with the
gaze -- one opens as the other closes -- and against an opening-shaped pair
it drags them across the middle of the eye instead of merely narrowing it.

Run from anywhere; assets are written next to this script in
../data/eyes/deer/.
"""

import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from eye_textures import (angular_noise, elliptical_eyelids, ramp,
                          supersampled, synthesise_iris, write_bmp1,
                          write_bmp24)

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "eyes" / "deer"

# Same budget reasoning as the other generated packages: the renderer caps the
# sclera at 4096 bytes for a package that does not ask for more, and gives the
# iris whatever heap is left, so author both at the size that survives rather
# than letting the loader point-sample them.
IRIS_W, IRIS_H = 480, 120
SCLERA_W, SCLERA_H = 64, 32
SCLERA_SS = 8
LID_SIZE = 240

# The opening, as fractions of the eye's half-width. A deer's eye is a wide
# oval; an ellipse gives it round ends, where the almond the package carried
# before came to a point at each corner.
LID_HALF_WIDTH = 0.97
LID_HALF_HEIGHT = 0.66

# Brown, and dark. The body colour is the one measured in both photographs;
# the rim is only a little deeper, because a deer's iris does not carry the
# hard limbal ring a goat's does, and the base is duller than the measurement
# since the fibres put light back into it.
IRIS_RAMP = [
    (0.00, (58, 40, 30)),     # Rim, barely darker than the body
    (0.15, (92, 66, 50)),
    (0.45, (106, 76, 58)),    # Body colour
    (0.80, (112, 80, 62)),
    (1.00, (100, 70, 52)),    # A shade deeper at the pupil edge
]

SCLERA_RAMP = [
    (0.00, (8, 6, 5)),        # Outer edge of the eyeball
    (0.55, (17, 12, 9)),
    (1.00, (28, 19, 13)),     # Meeting the iris
]


def iris_texture(seed=20260919):
    rng = np.random.default_rng(seed)
    return np.clip(synthesise_iris(
        rng, IRIS_W, IRIS_H, IRIS_RAMP,
        # Coarser and calmer than the goat: fewer fibres, wider, and leaning
        # less, because a deer's stroma is smoother and less combed.
        furrows=34,
        furrow_width=(1.1, 3.4),
        furrow_amount=0.22,
        drift=0.35,
        lic_coarse=4,
        lic_amount=0.13,
        # Barely a collarette, and the pupillary zone is not the smooth patch
        # it is on a goat.
        collar_lift=0.07,
        collar_width=0.13,
        smooth_floor=0.65,
        # Shallow crypts, and few of them.
        crypts=(2, 6),
        crypt_depth=(0.18, 0.34),
        concentric=(2, 4),
        concentric_depth=(0.06, 0.13),
        # A dark iris shows less hue variation than a pale one.
        hue_warm=0.06,
        hue_cool=0.10,
    ), 0, 255)


def sclera_texture(seed=7):
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
