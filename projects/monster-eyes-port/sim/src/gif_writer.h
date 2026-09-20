/**
 * @file gif_writer.h
 * @brief Writing an animated GIF, with no dependency beyond the standard
 *        library.
 *
 * Written here rather than shelled out to ffmpeg or ImageMagick because the
 * simulator's whole point is that it runs from a plain CMake build, and a
 * capture format that needs a second toolchain installed is one more reason
 * for the loop not to be reproducible on someone else's machine.
 *
 * The encoder is deterministic: the same frames in give the same bytes out.
 * That matters more than it sounds, because a seamless loop relies on two
 * identical frames encoding identically. Anything that varies per frame --
 * ordered dithering being the usual culprit -- puts a visible seam back into a
 * loop that was exact before encoding.
 */

#ifndef _SIM_GIF_WRITER_H_
#define _SIM_GIF_WRITER_H_

#include <stdint.h>
#include <string>
#include <vector>

/**
 * @brief Collects frames and writes them out as one animated GIF.
 *
 * Frames are handed in as native-endian RGB565, the framebuffer's own format.
 * The palette is built once from every frame together rather than per frame,
 * so colours do not shift between them.
 */
class GifWriter {
public:
  /**
   * @param width  Frame width in pixels.
   * @param height Frame height in pixels.
   * @param scale  Whole-pixel magnification, 1 for none.
   */
  GifWriter(int width, int height, int scale);

  /**
   * @brief Add a frame.
   * @param rgb565 width * height pixels, row-major, top row first.
   */
  void addFrame(const uint16_t *rgb565);

  /** @brief Frames collected so far. @return Count. */
  size_t frameCount(void) const { return _frames.size(); }

  /**
   * @brief Write the file.
   * @param path       Destination.
   * @param delayMs    Time each frame is shown.
   * @param error      Receives a message on failure.
   * @return true if it was written.
   */
  bool write(const std::string &path, int delayMs, std::string *error);

private:
  int _width;  ///< Source width
  int _height; ///< Source height
  int _scale;  ///< Magnification
  std::vector<std::vector<uint16_t>> _frames; ///< One entry per frame
};

#endif // _SIM_GIF_WRITER_H_
