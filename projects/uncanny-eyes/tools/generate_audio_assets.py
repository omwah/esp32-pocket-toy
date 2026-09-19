"""Generate raw PCM FATFS files from canonical 16 kHz mono WAV sources."""
from pathlib import Path
import wave

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parents[1]
else:
    Import("env")
    ROOT = Path(env["PROJECT_DIR"])
SOURCE = ROOT / "audio" / "sources"
OUT = ROOT / "generated"
DATA = ROOT / "data" / "audio"
NAMES = ["hazel", "dragon", "no_sclera", "goat", "newt", "terminator",
         "cat", "owl", "nauga", "deer", "anime", "hazel_sigh"]

OUT.mkdir(exist_ok=True); DATA.mkdir(parents=True, exist_ok=True)
records = []
for index, name in enumerate(NAMES):
    with wave.open(str(SOURCE / f"{name}.wav"), "rb") as wav:
        if (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) != (16000, 1, 2):
            raise ValueError(f"{name}.wav must be 16 kHz, mono, signed 16-bit PCM")
        count = wav.getnframes(); pcm = wav.readframes(count)
    path = f"/audio/{index:02d}.pcm"
    (DATA / f"{index:02d}.pcm").write_bytes(pcm)
    records.append((path, count))

(OUT / "audio_assets.h").write_text("""// Generated metadata. DO NOT EDIT.\n#pragma once\n#include <Arduino.h>\nstruct AudioSampleInfo { const char *path; uint32_t length; };\nextern const AudioSampleInfo AUDIO_SAMPLES[12];\nextern const uint32_t AUDIO_MAX_SAMPLES;\n""")
with (OUT / "audio_assets.cpp").open("w") as out:
    out.write('#include "audio_assets.h"\nconst AudioSampleInfo AUDIO_SAMPLES[12] = {\n')
    for path, count in records: out.write(f'  {{"{path}", {count}}},\n')
    out.write("};\n")
    out.write(f"const uint32_t AUDIO_MAX_SAMPLES = {max(n for _, n in records)};\n")
print("Generated audio metadata and FATFS PCM files")
