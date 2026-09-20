/**
 * @file gif_writer.cpp
 * @brief Palette building and LZW, the two halves of a GIF.
 */

#include "gif_writer.h"

#include <algorithm>
#include <map>
#include <stdio.h>
#include <string.h>

namespace {

/** @brief RGB565 expanded to 0x00RRGGBB the way the panel would show it. */
uint32_t expand565(uint16_t c) {
  const uint8_t r = (uint8_t)((c >> 11) & 0x1F);
  const uint8_t g = (uint8_t)((c >> 5) & 0x3F);
  const uint8_t b = (uint8_t)(c & 0x1F);
  return ((uint32_t)((r << 3) | (r >> 2)) << 16) |
         ((uint32_t)((g << 2) | (g >> 4)) << 8) | (uint32_t)((b << 3) | (b >> 3));
}

/** @brief One colour of the source, with how often it appears. */
struct ColorCount {
  uint16_t rgb565; ///< The colour as the framebuffer holds it
  uint32_t count;  ///< Pixels of it across every frame
  uint8_t r, g, b; ///< Expanded, for the median cut
};

/**
 * @brief Median cut down to at most @p wanted colours.
 *
 * Boxes are split on their widest channel at the weighted median, which keeps
 * a colour that covers a lot of pixels from being merged away into one that
 * barely appears.
 */
std::vector<uint32_t> medianCut(std::vector<ColorCount> &colors, size_t wanted) {
  struct Box {
    size_t begin, end; ///< Half-open range into colors
  };
  std::vector<Box> boxes;
  boxes.push_back({0, colors.size()});

  while (boxes.size() < wanted) {
    // Split the box with the widest channel; stop when none can be split.
    size_t target = boxes.size();
    int bestSpread = 0, bestChannel = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (boxes[i].end - boxes[i].begin < 2)
        continue;
      uint8_t lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
      for (size_t j = boxes[i].begin; j < boxes[i].end; ++j) {
        const uint8_t v[3] = {colors[j].r, colors[j].g, colors[j].b};
        for (int c = 0; c < 3; ++c) {
          lo[c] = std::min(lo[c], v[c]);
          hi[c] = std::max(hi[c], v[c]);
        }
      }
      for (int c = 0; c < 3; ++c) {
        const int spread = hi[c] - lo[c];
        if (spread > bestSpread) {
          bestSpread = spread;
          bestChannel = c;
          target = i;
        }
      }
    }
    if (target == boxes.size() || bestSpread == 0)
      break;

    Box &box = boxes[target];
    const int channel = bestChannel;
    std::sort(colors.begin() + box.begin, colors.begin() + box.end,
              [channel](const ColorCount &a, const ColorCount &b) {
                const uint8_t av = channel == 0 ? a.r : (channel == 1 ? a.g : a.b);
                const uint8_t bv = channel == 0 ? b.r : (channel == 1 ? b.g : b.b);
                if (av != bv)
                  return av < bv;
                return a.rgb565 < b.rgb565; // Ties broken stably, for determinism
              });
    uint64_t total = 0;
    for (size_t j = box.begin; j < box.end; ++j)
      total += colors[j].count;
    uint64_t half = 0;
    size_t split = box.begin + 1;
    for (size_t j = box.begin; j + 1 < box.end; ++j) {
      half += colors[j].count;
      if (half * 2 >= total) {
        split = j + 1;
        break;
      }
    }
    const Box left{box.begin, split}, right{split, box.end};
    boxes[target] = left;
    boxes.push_back(right);
  }

  std::vector<uint32_t> palette;
  for (const Box &box : boxes) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t j = box.begin; j < box.end; ++j) {
      r += (uint64_t)colors[j].r * colors[j].count;
      g += (uint64_t)colors[j].g * colors[j].count;
      b += (uint64_t)colors[j].b * colors[j].count;
      n += colors[j].count;
    }
    if (!n)
      continue;
    palette.push_back(((r / n) << 16) | ((g / n) << 8) | (b / n));
  }
  return palette;
}

/** @brief GIF's variable-width LZW, emitted as sub-blocks. */
class LzwEncoder {
public:
  /** @param minCodeSize Bits per pixel, at least 2. */
  explicit LzwEncoder(int minCodeSize)
      : _minCodeSize(minCodeSize), _clear(1 << minCodeSize),
        _eoi((1 << minCodeSize) + 1) {}

  /**
   * @brief Encode one frame's indices.
   * @param indices One byte per pixel.
   * @param out     Receives the LZW stream, without the sub-block framing.
   */
  void encode(const std::vector<uint8_t> &indices, std::vector<uint8_t> *out) {
    _out = out;
    _bitBuffer = 0;
    _bitCount = 0;
    reset();
    put(_clear);
    if (indices.empty()) {
      put(_eoi);
      flush();
      return;
    }

    uint32_t prefix = indices[0];
    for (size_t i = 1; i < indices.size(); ++i) {
      const uint8_t k = indices[i];
      const uint64_t key = ((uint64_t)prefix << 8) | k;
      const auto it = _table.find(key);
      if (it != _table.end()) {
        prefix = it->second;
        continue;
      }
      put(prefix);
      _table[key] = _next++;
      if (_next > (1u << _codeSize)) {
        if (_codeSize < 12) {
          ++_codeSize;
        } else {
          // The table is full: start again, which is what a decoder expects
          // when it sees a clear code.
          put(_clear);
          reset();
        }
      }
      prefix = k;
    }
    put(prefix);
    put(_eoi);
    flush();
  }

private:
  void reset(void) {
    _table.clear();
    _next = _eoi + 1;
    _codeSize = _minCodeSize + 1;
  }

