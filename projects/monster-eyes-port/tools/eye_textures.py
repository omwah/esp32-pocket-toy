#!/usr/bin/env python3
"""Shared pieces for generating eye package artwork.

Everything here works in the renderer's own polar texture space: X is the
angle around the eye, 0 to 1 and wrapping, and Y runs inwards, so row 0 of an
iris texture is its rim and the last row is the pupil edge. Authoring straight
into that space is what keeps a feature honest -- a band of brightness ending
at some depth arrives on screen as a ring, and a stripe at some angle arrives
as a spoke pointing straight out from the pupil.

The iris synthesiser follows a published model rather than an invented one:

  Samir Shah and Arun Ross, "Generating Synthetic Irises by Feature
  Agglomeration", ICIP 2006. An iris is a background texture with anatomical
  features laid over it in turn -- radial furrows drawn as randomly perturbed
  spline paths and textured by line integral convolution, a collarette as a
  zig-zag circle the furrows stop at, concentric furrows as darker arcs in the
  ciliary zone, and one to ten crypts as dark blobs around the collarette.

  Andrew Lefohn et al., "An Ocularist's Approach to Human Iris Synthesis",
  IEEE CG&A 2003, is the other half of the idea: an eye as stacked
  semi-transparent layers rather than one painted image.

Two departures from the paper, and the same two apply to any package built on
this module. Its background texture comes from a Markov random field seeded
with a patch of a real iris; there is rarely a real iris of the right animal
to seed with, so the background here is the line-integral-convolution field
itself, which carries the same directional grain. And the paper works on an
unwrapped image of a photographed eye, where this works in the texture space
the renderer samples, so nothing has to be unwrapped or rewrapped.

tools/make_goat_eye.py is the worked example. tools/make_deer_eye.py and
tools/make_sauron_eye.py predate this module and carry their own copies of the
helpers; they are left alone deliberately, because regenerating their artwork
to prove a refactor would change bitmaps that are already shipped and
validated. New packages should import this.
"""

import struct

import numpy as np

# ---------------------------------------------------------------------------
#  Fields and curves
# ---------------------------------------------------------------------------


def ramp(anchors, t):
    """Piecewise-linear colour ramp.

    @param anchors [(stop, (r, g, b)), ...], stops ascending from 0 to 1.
    @param t       Array of positions.
    @return        Array of colours, t's shape plus a trailing 3.
    """
    stops = np.array([s for s, _ in anchors])
    colors = np.array([c for _, c in anchors], dtype=float)
    t = np.clip(t, 0.0, 1.0)
    idx = np.clip(np.searchsorted(stops, t) - 1, 0, len(stops) - 2)
    lo, hi = stops[idx], stops[idx + 1]
    f = ((t - lo) / (hi - lo))[..., None]
    return colors[idx] * (1.0 - f) + colors[idx + 1] * f


def smoothstep(edge0, edge1, x):
    """Hermite fade from 0 at edge0 to 1 at edge1; edge1 may be the lower."""
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def angular_noise(rng, angles, cells):
    """Value noise around the eye, periodic so the bitmap wraps cleanly."""
    values = rng.random(cells)
    f = (angles % 1.0) * cells
    i = np.floor(f).astype(int)
    t = (1.0 - np.cos((f - i) * np.pi)) * 0.5
    return values[i % cells] * (1.0 - t) + values[(i + 1) % cells] * t


def periodic_spline(rng, angles, knots, smooth=2):
    """A smooth random curve that closes on itself.

    Shah and Ross interpolate randomly perturbed control points with periodic
    cubic splines, for furrow paths, the collarette and the crypt outlines.
    Catmull-Rom through the knots is the same curve family and needs no
    solver, which keeps these scripts to numpy.
    """
    values = rng.random(knots)
    for _ in range(smooth - 1):  # Gentler wander than white knots give
        values = (values + np.roll(values, 1) + np.roll(values, -1)) / 3.0
    f = (angles % 1.0) * knots
    i = np.floor(f).astype(int)
    t = f - i
    p0 = values[(i - 1) % knots]
    p1 = values[i % knots]
    p2 = values[(i + 1) % knots]
    p3 = values[(i + 2) % knots]
    t2, t3 = t * t, t * t * t
    return 0.5 * ((2 * p1) + (-p0 + p2) * t
                  + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                  + (-p0 + 3 * p1 - 3 * p2 + p3) * t3)


def angular_delta(a, b):
    """Signed distance between two angles on the unit circle."""
    return (a - b + 0.5) % 1.0 - 0.5


