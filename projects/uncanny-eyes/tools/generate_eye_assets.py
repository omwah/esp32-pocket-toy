"""Generate compact FATFS eye files from editable PNG source artwork."""
from pathlib import Path
import struct
from PIL import Image

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parents[1]
else:
    Import("env")
    ROOT = Path(env["PROJECT_DIR"])
ASSETS = ROOT / "assets"
OUT = ROOT / "generated"
DATA = ROOT / "data" / "eyes"
STYLES = [
    ("defaultEye", "Hazel"), ("dragonEye", "Dragon"),
    ("noScleraEye", "No sclera"), ("goatEye", "Goat / Krampus"),
    ("newtEye", "Newt"), ("terminatorEye", "Terminator"),
    ("catEye", "Cartoon cat"), ("owlEye", "Owl"),
    ("naugaEye", "Nauga"), ("doeEye", "Realistic deer"),
    ("animeEye", "Big Anime"), ("m4BigBlue", "Big Blue"),
    ("m4Demon", "Demon"), ("m4DoomRed", "Doom Red"),
    ("m4DoomSpiral", "Doom Spiral"), ("m4Fish", "Fish"),
    ("m4Fizzgig", "Fizzgig"), ("m4HypnoRed", "Hypno Red"),
    ("m4Reflection", "Reflection"), ("m4Skull", "Skull"),
    ("m4SnakeGreen", "Snake Green"), ("m4Spikes", "Spikes"),
    ("m4ToonStripe", "Toon Stripe"),
]

def rgb565(pixel):
    r, g, b = pixel[:3]
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def load(path, mode):
    image = Image.open(path).convert(mode)
    values = ([rgb565(p) for p in image.getdata()] if mode == "RGB"
              else list(image.getdata()))
    return image.width, image.height, values

def generate():
    OUT.mkdir(exist_ok=True)
    DATA.mkdir(parents=True, exist_ok=True)
    records = []
    max_sclera = max_iris = max_lid = 0
    for index, (folder, title) in enumerate(STYLES):
        base = ASSETS / folder
        sw, sh, sclera = load(base / "sclera.png", "RGB")
        iw, ih, iris = load(base / "iris.png", "RGB")
        lw, lh, upper = load(base / "lid-upper.png", "L")
        lw2, lh2, lower = load(base / "lid-lower.png", "L")
        if (lw, lh) != (lw2, lh2):
            raise ValueError(f"lid dimensions differ in {folder}")
        path = f"/eyes/{index:02d}.eye"
        with (DATA / f"{index:02d}.eye").open("wb") as out:
            out.write(struct.pack("<4s6H", b"EYE1", sw, sh, iw, ih, lw, lh))
            out.write(struct.pack(f"<{len(sclera)}H", *sclera))
            out.write(struct.pack(f"<{len(iris)}H", *iris))
            out.write(bytes(upper)); out.write(bytes(lower))
        records.append((title, path, sw, sh, iw, ih, lw, lh))
        max_sclera = max(max_sclera, sw * sh)
        max_iris = max(max_iris, iw * ih)
        max_lid = max(max_lid, lw * lh)

    (OUT / "eye_assets.h").write_text("""// Generated metadata. DO NOT EDIT.\n#pragma once\n#include <Arduino.h>\nstruct EyeAssetInfo {\n  const char *name; const char *path;\n  uint16_t scleraW, scleraH, irisW, irisH, lidW, lidH;\n};\nextern const EyeAssetInfo EYE_ASSETS[];\nextern const uint8_t EYE_ASSET_COUNT;\nextern const uint32_t EYE_MAX_SCLERA_PIXELS;\nextern const uint32_t EYE_MAX_IRIS_PIXELS;\nextern const uint32_t EYE_MAX_LID_PIXELS;\n""")
    with (OUT / "eye_assets.cpp").open("w") as out:
        out.write('#include "eye_assets.h"\nconst EyeAssetInfo EYE_ASSETS[] = {\n')
        for title, path, sw, sh, iw, ih, lw, lh in records:
            out.write(f'  {{"{title}", "{path}", {sw}, {sh}, {iw}, {ih}, {lw}, {lh}}},\n')
        out.write("};\nconst uint8_t EYE_ASSET_COUNT = sizeof(EYE_ASSETS)/sizeof(EYE_ASSETS[0]);\n")
        out.write(f"const uint32_t EYE_MAX_SCLERA_PIXELS = {max_sclera};\n")
        out.write(f"const uint32_t EYE_MAX_IRIS_PIXELS = {max_iris};\n")
        out.write(f"const uint32_t EYE_MAX_LID_PIXELS = {max_lid};\n")
    print(f"Generated metadata and {len(records)} FATFS eye files")

generate()
