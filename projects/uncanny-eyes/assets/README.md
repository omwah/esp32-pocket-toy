# Eye source artwork

These PNG files are the editable source artwork from
[Adafruit Uncanny Eyes](https://github.com/adafruit/Uncanny_Eyes), by Phil
Burgess / Paint Your Dragon for Adafruit Industries. The upstream project is
released under the MIT license.

The `owlEye` eyelid PNGs were losslessly reconstructed from the arrays in
upstream `uncannyEyes/graphics/owlEye.h`, because upstream does not include that
design's original conversion directory. Its flat sclera and iris were replaced
with locally created feathered tissue and a textured golden owl iris. The
`doeEye` sclera and iris were similarly replaced with locally created realistic
deer artwork; its eyelid maps remain derived from upstream. `animeEye` is a
local large-eye design with a sapphire iris and oversized catchlights. All
other original directories are copied from upstream `convert/`.

The `m4*` directories adapt the ready-made artwork referenced by
[Adafruit Monster Eyes](https://github.com/adafruit/Adafruit_Monster_Eyes) from
[`M4_Eyes/eyes`](https://github.com/adafruit/Adafruit_Learning_System_Guides/tree/main/M4_Eyes/eyes).
The source artwork and `config.eye` files are MIT-licensed by Adafruit
Industries. Iris textures were resized for this display, and wide spherical
sclera textures were projected into square PNG maps. Smooth Uncanny Eyes lid
maps replace the source renderer's 1-bit sweep masks so blinking remains
progressive in this renderer.

Do not edit files under `generated/` or `data/`. After changing artwork,
PlatformIO regenerates compact FATFS eye files and uploads them with:

```sh
micromamba run -n platformio pio run -d projects/uncanny-eyes -t uploadfs
```

The PNG artwork remains the canonical source.
