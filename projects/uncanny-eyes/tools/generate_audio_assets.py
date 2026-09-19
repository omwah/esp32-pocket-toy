"""Convert canonical 16 kHz mono PCM WAV files into firmware tables."""
from pathlib import Path
import wave, struct

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parents[1]
else:
    Import("env")
    ROOT = Path(env["PROJECT_DIR"])
SOURCE = ROOT / "audio" / "sources"
OUT = ROOT / "generated"
NAMES = ["hazel", "dragon", "no_sclera", "goat", "newt", "terminator",
         "cat", "owl", "nauga", "deer", "hazel_sigh"]

OUT.mkdir(exist_ok=True)
(OUT / "audio_assets.h").write_text("""// Generated from audio/sources/*.wav. DO NOT EDIT.\n#pragma once\n#include <Arduino.h>\nstruct AudioSample { const int16_t *data; uint32_t length; };\nextern const AudioSample AUDIO_SAMPLES[11];\n""")
with (OUT / "audio_assets.cpp").open("w") as out:
    out.write('#include "audio_assets.h"\n\n')
    records=[]
    for index,name in enumerate(NAMES):
        with wave.open(str(SOURCE / f"{name}.wav"), "rb") as wav:
            if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000,1,2):
                raise ValueError(f"{name}.wav must be 16 kHz, mono, signed 16-bit PCM")
            count=wav.getnframes(); values=struct.unpack(f"<{count}h", wav.readframes(count))
        symbol=f"sample{index}"
        out.write(f"const int16_t {symbol}[] PROGMEM = {{\n")
        for i in range(0,count,16):
            out.write("  "+", ".join(str(v) for v in values[i:i+16])+",\n")
        out.write("};\n\n"); records.append((symbol,count))
    out.write("const AudioSample AUDIO_SAMPLES[11] = {\n")
    for symbol,count in records: out.write(f"  {{{symbol}, {count}}},\n")
    out.write("};\n")
print("Generated audio sample tables")
