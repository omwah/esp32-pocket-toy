#!/usr/bin/env python3
"""Generate the iris and sclera artwork for the Eye of Sauron package.

The renderer samples both textures in polar space: X is the angle around the
eye and Y runs inwards, so row 0 of the iris is its rim and the last row is the
pupil edge, while row 0 of the sclera is the rim of the eyeball and its last
row meets the iris. Authoring straight into that space is what keeps the fire
honest: every feature here is a function of the angle and of the distance from
the pupil, never of one twisted into the other, so a flame drawn as a band of
brightness ending at some radius arrives on the screen as a tongue pointing
straight out from the pupil. Nothing in the texture leans, curls or spirals,
and the package leaves irisSpin at zero, so the fire never turns around the
iris: it only reaches outward.

The palette and the layout come from measuring the reference footage -- the
film's eye and the stock loop of it -- frame by frame in polar coordinates
around the pupil:

  * a black slit pupil, upright, about four times as tall as it is wide;
  * a white-hot rim hugging the pupil, the brightest thing in the eye;
  * flames reaching out from there, orange at the root and thinning to
    ragged tips at different distances, so the fire has a torn edge;
  * left and right of the pupil the fire is markedly cooler -- deep blood
    red where the top and bottom are yellow-white. In the footage that is
    the most recognisable thing about the eye after the slit itself;
  * everything fading to black by the rim, with the sclera no more than a
    dim ember, so the eye reads as fire floating in the dark.

The package has no eyelid bitmaps at all. The loader treats a missing lid as
"fully out of the way" for both its open and its closed position, so the blink
timer still runs but moves nothing: the Eye does not blink, and no lid ever
crosses the fire.

Run from anywhere; assets are written next to this script in
../data/eyes/sauron/.
"""

import pathlib
import struct

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "eyes" / "sauron"

# Same budget reasoning as the other generated packages: the renderer caps the
# sclera at 4096 bytes and gives the iris whatever heap is left, so author both
# at the size that survives rather than letting the loader point-sample them.
# The iris is wide because the flames are narrow: at 480 columns a tongue two
# columns across is still under a degree of arc.
IRIS_W, IRIS_H = 480, 120
SCLERA_W, SCLERA_H = 64, 32
SCLERA_SS = 8

# Heat to colour. Black through the deep reds the sides of the eye are made
# of, up to the white-yellow of the rim of the pupil, sampled from the
# footage.
FIRE_RAMP = [
    (0.00, (0, 0, 0)),
    (0.10, (26, 2, 0)),
    (0.22, (88, 8, 2)),
    (0.35, (148, 22, 4)),
    (0.50, (204, 60, 8)),
    (0.65, (236, 110, 16)),
    (0.80, (250, 164, 40)),
    (0.92, (253, 214, 112)),
    (1.00, (255, 243, 196)),
]

# How much cooler the fire runs to the left and right of the pupil than above
# and below it. 0 would be an even ring of flame; at 0.62 the sides land in
# the deep reds of the ramp while the top and bottom reach the yellows.
SIDE_COOLING = 0.62

# Cooling on its own takes the sides nearly to black, and the footage has
# flame there still: dark, but plainly flame. This colour is added back at the
# sides in proportion to how bright the fire would have been, so every tongue
# keeps its shape and loses only its yellow.
SIDE_EMBER = (150, 14, 4)

