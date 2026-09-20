/**
 * @file capture.cpp
 * @brief PNG encoding and the JSON sidecars.
 */

#include "capture.h"

#include <Adafruit_Monster_Eyes.h>
#include <zlib.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

uint64_t hostWallMicros(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

// ---------------------------------------------------------------------------
//  STATE
// ---------------------------------------------------------------------------

// renderMillis() and transferMillis() are the library's own measurements, taken
// with micros(). Under the virtual clock time only moves between frames, so
// both read zero; and transferMillis() is zero regardless here, because a
// memory backend has no bus to push pixels down. wallMs is the number with
// something in it -- host time actually spent, which is what says whether a
// change made the renderer slower.
FrameState captureState(Adafruit_Monster_Eyes &eyes, int index,
                        const char *eyeName, float wallMs) {
  FrameState s;
  s.index = index;
  s.timeUs = micros();
  s.eyeName = eyeName;
  s.eyeSize = eyes.eyeSize();
  s.gazeMapX = eyes.gazeMapX();
  s.gazeMapY = eyes.gazeMapY();
  s.gazeX = eyes.gazeX();
  s.gazeY = eyes.gazeY();
  // The library's sign convention is opposite to what is on screen on both
  // axes, so both are recorded rather than leaving a reader to guess which one
  // a bare "gaze" meant.
  s.gazeScreenX = -eyes.gazeX();
  s.gazeScreenY = -eyes.gazeY();
  s.blinkPhase = eyes.blinkPhase();
  s.irisFraction = eyes.irisFraction();
  s.pupil = eyes.pupil();
  s.renderMs = eyes.renderMillis();
  s.transferMs = eyes.transferMillis();
  s.frameRate = eyes.frameRate();
  s.wallMs = wallMs;
  s.autoGaze = eyes.autoGaze();
  s.autoBlink = eyes.autoBlink();
  return s;
}

// ---------------------------------------------------------------------------
//  PNG
// ---------------------------------------------------------------------------
//
// Hand-rolled rather than linked against libpng: a PNG is a signature, three
// chunks and a zlib stream, and zlib is already a dependency of everything.
// One less package for anyone building the simulator to install.

static void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static bool writeChunk(FILE *f, const char *type, const uint8_t *data,
                       size_t len) {
  uint8_t header[8];
  put32(header, (uint32_t)len);
  memcpy(header + 4, type, 4);
  if (fwrite(header, 1, 8, f) != 8)
    return false;
  if (len && fwrite(data, 1, len, f) != len)
    return false;

  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, (const Bytef *)type, 4);
  if (len)
    crc = crc32(crc, (const Bytef *)data, (uInt)len);
  uint8_t tail[4];
  put32(tail, (uint32_t)crc);
  return fwrite(tail, 1, 4, f) == 4;
}

// RGB565 to RGB888 by replicating the high bits into the low ones, so full
// scale stays full scale: 0x1F becomes 0xFF rather than 0xF8. Shifting alone
// would darken every capture by about 3%.
static inline void expand565(uint16_t c, uint8_t *rgb) {
  const uint8_t r = (uint8_t)((c >> 11) & 0x1F);
  const uint8_t g = (uint8_t)((c >> 5) & 0x3F);
  const uint8_t b = (uint8_t)(c & 0x1F);
  rgb[0] = (uint8_t)((r << 3) | (r >> 2));
  rgb[1] = (uint8_t)((g << 2) | (g >> 4));
  rgb[2] = (uint8_t)((b << 3) | (b >> 3));
}

