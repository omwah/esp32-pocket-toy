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
other directories are copied from upstream `convert/`.

Do not edit files under `generated/`. After changing artwork, regenerate it:

```sh
micromamba run -n platformio python tools/generate_eye_assets.py
```

PlatformIO also runs the generator automatically before every build.