  void put(uint32_t code) {
    _bitBuffer |= (uint32_t)code << _bitCount;
    _bitCount += _codeSize;
    while (_bitCount >= 8) {
      _out->push_back((uint8_t)(_bitBuffer & 0xFF));
      _bitBuffer >>= 8;
      _bitCount -= 8;
    }
  }

  void flush(void) {
    if (_bitCount > 0) {
      _out->push_back((uint8_t)(_bitBuffer & 0xFF));
      _bitBuffer = 0;
      _bitCount = 0;
    }
  }

  int _minCodeSize;                   ///< Bits per pixel
  uint32_t _clear;                    ///< Clear code
  uint32_t _eoi;                      ///< End-of-information code
  uint32_t _next = 0;                 ///< Next code to hand out
  int _codeSize = 0;                  ///< Current code width in bits
  uint32_t _bitBuffer = 0;            ///< Partial byte
  int _bitCount = 0;                  ///< Bits held in _bitBuffer
  std::map<uint64_t, uint32_t> _table; ///< (prefix, byte) -> code
  std::vector<uint8_t> *_out = nullptr; ///< Destination
};

void putByte(std::vector<uint8_t> &v, uint8_t b) { v.push_back(b); }
void putShort(std::vector<uint8_t> &v, uint16_t s) {
  v.push_back((uint8_t)(s & 0xFF));
  v.push_back((uint8_t)(s >> 8));
}

/** @brief Wrap a byte stream in GIF's 255-byte sub-blocks. */
void putSubBlocks(std::vector<uint8_t> &v, const std::vector<uint8_t> &data) {
  size_t at = 0;
  while (at < data.size()) {
    const size_t n = std::min<size_t>(255, data.size() - at);
    v.push_back((uint8_t)n);
    v.insert(v.end(), data.begin() + at, data.begin() + at + n);
    at += n;
  }
  v.push_back(0);
}

} // namespace

GifWriter::GifWriter(int width, int height, int scale)
    : _width(width), _height(height), _scale(scale < 1 ? 1 : scale) {}

void GifWriter::addFrame(const uint16_t *rgb565) {
  _frames.push_back(
      std::vector<uint16_t>(rgb565, rgb565 + (size_t)_width * _height));
}