def line_integral_convolution(noise, drift, length):
    """Smear white noise along the fibre field.

    This is what gives the furrows their grain in Shah and Ross: the intensity
    at a point is a low-pass filtered integral of a noise image taken ALONG
    the furrow, so the result is fine across a fibre and smooth down its
    length. Their kernel runs L = 25 pixels either way.

    Here the field is known analytically -- fibres run from the pupil to the
    rim, wandering in angle by `drift` columns per row -- so the streamline is
    walked a row at a time instead of being traced.

    @param noise  White noise, rows by columns.
    @param drift  Angular wander per row, in columns, same shape.
    @param length Half-length of the kernel, in rows.
    @return       Field of the same shape.
    """
    h, w = noise.shape
    total = np.zeros_like(noise)
    weight = 0.0
    cols = np.arange(w)[None, :]
    for step in range(-length, length + 1):
        k = 0.5 * (1.0 + np.cos(np.pi * step / (length + 1.0)))  # Raised cosine
        rows = np.clip(np.arange(h)[:, None] + step, 0, h - 1)
        # Wander accumulates along the walk, which is what bends a fibre.
        shift = np.rint(drift * step).astype(int)
        total += k * noise[rows, (cols + shift) % w]
        weight += k
    return total / weight


# ---------------------------------------------------------------------------
#  Iris synthesis
# ---------------------------------------------------------------------------

# Defaults are the goat's, which is the package these were tuned on. Every one
# of them is worth moving for a different animal; the notes say what each does
# and which way to push it.
IRIS_DEFAULTS = dict(
    # Fibre field
    drift_knots=37,        # Sectors of lean around the eye
    drift=0.5,             # Columns of lean per row. Much more reads as wood
    lic_length=35,         # Kernel half-length, rows. Longer = smoother fibres
    lic_coarse=3,          # Columns per noise cell. 1 gives hairlines
    lic_amount=0.17,       # Strength of the background grain
    # Zones
    collar_at=0.62,        # Depth of the collarette, 0 rim .. 1 pupil
    collar_wobble=0.05,    # How far it strays from a circle
    collar_knots=23,
    collar_width=0.09,
    collar_lift=0.18,      # How much brighter the ridge itself is
    smooth_zone=(0.80, 0.45),  # Grain fades to this side of the collarette
    smooth_floor=0.35,     # Grain left in the pupillary zone
    # Radial furrows
    furrows=55,
    furrow_width=(0.6, 2.6),   # Angular width, in units of 1 / furrows
    furrow_amount=0.30,
    furrow_reach=(0.30, 0.95), # How far in from the rim each one carries
    sector_knots=11,       # Broad heavier and thinner stretches of stroma
    # Concentric furrows
    concentric=(3, 6),     # How many
    concentric_at=(0.08, 0.34),
    concentric_depth=(0.10, 0.22),
    # Crypts
    crypts=(4, 11),
    crypt_width=(0.010, 0.032),
    crypt_height=(0.03, 0.09),
    crypt_depth=(0.35, 0.60),
    # Surface
    grain=0.07,
    fleck_rate=0.0035,     # Fraction of pixels that are pigment flecks
    hue_knots=13,
    hue_warm=0.10,         # Red gained where the iris runs warm
    hue_cool=0.16,         # Blue lost there
)


