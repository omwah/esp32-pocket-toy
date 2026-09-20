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

The iris is one flat brown, the body colour measured in photographs of a sika
doe and a red deer, with fibres for texture and nothing else: no darkening
towards the rim and no shading from one side to the other. The pupil is
photographed as a dark blue-grey rather than black. Deer sclera barely shows
and is brown rather than white, so it is near black here.

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
import struct

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "eyes" / "deer"

# Same budget reasoning as the other generated packages: the renderer caps the
# sclera at 4096 bytes and gives the iris whatever heap is left, so author both
# at the size that survives rather than letting the loader point-sample them.
IRIS_W, IRIS_H = 480, 120
SCLERA_W, SCLERA_H = 64, 32
SCLERA_SS = 8
LID_SIZE = 240

# The opening, as fractions of the eye's half-width. A deer's eye is a wide
# oval; an ellipse gives it round ends, where the almond the package carried
# before came to a point at each corner.
LID_HALF_WIDTH = 0.97
LID_HALF_HEIGHT = 0.66

# One brown for the whole iris, the body colour measured in both
# photographs. There is deliberately no ramp: no darkening towards the rim
# and no shading from one side to the other, so the iris reads as a single
# colour with only its fibres breaking it up.
IRIS_RGB = (112, 80, 62)


SCLERA_RAMP = [
    (0.00, (8, 6, 5)),        # Outer edge of the eyeball
    (0.55, (17, 12, 9)),
    (1.00, (28, 19, 13)),     # Meeting the iris
]


def ramp(anchors, t):
    stops = np.array([s for s, _ in anchors])
    colors = np.array([c for _, c in anchors], dtype=float)
    t = np.clip(t, 0.0, 1.0)
    idx = np.clip(np.searchsorted(stops, t) - 1, 0, len(stops) - 2)
    lo, hi = stops[idx], stops[idx + 1]
    f = ((t - lo) / (hi - lo))[..., None]
    return colors[idx] * (1.0 - f) + colors[idx + 1] * f


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def angular_noise(rng, angles, cells):
    """Value noise around the eye, periodic so the bitmap wraps cleanly."""
    values = rng.random(cells)
    f = (angles % 1.0) * cells
    i = np.floor(f).astype(int)
    t = (1.0 - np.cos((f - i) * np.pi)) * 0.5
    return values[i % cells] * (1.0 - t) + values[(i + 1) % cells] * t


def iris_texture(seed=20260919):
    rng = np.random.default_rng(seed)
    x = (np.arange(IRIS_W) + 0.5) / IRIS_W
    y = (np.arange(IRIS_H) + 0.5) / IRIS_H
    angles, rows = np.meshgrid(x, y)
    # Row 0 is the rim, the last row is the pupil, so distance from the pupil
    # runs backwards up the image.
    dist = 1.0 - rows

    rgb = np.broadcast_to(np.array(IRIS_RGB, dtype=float),
                          angles.shape + (3,)).copy()

    # Fibres: stripes running out from the pupil, at three scales so they are
    # not evenly spaced. They run the full depth of the iris now that nothing
    # darkens its rim.
    fibre = (0.45 * angular_noise(rng, angles, 61)
             + 0.35 * angular_noise(rng, angles, 113)
             + 0.20 * angular_noise(rng, angles, 211))
    fibre = fibre - 0.5
    # Crypts: darker pits scattered through the middle of the iris.
    crypt = angular_noise(rng, angles, 29) - 0.5
    crypt = crypt * smoothstep(0.05, 0.35, dist) * smoothstep(0.95, 0.6, dist)

    rgb = rgb * (1.0 + 0.30 * fibre + 0.18 * crypt)[..., None]

    return np.clip(rgb, 0, 255)


def sclera_texture(seed=7):
    rng = np.random.default_rng(seed)
    w, h = SCLERA_W * SCLERA_SS, SCLERA_H * SCLERA_SS
    x = (np.arange(w) + 0.5) / w
    y = (np.arange(h) + 0.5) / h
    angles, rows = np.meshgrid(x, y)
    rgb = ramp(SCLERA_RAMP, rows)
    mottle = 1.0 + 0.30 * (angular_noise(rng, angles, 23) - 0.5)
    rgb = rgb * mottle[..., None]
    big = np.clip(rgb, 0, 255)
    return big.reshape(SCLERA_H, SCLERA_SS, SCLERA_W, SCLERA_SS, 3).mean((1, 3))


