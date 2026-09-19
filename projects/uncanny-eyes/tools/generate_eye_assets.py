"""Generate firmware tables from the editable PNG eye sources.

The PNGs in assets/ are the source of truth. This file is both a normal Python
program and a PlatformIO pre-build script. Generated files are intentionally
not committed; run this script whenever an asset changes.
"""
from pathlib import Path
import sys
from PIL import Image

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parents[1]
else:  # SCons executes extra_scripts without defining __file__
    Import("env")
    ROOT = Path(env["PROJECT_DIR"])
ASSETS = ROOT / "assets"
OUT = ROOT / "generated"
STYLES = [
    ("defaultEye", "Hazel"), ("dragonEye", "Dragon"),
    ("noScleraEye", "No sclera"), ("goatEye", "Goat / Krampus"),
    ("newtEye", "Newt"), ("terminatorEye", "Terminator"),
    ("catEye", "Cartoon cat"), ("owlEye", "Owl"),
    ("naugaEye", "Nauga"), ("doeEye", "Realistic deer"),
    ("animeEye", "Big Anime"),
]

def rgb565(pixel):
    r, g, b = pixel[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def load(path, mode):
    image = Image.open(path).convert(mode)
    if mode == "RGB": values = [rgb565(p) for p in image.getdata()]
    else: values = list(image.getdata())
    return image.width, image.height, values

def emit_array(fp, ctype, name, values, columns):
    fp.write(f"const {ctype} {name}[] PROGMEM = {{\n")
    for i in range(0, len(values), columns):
        row = values[i:i + columns]
        fmt = "0x%04X" if ctype == "uint16_t" else "0x%02X"
        fp.write("  " + ", ".join(fmt % v for v in row) + ",\n")
    fp.write("};\n\n")

def generate():
    OUT.mkdir(exist_ok=True)
    header = OUT / "eye_assets.h"
    source = OUT / "eye_assets.cpp"
    header.write_text("""// Generated from assets/*.png by tools/generate_eye_assets.py. DO NOT EDIT.\n#pragma once\n#include <Arduino.h>\nstruct EyeAsset {\n  const char *name;\n  const uint16_t *sclera; uint16_t scleraW, scleraH;\n  const uint16_t *iris; uint16_t irisW, irisH;\n  const uint8_t *upper; const uint8_t *lower; uint16_t lidW, lidH;\n};\nextern const EyeAsset EYE_ASSETS[];\nextern const uint8_t EYE_ASSET_COUNT;\n""")
    records = []
    with source.open("w") as fp:
        fp.write('// Generated from assets/*.png. DO NOT EDIT.\n#include "eye_assets.h"\n\n')
        for index, (folder, title) in enumerate(STYLES):
            base = ASSETS / folder
            sw, sh, sclera = load(base / "sclera.png", "RGB")
            iw, ih, iris = load(base / "iris.png", "RGB")
            upper_path = base / "lid-upper.png"
            lower_path = base / "lid-lower.png"
            lw, lh, upper = load(upper_path, "L")
            lw2, lh2, lower = load(lower_path, "L")
            if (lw, lh) != (lw2, lh2):
                raise ValueError(f"lid dimensions differ in {folder}")
            prefix = f"eye{index}"
            emit_array(fp, "uint16_t", prefix + "Sclera", sclera, 12)
            emit_array(fp, "uint16_t", prefix + "Iris", iris, 12)
            emit_array(fp, "uint8_t", prefix + "Upper", upper, 20)
            emit_array(fp, "uint8_t", prefix + "Lower", lower, 20)
            records.append((title, prefix, sw, sh, iw, ih, lw, lh))
        fp.write("const EyeAsset EYE_ASSETS[] = {\n")
        for title, p, sw, sh, iw, ih, lw, lh in records:
            fp.write(f'  {{"{title}", {p}Sclera, {sw}, {sh}, {p}Iris, {iw}, {ih}, '
                     f'{p}Upper, {p}Lower, {lw}, {lh}}},\n')
        fp.write("};\nconst uint8_t EYE_ASSET_COUNT = sizeof(EYE_ASSETS) / sizeof(EYE_ASSETS[0]);\n")
    print(f"Generated {header.relative_to(ROOT)} and {source.relative_to(ROOT)}")

# PlatformIO executes extra_scripts using SCons' Import; direct execution is
# useful for contributors and CI.
generate()