bool writePng(const char *path, const uint16_t *pixels, int width,
              int height) {
  if (!pixels || width <= 0 || height <= 0)
    return false;

  // Each scanline is preceded by its filter byte. Filter 0 (none) throughout:
  // the images are flat-shaded eyes on a solid background, where deflate
  // already finds the runs and a predictor would only scramble them.
  const size_t stride = (size_t)width * 3 + 1;
  const size_t rawLen = stride * (size_t)height;
  uint8_t *raw = (uint8_t *)malloc(rawLen);
  if (!raw)
    return false;

  for (int y = 0; y < height; ++y) {
    uint8_t *row = raw + (size_t)y * stride;
    *row++ = 0;
    const uint16_t *src = pixels + (size_t)y * width;
    for (int x = 0; x < width; ++x)
      expand565(src[x], row + (size_t)x * 3);
  }

  uLongf compLen = compressBound((uLong)rawLen);
  uint8_t *comp = (uint8_t *)malloc(compLen);
  if (!comp) {
    free(raw);
    return false;
  }
  const int zr = compress2(comp, &compLen, raw, (uLong)rawLen, 6);
  free(raw);
  if (zr != Z_OK) {
    free(comp);
    return false;
  }

  FILE *f = fopen(path, "wb");
  if (!f) {
    free(comp);
    return false;
  }

  static const uint8_t signature[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
  bool ok = fwrite(signature, 1, 8, f) == 8;

  uint8_t ihdr[13];
  put32(ihdr, (uint32_t)width);
  put32(ihdr + 4, (uint32_t)height);
  ihdr[8] = 8;  // Bit depth
  ihdr[9] = 2;  // Colour type: truecolour
  ihdr[10] = 0; // Deflate
  ihdr[11] = 0; // Adaptive filtering
  ihdr[12] = 0; // No interlace
  ok = ok && writeChunk(f, "IHDR", ihdr, sizeof(ihdr));
  ok = ok && writeChunk(f, "IDAT", comp, compLen);
  ok = ok && writeChunk(f, "IEND", nullptr, 0);

  fclose(f);
  free(comp);
  return ok;
}

// ---------------------------------------------------------------------------
//  JSON
// ---------------------------------------------------------------------------

static void writeStateFields(FILE *f, const FrameState &s, const char *indent) {
  fprintf(f, "%s\"frame\": %d,\n", indent, s.index);
  fprintf(f, "%s\"timeUs\": %u,\n", indent, s.timeUs);
  fprintf(f, "%s\"eye\": \"%s\",\n", indent, s.eyeName ? s.eyeName : "");
  fprintf(f, "%s\"eyeSize\": %d,\n", indent, s.eyeSize);
  fprintf(f, "%s\"gazeScreen\": {\"x\": %.4f, \"y\": %.4f},\n", indent,
          s.gazeScreenX, s.gazeScreenY);
  fprintf(f, "%s\"gazeLibrary\": {\"x\": %.4f, \"y\": %.4f},\n", indent,
          s.gazeX, s.gazeY);
  fprintf(f, "%s\"gazeMap\": {\"x\": %.3f, \"y\": %.3f},\n", indent, s.gazeMapX,
          s.gazeMapY);
  fprintf(f, "%s\"blinkPhase\": %.4f,\n", indent, s.blinkPhase);
  fprintf(f, "%s\"irisFraction\": %.4f,\n", indent, s.irisFraction);
  fprintf(f, "%s\"pupil\": %.4f,\n", indent, s.pupil);
  fprintf(f, "%s\"renderMs\": %.3f,\n", indent, s.renderMs);
  fprintf(f, "%s\"transferMs\": %.3f,\n", indent, s.transferMs);
  fprintf(f, "%s\"wallMs\": %.3f,\n", indent, s.wallMs);
  fprintf(f, "%s\"frameRate\": %.2f,\n", indent, s.frameRate);
  fprintf(f, "%s\"autoGaze\": %s,\n", indent, s.autoGaze ? "true" : "false");
  fprintf(f, "%s\"autoBlink\": %s\n", indent, s.autoBlink ? "true" : "false");
}

bool writeStateJson(const char *path, const FrameState &state) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  fprintf(f, "{\n");
  writeStateFields(f, state, "  ");
  fprintf(f, "}\n");
  fclose(f);
  return true;
}

bool writeManifestJson(const char *path, const FrameState *states, size_t count,
                       const char *pngPattern) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  fprintf(f, "{\n");
  fprintf(f, "  \"panel\": {\"width\": %d, \"height\": %d, \"format\": "
             "\"rgb565\"},\n",
          320, 240);
  fprintf(f, "  \"imagePattern\": \"%s\",\n", pngPattern ? pngPattern : "");
  fprintf(f, "  \"frameCount\": %zu,\n", count);
  fprintf(f, "  \"frames\": [\n");
  for (size_t i = 0; i < count; ++i) {
    fprintf(f, "    {\n");
    writeStateFields(f, states[i], "      ");
    fprintf(f, "    }%s\n", (i + 1 < count) ? "," : "");
  }
  fprintf(f, "  ]\n}\n");
  fclose(f);
  return true;
}