def write_bmp24(path, rgb):
    """Write a bottom-up 24-bit BMP, the only texture format the loader takes."""
    h, w, _ = rgb.shape
    row_size = (w * 3 + 3) & ~3
    pad = b"\0" * (row_size - w * 3)
    bgr = np.clip(rgb, 0, 255).astype(np.uint8)[:, :, ::-1]
    body = b"".join(bgr[y].tobytes() + pad for y in range(h - 1, -1, -1))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body),
                       2835, 2835, 0, 0)
    path.write_bytes(header + info + body)


def write_bmp1(path, mask):
    """Write a bottom-up 1-bit BMP; set bits are the lid, index 1 is white."""
    h, w = mask.shape
    row_size = ((w + 31) // 32) * 4
    packed = np.packbits(mask.astype(np.uint8), axis=1)
    rows = []
    for y in range(h - 1, -1, -1):
        row = packed[y].tobytes()
        rows.append(row + b"\0" * (row_size - len(row)))
    body = b"".join(rows)
    offset = 14 + 40 + 8
    header = struct.pack("<2sIHHI", b"BM", offset + len(body), 0, 0, offset)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 1, 0, len(body),
                       2835, 2835, 2, 2)
    palette = struct.pack("<4B4B", 0, 0, 0, 0, 255, 255, 255, 0)
    path.write_bytes(header + info + palette + body)


def eyelid_masks():
    """Lit pixels are the band each lid's edge sweeps between open and closed.

    The loader reads the topmost and bottommost lit pixel of every column, so
    for the upper lid the open edge is the top of the opening and the closed
    edge sits just past the middle; the lower lid is the mirror of that.

    The opening is an ellipse rather than an almond. At the corners an ellipse
    has a vertical tangent, so the lids meet there roundly and the eye keeps
    its width right to the edge instead of tapering to a slit. Every column
    still needs at least one lit pixel: without one the loader keeps its
    wide-open default for that column and the lid would tear open there.
    """
    n = LID_SIZE
    centre = n * 0.5
    xs = (np.arange(n) + 0.5 - centre) / (centre * LID_HALF_WIDTH)
    # Outside the ellipse the opening has closed; inside, this is its height.
    half = np.sqrt(np.clip(1.0 - xs * xs, 0.0, 1.0)) * centre * LID_HALF_HEIGHT

    open_top = np.round(centre - half).astype(int)
    open_bottom = np.round(centre + half).astype(int)
    # Closing overshoots the middle a little so the lids meet rather than
    # leaving a seam of iris between them, and the closed edge is an arc so a
    # half-blink looks like a lid and not a shutter.
    bulge = np.sqrt(np.clip(1.0 - xs * xs, 0.0, 1.0)) * 5.0
    closed_upper = np.clip(np.round(centre + 2.0 + bulge), 0, n - 1).astype(int)
    closed_lower = np.clip(np.round(centre - 2.0 - bulge), 0, n - 1).astype(int)

    upper = np.zeros((n, n), dtype=bool)
    lower = np.zeros((n, n), dtype=bool)
    for col in range(n):
        top = open_top[col]
        upper[top:max(closed_upper[col], top) + 1, col] = True
        bottom = open_bottom[col]
        lower[min(closed_lower[col], bottom):bottom + 1, col] = True
    return upper, lower


def main():
    write_bmp24(OUT / "iris.bmp", iris_texture())
    write_bmp24(OUT / "sclera.bmp", sclera_texture())
    upper, lower = eyelid_masks()
    write_bmp1(OUT / "upper.bmp", upper)
    write_bmp1(OUT / "lower.bmp", lower)
    for name in ("iris.bmp", "sclera.bmp", "upper.bmp", "lower.bmp"):
        print(f"{OUT / name}: {(OUT / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
