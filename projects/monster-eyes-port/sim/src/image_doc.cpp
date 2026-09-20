/**
 * @file image_doc.cpp
 * @brief Decoding, encoding and undo for a package bitmap.
 */

#include "image_doc.h"

#include <stdlib.h>
#include <string.h>

namespace {

uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
void wr16(std::vector<uint8_t> &v, uint16_t x) {
  v.push_back((uint8_t)(x & 0xFF));
  v.push_back((uint8_t)(x >> 8));
}
void wr32(std::vector<uint8_t> &v, uint32_t x) {
  v.push_back((uint8_t)(x & 0xFF));
  v.push_back((uint8_t)((x >> 8) & 0xFF));
  v.push_back((uint8_t)((x >> 16) & 0xFF));
  v.push_back((uint8_t)((x >> 24) & 0xFF));
}

/** @brief Round a channel to what RGB565 can hold, by the renderer's rule. */
uint32_t quantise565(uint32_t rgb) {
  const uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
  const uint8_t g = (uint8_t)((rgb >> 8) & 0xFF);
  const uint8_t b = (uint8_t)(rgb & 0xFF);
  // The loader keeps the top 5, 6 and 5 bits; expanding them back by
  // replication is what the panel does, so the canvas shows the colour the eye
  // will actually be drawn in rather than the one that was asked for.
  const uint8_t r5 = (uint8_t)(r >> 3), g6 = (uint8_t)(g >> 2),
                b5 = (uint8_t)(b >> 3);
  return ((uint32_t)((r5 << 3) | (r5 >> 2)) << 16) |
         ((uint32_t)((g6 << 2) | (g6 >> 4)) << 8) |
         (uint32_t)((b5 << 3) | (b5 >> 3));
}

int luminance(uint32_t rgb) {
  return (int)((rgb >> 16) & 0xFF) + (int)((rgb >> 8) & 0xFF) +
         (int)(rgb & 0xFF);
}

} // namespace

bool ImageDocument::load(const std::vector<uint8_t> &bytes,
                         std::string *error) {
  auto fail = [&](const char *why) {
    if (error)
      *error = why;
    return false;
  };

  if (bytes.size() < 54)
    return fail("Not a BMP: too short.");
  const uint8_t *d = bytes.data();
  if (d[0] != 'B' || d[1] != 'M')
    return fail("Not a BMP.");

  const uint32_t dataOffset = rd32(&d[10]);
  const uint32_t dibSize = rd32(&d[14]);
  const int32_t w = (int32_t)rd32(&d[18]);
  const int32_t h = (int32_t)rd32(&d[22]);
  const uint16_t bpp = rd16(&d[28]);
  const uint32_t compression = rd32(&d[30]);

  if (dibSize < 40)
    return fail("Unsupported BMP header.");
  if (compression != 0)
    return fail("Compressed BMPs are not supported.");
  if (bpp != 1 && bpp != 24)
    return fail("Only 1-bit and 24-bit BMPs are used by the renderer.");
  if (w <= 0)
    return fail("Bad width.");

  const bool topDown = h < 0;
  const int32_t height = topDown ? -h : h;
  if (height <= 0)
    return fail("Bad height.");

  const uint32_t rowSize = (((uint32_t)w * bpp + 31) / 32) * 4;
  if (dataOffset + rowSize * (uint32_t)height > bytes.size())
    return fail("Truncated BMP.");

  _width = w;
  _height = height;
  _kind = (bpp == 1) ? Indexed1 : Rgb24;
  _rgb.assign((size_t)w * height * 3, 0);

  if (bpp == 1) {
    if (bytes.size() < 14 + dibSize + 8)
      return fail("Missing palette.");
    const uint8_t *pal = &d[14 + dibSize];
    const uint32_t c0 = ((uint32_t)pal[2] << 16) | ((uint32_t)pal[1] << 8) |
                        (uint32_t)pal[0];
    const uint32_t c1 = ((uint32_t)pal[6] << 16) | ((uint32_t)pal[5] << 8) |
                        (uint32_t)pal[4];
    // The loader calls the BRIGHTER entry lit. Normalise so index 1 is always
    // that one, and the editor never has to think about palette order again.
    const bool oneIsLit = luminance(c1) > luminance(c0);
    _palette[0] = oneIsLit ? c0 : c1;
    _palette[1] = oneIsLit ? c1 : c0;

    for (int32_t y = 0; y < height; ++y) {
      const int32_t fileRow = topDown ? y : (height - 1 - y);
      const uint8_t *row = &d[dataOffset + (uint32_t)fileRow * rowSize];
      for (int32_t x = 0; x < w; ++x) {
        const uint8_t bit = (uint8_t)((row[x >> 3] >> (7 - (x & 7))) & 1);
        const bool lit = oneIsLit ? (bit == 1) : (bit == 0);
        const uint32_t c = _palette[lit ? 1 : 0];
        uint8_t *p = &_rgb[((size_t)y * w + x) * 3];
        p[0] = (uint8_t)(c >> 16);
        p[1] = (uint8_t)(c >> 8);
        p[2] = (uint8_t)c;
      }
    }
  } else {
    for (int32_t y = 0; y < height; ++y) {
      const int32_t fileRow = topDown ? y : (height - 1 - y);
      const uint8_t *row = &d[dataOffset + (uint32_t)fileRow * rowSize];
      for (int32_t x = 0; x < w; ++x) {
        const uint8_t *src = &row[(size_t)x * 3]; // Stored B, G, R
        const uint32_t c = quantise565(((uint32_t)src[2] << 16) |
                                       ((uint32_t)src[1] << 8) |
                                       (uint32_t)src[0]);
        uint8_t *p = &_rgb[((size_t)y * w + x) * 3];
        p[0] = (uint8_t)(c >> 16);
        p[1] = (uint8_t)(c >> 8);
        p[2] = (uint8_t)c;
      }
    }
  }

  _dirty = false;
  _uploadStale = true;
  _undo.clear();
  _redo.clear();
  _inStroke = false;
  return true;
}