def synthesise_iris(rng, width, height, ramp_anchors, **overrides):
    """Build an iris texture by feature agglomeration.

    @param rng          numpy Generator; seed it for reproducible artwork.
    @param width        Texture width, the angle axis.
    @param height       Texture height, rim to pupil.
    @param ramp_anchors Colour ramp from rim (0) to pupil edge (1), the base
                        the features are laid over. Keep it duller than the
                        colour measured in a photograph: the fibres add light
                        back, and starting at the measured value reads bright.
    @param overrides    Any key of IRIS_DEFAULTS.
    @return             Float array, height by width by 3, unclipped.
    """
    p = dict(IRIS_DEFAULTS)
    unknown = set(overrides) - set(p)
    if unknown:
        raise TypeError(f"unknown iris parameters: {sorted(unknown)}")
    p.update(overrides)

    x = (np.arange(width) + 0.5) / width
    y = (np.arange(height) + 0.5) / height
    angles, rows = np.meshgrid(x, y)

    rgb = ramp(ramp_anchors, rows)

    # --- Fibre field -------------------------------------------------------
    # Every fibre runs pupil to rim, wandering in angle as it goes. The wander
    # is smooth around the eye so neighbouring fibres stay roughly parallel,
    # which is what makes them read as a combed mesh rather than as noise.
    drift = (periodic_spline(rng, angles, p["drift_knots"]) - 0.5) * p["drift"]

    # --- Background: LIC of white noise along that field -------------------
    # The noise is coarsened across the eye first: white noise at full
    # resolution gives hairlines, where a photographed iris is built of plumes
    # several pixels wide.
    coarse = p["lic_coarse"]
    noise = np.repeat(rng.random((height, width // coarse)), coarse, axis=1)
    lic = line_integral_convolution(noise, drift, p["lic_length"])
    lic = (lic - lic.mean()) / (lic.std() + 1e-6)
    # The pupillary zone, inside the collarette, is the smooth part of an
    # iris: the fibrous stroma shows in the ciliary zone outside it.
    zone = smoothstep(p["smooth_zone"][0], p["smooth_zone"][1], rows)
    floor = p["smooth_floor"]
    rgb = rgb * (1.0 + p["lic_amount"] * lic * (floor + (1.0 - floor) * zone))[..., None]

    # --- Collarette --------------------------------------------------------
    collar_at = p["collar_at"] + p["collar_wobble"] * (
        periodic_spline(rng, angles, p["collar_knots"]) - 0.5)
    collar = np.exp(-((rows - collar_at) / p["collar_width"]) ** 2)

    # --- Radial furrows ----------------------------------------------------
    # Ridges along the fibre field, each with its own width, strength and
    # reach, and each stopping where it meets the collarette. Positions are
    # drawn at random rather than spread evenly, so the eye has combed sectors
    # and bare ones the way a photographed iris does.
    count = p["furrows"]
    furrows = np.zeros_like(angles)
    starts = rng.random(count)
    w0, w1 = p["furrow_width"]
    widths = (w0 + (w1 - w0) * rng.random(count)) / count
    powers = rng.random(count)
    r0, r1 = p["furrow_reach"]
    reaches = r0 + (r1 - r0) * rng.random(count)
    for i in range(count):
        # Wander accumulated from the rim down to this row, in angle units.
        path = starts[i] + drift * (rows * height) / width
        d = angular_delta(angles, path) / widths[i]
        profile = np.exp(-d * d * 4.0)
        alive = (smoothstep(collar_at + 0.04, collar_at - 0.10, rows)
                 * smoothstep(0.0, 0.12, rows)
                 * smoothstep(reaches[i], reaches[i] - 0.25, rows))
        furrows += profile * alive * (powers[i] - 0.35)
    sector = 0.55 + 0.9 * periodic_spline(rng, angles, p["sector_knots"])
    rgb = rgb * (1.0 + p["furrow_amount"]
                 * np.clip(furrows * sector, -1.0, 1.0))[..., None]

    # The ridge itself, once the furrows have been drawn against it.
    rgb = rgb * (1.0 + p["collar_lift"] * collar)[..., None]

    # --- Concentric furrows ------------------------------------------------
    # Darker arcs in the ciliary zone, part way round the eye.
    for _ in range(rng.integers(*p["concentric"])):
        at = rng.uniform(*p["concentric_at"])
        centre = rng.random()
        extent = rng.uniform(0.15, 0.45)
        depth = rng.uniform(*p["concentric_depth"])
        radial = np.exp(-((rows - at) / 0.035) ** 2)
        span = smoothstep(extent, extent * 0.4,
                          np.abs(angular_delta(angles, centre)))
        rgb = rgb * (1.0 - depth * radial * span)[..., None]

    # --- Crypts ------------------------------------------------------------
    # Thinnings that show the dark layer behind, scattered around the
    # collarette. The paper's spline outline comes out of a perturbed radius.
    for _ in range(rng.integers(*p["crypts"])):
        centre = rng.random()
        at = float(np.mean(collar_at)) + rng.uniform(-0.16, 0.10)
        wide = rng.uniform(*p["crypt_width"])
        tall = rng.uniform(*p["crypt_height"])
        wobble = 1.0 + 0.35 * (periodic_spline(rng, angles, 17) - 0.5)
        da = angular_delta(angles, centre) / (wide * wobble)
        dr = (rows - at) / tall
        blob = np.exp(-(da * da + dr * dr) * 2.2)
        rgb = rgb * (1.0 - rng.uniform(*p["crypt_depth"]) * blob)[..., None]

    # --- Surface -----------------------------------------------------------
    # Soften across the fibres before the grain goes on: photographed through
    # a cornea nothing in an iris has a hard edge, and the ridges above are
    # drawn as clean Gaussians.
    rgb = (rgb + np.roll(rgb, 1, axis=1) + np.roll(rgb, -1, axis=1)) / 3.0
    rgb[1:-1] = (rgb[1:-1] * 2.0 + rgb[:-2] + rgb[2:]) / 4.0

    # Pigment flecks and the pixel-level speckle every photographed iris has.
    fleck = np.clip(rng.random(angles.shape) - (1.0 - p["fleck_rate"]),
                    0.0, None) * (120.0 / max(p["fleck_rate"], 1e-6) * 0.0035)
    grain = rng.random(angles.shape) - 0.5
    rgb = rgb * (1.0 + p["grain"] * grain)[..., None] - fleck[..., None]

    # Hue varies sector to sector: some of an iris runs green-grey, some warm,
    # which is what stops one colour reading as a flat wash.
    hue = periodic_spline(rng, angles, p["hue_knots"]) - 0.5
    rgb[..., 0] *= 1.0 + p["hue_warm"] * hue
    rgb[..., 2] *= 1.0 - p["hue_cool"] * hue

    return rgb


def lid_shading(angles, rows, top_angle, shade=0.26, sky=0.22):
    """Light from above: shadow under the lid, sky reflected in the cornea.

    @param angles    Angle field.
    @param rows      Depth field, 0 rim to 1 pupil.
    @param top_angle Where the top of the eye falls on the angle axis. MEASURE
                     it -- paint a band at a known angle, render, and see
                     where it lands -- because it depends on the renderer's
                     own angle convention, not on anything in the texture.
    @param shade     How much darker the top third goes.
    @param sky       How much bluer the reflection runs.
    @return          Multiplier, the field's shape plus a trailing 3.
    """
    dist = 1.0 - rows
    dark = np.clip(np.cos((angles - top_angle) * 2.0 * np.pi), 0.0, 1.0) ** 1.5
    cool = np.cos((angles - top_angle) * 2.0 * np.pi) * 0.5 + 0.5
    cool = (cool ** 3) * smoothstep(0.0, 0.5, dist)
    mul = np.ones(angles.shape + (3,))
    mul *= (1.0 - shade * dark)[..., None]
    mul[..., 0] *= 1.0 - 0.10 * cool
    mul[..., 2] *= 1.0 + sky * cool
    return mul


# ---------------------------------------------------------------------------
#  Eyelids
# ---------------------------------------------------------------------------


def elliptical_eyelids(size, half_width, half_height, overshoot=2.0, bulge=5.0):
    """Lid masks for an eye whose opening is an ellipse.

    Adafruit's own eyelid bitmaps hold the LID: painted from the edge of the
    frame down to a curve, so both lids retract out of the eye when open and
    the eyeball shows as a full circle. These hold the OPENING instead, which
    is what gives a deer or a goat its wide oval with round corners rather
    than an almond that tapers to a point.

    The cost is that the lids sit inside the eye even wide open, so a package
    using these must set "tracking": false. Tracking slides the lids with the
    gaze, one opening as the other closes, and against an opening-shaped pair
    it drags them across the middle of the eye.

    The loader reads the topmost and bottommost lit pixel of every column, so
    every column needs at least one lit pixel: without one the loader keeps
    its wide-open default there and the lid tears open.

    @param size       Bitmap side, pixels.
    @param half_width  Opening half-width, as a fraction of the eye's.
    @param half_height Opening half-height, likewise.
    @param overshoot  How far past the middle a closing lid goes, pixels, so
                      the two meet rather than leaving a seam.
    @param bulge      Arc depth of the closed edge, pixels. A flat lid closes
                      like a shutter.
    @return           (upper, lower) boolean masks.
    """
    n = size
    centre = n * 0.5
    xs = (np.arange(n) + 0.5 - centre) / (centre * half_width)
    # Outside the ellipse the opening has closed; inside, this is its height.
    half = np.sqrt(np.clip(1.0 - xs * xs, 0.0, 1.0)) * centre * half_height

    open_top = np.round(centre - half).astype(int)
    open_bottom = np.round(centre + half).astype(int)
    arc = np.sqrt(np.clip(1.0 - xs * xs, 0.0, 1.0)) * bulge
    closed_upper = np.clip(np.round(centre + overshoot + arc), 0, n - 1).astype(int)
    closed_lower = np.clip(np.round(centre - overshoot - arc), 0, n - 1).astype(int)

    upper = np.zeros((n, n), dtype=bool)
    lower = np.zeros((n, n), dtype=bool)
    for col in range(n):
        top = open_top[col]
        upper[top:max(closed_upper[col], top) + 1, col] = True
        bottom = open_bottom[col]
        lower[min(closed_lower[col], bottom):bottom + 1, col] = True
    return upper, lower


# ---------------------------------------------------------------------------
#  Bitmaps
# ---------------------------------------------------------------------------


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


def supersampled(field, factor):
    """Average a field down by `factor` in both axes.

    The sclera is capped at 4096 bytes, so it is authored large and reduced
    rather than sampled sparsely, which would alias its mottling.
    """
    h, w, c = field.shape
    return field.reshape(h // factor, factor, w // factor, factor, c).mean((1, 3))