bool GifWriter::write(const std::string &path, int delayMs,
                      std::string *error) {
  if (_frames.empty()) {
    if (error)
      *error = "no frames";
    return false;
  }

  // -- Palette, from every frame at once --------------------------------
  //
  // Per-frame palettes are what make a GIF of a slow animation shimmer: the
  // colours shift under a picture that is barely moving. One palette for the
  // whole loop also keeps identical frames encoding identically, which is what
  // holds the seam together.
  std::map<uint16_t, uint32_t> histogram;
  for (const auto &frame : _frames)
    for (uint16_t c : frame)
      histogram[c]++;

  std::vector<ColorCount> colors;
  colors.reserve(histogram.size());
  for (const auto &entry : histogram) {
    const uint32_t rgb = expand565(entry.first);
    colors.push_back({entry.first, entry.second, (uint8_t)(rgb >> 16),
                      (uint8_t)(rgb >> 8), (uint8_t)rgb});
  }

  // One index is kept back for transparency, which is what lets a frame say
  // "unchanged since the last one" instead of repeating the pixel.
  const size_t maxColors = 255;
  std::vector<uint32_t> palette;
  if (colors.size() <= maxColors) {
    // Few enough to keep exactly, which is common: these are flat-shaded eyes
    // on a plain background.
    for (const ColorCount &c : colors)
      palette.push_back(expand565(c.rgb565));
  } else {
    palette = medianCut(colors, maxColors);
  }
  if (palette.empty())
    palette.push_back(0);

  // Map every source colour to its nearest palette entry once, rather than per
  // pixel: there are at most a few thousand distinct colours and millions of
  // pixels.
  std::map<uint16_t, uint8_t> lookup;
  for (const auto &entry : histogram) {
    const uint32_t rgb = expand565(entry.first);
    const int r = (int)((rgb >> 16) & 0xFF), g = (int)((rgb >> 8) & 0xFF),
              b = (int)(rgb & 0xFF);
    int bestIndex = 0;
    long bestDistance = -1;
    for (size_t i = 0; i < palette.size(); ++i) {
      const int pr = (int)((palette[i] >> 16) & 0xFF);
      const int pg = (int)((palette[i] >> 8) & 0xFF);
      const int pb = (int)(palette[i] & 0xFF);
      const long d = (long)(r - pr) * (r - pr) + (long)(g - pg) * (g - pg) +
                     (long)(b - pb) * (b - pb);
      if (bestDistance < 0 || d < bestDistance) {
        bestDistance = d;
        bestIndex = (int)i;
      }
    }
    lookup[entry.first] = (uint8_t)bestIndex;
  }

  const uint8_t transparentIndex = (uint8_t)palette.size();

  // GIF's colour table must be a power of two, at least 4 entries, and must
  // have room for the transparent index as well as the colours.
  int bits = 2;
  while ((1u << bits) < palette.size() + 1)
    ++bits;
  const size_t tableSize = (size_t)1 << bits;

  const int outW = _width * _scale, outH = _height * _scale;

  std::vector<uint8_t> file;
  const char *magic = "GIF89a";
  file.insert(file.end(), magic, magic + 6);
  putShort(file, (uint16_t)outW);
  putShort(file, (uint16_t)outH);
  putByte(file, (uint8_t)(0x80 | 0x70 | (bits - 1))); // Global table, 8-bit
  putByte(file, 0);                                   // Background index
  putByte(file, 0);                                   // Pixel aspect
  for (size_t i = 0; i < tableSize; ++i) {
    const uint32_t c = i < palette.size() ? palette[i] : 0;
    putByte(file, (uint8_t)(c >> 16));
    putByte(file, (uint8_t)(c >> 8));
    putByte(file, (uint8_t)c);
  }

  // Loop forever.
  putByte(file, 0x21);
  putByte(file, 0xFF);
  putByte(file, 11);
  const char *netscape = "NETSCAPE2.0";
  file.insert(file.end(), netscape, netscape + 11);
  putByte(file, 3);
  putByte(file, 1);
  putShort(file, 0);
  putByte(file, 0);

  // Each frame carries only what changed since the one before, inside the
  // smallest rectangle that holds it, with everything else transparent so the
  // previous pixels show through. An eye is a small moving thing on a large
  // still background, so most of the picture is sent once rather than thirty
  // times a second.
  const int delayCentis = (delayMs + 5) / 10;
  std::vector<uint8_t> indices;
  for (size_t f = 0; f < _frames.size(); ++f) {
    const std::vector<uint16_t> &frame = _frames[f];
    const std::vector<uint16_t> *prev = f ? &_frames[f - 1] : nullptr;

    // The dirty rectangle, in source pixels.
    int x0 = 0, y0 = 0, x1 = _width - 1, y1 = _height - 1;
    if (prev) {
      x0 = _width;
      y0 = _height;
      x1 = -1;
      y1 = -1;
      for (int y = 0; y < _height; ++y) {
        for (int x = 0; x < _width; ++x) {
          if (frame[(size_t)y * _width + x] == (*prev)[(size_t)y * _width + x])
            continue;
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
      }
      if (x1 < x0) {
        // Nothing moved. A one-pixel transparent frame still holds the delay.
        x0 = y0 = 0;
        x1 = y1 = 0;
      }
    }

    const int boxW = (x1 - x0 + 1) * _scale, boxH = (y1 - y0 + 1) * _scale;

    putByte(file, 0x21); // Graphic control extension
    putByte(file, 0xF9);
    putByte(file, 4);
    // Disposal 1, leave in place, so the untouched pixels stay on screen.
    putByte(file, (uint8_t)(0x04 | (prev ? 0x01 : 0x00)));
    putShort(file, (uint16_t)(delayCentis < 1 ? 1 : delayCentis));
    putByte(file, prev ? transparentIndex : 0);
    putByte(file, 0);

    putByte(file, 0x2C); // Image descriptor
    putShort(file, (uint16_t)(x0 * _scale));
    putShort(file, (uint16_t)(y0 * _scale));
    putShort(file, (uint16_t)boxW);
    putShort(file, (uint16_t)boxH);
    putByte(file, 0);

    indices.resize((size_t)boxW * boxH);
    for (int y = 0; y < boxH; ++y) {
      const int sy = y0 + y / _scale;
      uint8_t *dst = &indices[(size_t)y * boxW];
      for (int x = 0; x < boxW; ++x) {
        const int sx = x0 + x / _scale;
        const uint16_t c = frame[(size_t)sy * _width + sx];
        dst[x] = (prev && c == (*prev)[(size_t)sy * _width + sx])
                     ? transparentIndex
                     : lookup[c];
      }
    }

    std::vector<uint8_t> lzw;
    LzwEncoder encoder(8);
    encoder.encode(indices, &lzw);
    putByte(file, 8); // Minimum code size
    putSubBlocks(file, lzw);
  }
  putByte(file, 0x3B); // Trailer

  FILE *f = fopen(path.c_str(), "wb");
  if (!f) {
    if (error)
      *error = "could not open the file for writing";
    return false;
  }
  const bool ok = fwrite(file.data(), 1, file.size(), f) == file.size();
  fclose(f);
  if (!ok && error)
    *error = "short write";
  return ok;
}