std::vector<uint8_t> ImageDocument::encode(void) const {
  std::vector<uint8_t> out;
  if (_width <= 0 || _height <= 0)
    return out;

  const uint16_t bpp = (_kind == Indexed1) ? 1 : 24;
  const uint32_t paletteBytes = (_kind == Indexed1) ? 8 : 0;
  const uint32_t dataOffset = 14 + 40 + paletteBytes;
  const uint32_t rowSize = (((uint32_t)_width * bpp + 31) / 32) * 4;
  const uint32_t imageBytes = rowSize * (uint32_t)_height;

  out.reserve(dataOffset + imageBytes);
  out.push_back('B');
  out.push_back('M');
  wr32(out, dataOffset + imageBytes);
  wr32(out, 0);
  wr32(out, dataOffset);

  wr32(out, 40);                      // BITMAPINFOHEADER
  wr32(out, (uint32_t)_width);
  wr32(out, (uint32_t)_height);       // Positive: bottom-up, as the sources are
  wr16(out, 1);                       // Planes
  wr16(out, bpp);
  wr32(out, 0);                       // BI_RGB
  wr32(out, imageBytes);
  wr32(out, 2835);                    // 72 dpi, in pixels per metre
  wr32(out, 2835);
  wr32(out, (_kind == Indexed1) ? 2u : 0u); // Colours used
  wr32(out, 0);                       // All colours important

  if (_kind == Indexed1) {
    for (int i = 0; i < 2; ++i) {
      const uint32_t c = _palette[i];
      out.push_back((uint8_t)c);         // B
      out.push_back((uint8_t)(c >> 8));  // G
      out.push_back((uint8_t)(c >> 16)); // R
      out.push_back(0);
    }
  }

  std::vector<uint8_t> row(rowSize);
  for (int y = _height - 1; y >= 0; --y) { // Bottom-up
    memset(row.data(), 0, row.size());
    if (_kind == Indexed1) {
      // Index 1 is the lit entry, matching how load() normalised the palette.
      const int litLum = luminance(_palette[1]);
      const int darkLum = luminance(_palette[0]);
      for (int x = 0; x < _width; ++x) {
        const uint8_t *p = &_rgb[((size_t)y * _width + x) * 3];
        const int lum = p[0] + p[1] + p[2];
        const bool lit = (litLum == darkLum)
                             ? false
                             : (abs(lum - litLum) < abs(lum - darkLum));
        if (lit)
          row[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
      }
    } else {
      for (int x = 0; x < _width; ++x) {
        const uint8_t *p = &_rgb[((size_t)y * _width + x) * 3];
        uint8_t *q = &row[(size_t)x * 3];
        q[0] = p[2]; // B
        q[1] = p[1]; // G
        q[2] = p[0]; // R
      }
    }
    out.insert(out.end(), row.begin(), row.end());
  }
  return out;
}

uint32_t ImageDocument::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= _width || y >= _height)
    return 0;
  const uint8_t *p = &_rgb[((size_t)y * _width + x) * 3];
  return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

void ImageDocument::setPixel(int x, int y, uint32_t rgb) {
  if (x < 0 || y < 0 || x >= _width || y >= _height)
    return;
  // A 1-bit image has two colours and nothing between them, so anything
  // painted lands on whichever entry it is nearer.
  if (_kind == Indexed1) {
    const int lum = luminance(rgb);
    const bool lit = abs(lum - luminance(_palette[1])) <
                     abs(lum - luminance(_palette[0]));
    rgb = _palette[lit ? 1 : 0];
  } else {
    rgb = quantise565(rgb);
  }
  uint8_t *p = &_rgb[((size_t)y * _width + x) * 3];
  const uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8),
                b = (uint8_t)rgb;
  if (p[0] == r && p[1] == g && p[2] == b)
    return;
  p[0] = r;
  p[1] = g;
  p[2] = b;
  touch();
}

void ImageDocument::touch(void) {
  _dirty = true;
  _uploadStale = true;
}

const std::vector<uint32_t> &ImageDocument::rgbaForUpload(void) const {
  if (_uploadStale) {
    _upload.resize((size_t)_width * _height);
    for (size_t i = 0; i < _upload.size(); ++i) {
      const uint8_t *p = &_rgb[i * 3];
      // 0xAABBGGRR, which is what SDL_PIXELFORMAT_ABGR8888 wants.
      _upload[i] = 0xFF000000u | ((uint32_t)p[2] << 16) |
                   ((uint32_t)p[1] << 8) | (uint32_t)p[0];
    }
    _uploadStale = false;
  }
  return _upload;
}

void ImageDocument::beginStroke(void) {
  if (_inStroke)
    return;
  _strokeBefore = _rgb;
  _inStroke = true;
}

void ImageDocument::endStroke(void) {
  if (!_inStroke)
    return;
  _inStroke = false;
  if (_strokeBefore == _rgb)
    return; // A stroke that changed nothing is not worth undoing
  _undo.push_back(_strokeBefore);
  if (_undo.size() > kMaxUndo)
    _undo.erase(_undo.begin());
  _redo.clear();
}

bool ImageDocument::undo(void) {
  if (_undo.empty())
    return false;
  _redo.push_back(_rgb);
  _rgb = _undo.back();
  _undo.pop_back();
  touch();
  return true;
}

bool ImageDocument::redo(void) {
  if (_redo.empty())
    return false;
  _undo.push_back(_rgb);
  _rgb = _redo.back();
  _redo.pop_back();
  touch();
  return true;
}