# The tongues, as (cells around the eye, weight, shortest reach, longest
# reach). Reach is a fraction of the distance from the pupil to the rim.
# Coarse octaves make the broad sheets of flame, fine ones the filaments;
# because each has its own reach the tips end at different distances and the
# fire has a ragged edge rather than a hem.
FLAME_OCTAVES = [
    (17, 0.34, 0.30, 1.00),
    (37, 0.26, 0.26, 0.86),
    (79, 0.20, 0.22, 0.68),
    (157, 0.13, 0.18, 0.52),
    (311, 0.08, 0.12, 0.38),
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
    # Row 0 is the rim and the last row is the pupil, so distance travelled
    # out from the pupil runs backwards up the image.
    dist = 1.0 - rows

    # The package sets irisAngle to 0, which puts texture column 0 at the top
    # of the screen, so the sides of the pupil are a quarter and three
    # quarters of the way along. This is 1 there and 0 at top and bottom.
    sideways = np.abs(np.sin(angles * 2.0 * np.pi))

    # Flames. Every term is a function of the angle alone, scaled by a
    # function of the distance alone: that is what makes a tongue point
    # straight out from the pupil instead of leaning to one side.
    # The flames at the sides throw the furthest, which is why the eye looks
    # wider than it is tall in the footage even though those flames are the
    # dark ones.
    stretch = 1.0 + 0.28 * sideways

    body = np.zeros_like(angles)
    for cells, weight, near, far in FLAME_OCTAVES:
        reach = (near + (far - near) * angular_noise(rng, angles, cells)) * stretch
        brightness = 0.35 + 0.65 * angular_noise(rng, angles, cells)
        # 1 along the length of the tongue, falling to 0 over the last
        # quarter of it, which is its tip.
        taper = 0.25 * reach
        body += weight * brightness * smoothstep(reach, reach - taper, dist)

    # Filaments: fine bright threads drawn the whole length of the fire, so
    # the sheets of flame are not flat.
    strands = angular_noise(rng, angles, 311)
    body *= 0.80 + 0.40 * strands

    # A little way out from the collar the fire is at its hottest -- that is
    # where the footage peaks, measured ring by ring -- and it is spent by the
    # time it reaches the rim.
    envelope = np.exp(-(((dist - 0.16) / 0.46) ** 2))
    # A floor under the tongues so the sheets of flame join up instead of
    # standing apart as separate spikes.
    body = body + 0.06 * smoothstep(1.05, 0.55, dist)
    # The white-hot collar that hugs the pupil in every frame of the
    # reference, with just enough angular variation not to look printed on.
    collar = (0.98 - 0.14 * angular_noise(rng, angles, 37)) * np.exp(
        -((dist / 0.085) ** 2))

    # A haze of fire behind the tongues. Without it the gaps between them go
    # to black and the eye reads as a starburst; the footage has burning air
    # in those gaps, dimmer than the flames but never dark.
    haze = (0.30 * envelope * (0.7 + 0.3 * angular_noise(rng, angles, 13))
            * smoothstep(0.05, 0.30, dist))

    # Gains chosen so that only the collar and the brightest tongues reach the
    # white end of the ramp. On the board an eye is 128 px across, and
    # anything hotter than this floods the middle of it into a white
    # starburst: the fire has to spend most of its range in the oranges and
    # reds to read as fire at that size.
    heat = collar + haze + 0.72 * envelope * body

    # Licks: every ray brightens and dims along its length, at a spacing and a
    # phase of its own, so the fire has fronts in it instead of being a clean
    # starburst. The phase is a function of the angle alone, so a lick stays
    # on its ray -- nothing shears sideways into a swirl.
    phase = angular_noise(rng, angles, 53)
    heat *= 1.0 + 0.32 * np.sin(2.0 * np.pi * (dist * 2.6 + phase))

    # Cooler to the left and right. The flames are the same flames; they just
    # run lower down the ramp there, which is what turns them blood red while
    # the top and bottom stay yellow.
    # The collar itself stays hot the whole way round -- in the footage the
    # rim of the pupil is white on every side of it -- so the cooling only
    # takes hold once the flames are clear of it, and it lets go again at the
    # very tips, where the reference has the sideways flames as bright as any
    # other. The wedge is wider and deeper than a strict reading of the
    # footage asks for, because at 128 px a subtle one disappears: on the
    # board it has to be unmistakably two dark red flanks.
    sides = (sideways ** 1.6 * smoothstep(0.05, 0.20, dist)
             * smoothstep(1.05, 0.80, dist))
    rgb = ramp(FIRE_RAMP, heat * (1.0 - SIDE_COOLING * sides))
    rgb += (np.clip(heat, 0.0, 1.0) * sides)[..., None] * np.array(SIDE_EMBER)

    return np.clip(rgb, 0, 255)


def sclera_texture(seed=101):
    """A dim ember ring: the last of the fire, dying before the eyeball rim."""
    rng = np.random.default_rng(seed)
    w, h = SCLERA_W * SCLERA_SS, SCLERA_H * SCLERA_SS
    x = (np.arange(w) + 0.5) / w
    y = (np.arange(h) + 0.5) / h
    angles, rows = np.meshgrid(x, y)

    sideways = np.abs(np.sin(angles * 2.0 * np.pi))
    # Row 0 is the eyeball rim, the last row meets the iris.
    # Dim enough to meet the spent outer rows of the iris without a seam: the
    # boundary between the two textures should be invisible.
    heat = 0.12 * rows ** 3.0
    heat *= 0.55 + 0.45 * angular_noise(rng, angles, 29)
    heat *= 1.0 - SIDE_COOLING * sideways ** 2.0

    big = np.clip(ramp(FIRE_RAMP, heat), 0, 255)
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


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_bmp24(OUT / "iris.bmp", iris_texture())
    write_bmp24(OUT / "sclera.bmp", sclera_texture())
    for name in ("iris.bmp", "sclera.bmp"):
        print(f"{OUT / name}: {(OUT / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
