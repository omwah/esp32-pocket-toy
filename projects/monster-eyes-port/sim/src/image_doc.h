/**
 * @file image_doc.h
 * @brief A package bitmap held in memory, decoded for editing and encoded back.
 *
 * The renderer only ever reads a BMP, so an edited image has to go back to it
 * as a BMP. It never becomes a file: the bytes encode() produces are handed to
 * the FFat shim's overlay, exactly as an edited config.eye is, so the tree on
 * disk is untouched until a package is saved.
 *
 * Two kinds of image, and they are not the same job:
 *
 *   1-bit eyelids. bmpLoadEyelid() reads only the TOPMOST AND BOTTOMMOST lit
 *   pixel of each column and throws the rest away, so the image is a silhouette
 *   envelope rather than a picture. A hole in the middle of a lid changes
 *   nothing. Two colours, and only the palette's relative brightness matters:
 *   the loader decides which index is "lit" by comparing luminance, so this
 *   class normalises index 1 to the lighter entry on load and writes the
 *   palette that way, and what you paint is what the loader will read.
 *
 *   24-bit iris and sclera textures. These are POLAR: x is the angle around the
 *   eye, 0 to 1023 scaled across the width, and y is distance out from the
 *   pupil. They are not pictures of an eye and painting one is not painting on
 *   the eye. The renderer converts to RGB565 as it loads, so the low bits of
 *   each channel are discarded; colours are quantised here on the way in so the
 *   canvas shows what will actually be rendered.
 */

#ifndef _SIM_IMAGE_DOC_H_
#define _SIM_IMAGE_DOC_H_

#include <stdint.h>
#include <string>
#include <vector>

/**
 * @brief One editable bitmap.
 *
 * Pixels are held as 8-bit RGB triples whatever the file's depth, so drawing
 * code does not care which kind it has; a 1-bit image simply holds only its two
 * palette colours, and encode() maps them back to indices.
 */
class ImageDocument {
public:
  /** @brief What the file's depth allows. */
  enum Kind {
    Indexed1, ///< 1-bit eyelid silhouette; two colours
    Rgb24     ///< 24-bit texture, quantised to RGB565
  };

  /**
   * @brief Decode a BMP.
   * @param bytes File contents.
   * @param error Receives a message on failure.
   * @return true if it decoded.
   */
  bool load(const std::vector<uint8_t> &bytes, std::string *error);

  /** @brief Encode back to a BMP the library's parser accepts.
   *  @return File contents. */
  std::vector<uint8_t> encode(void) const;

  /** @brief Image width. @return Pixels. */
  int width(void) const { return _width; }
  /** @brief Image height. @return Pixels. */
  int height(void) const { return _height; }
  /** @brief Which kind of image this is. @return Kind. */
  Kind kind(void) const { return _kind; }
  /** @brief Has it been edited since load? @return true if so. */
  bool dirty(void) const { return _dirty; }
  /** @brief Forget the edits, after a save. */
  void clearDirty(void) { _dirty = false; }

  /**
   * @brief Read a pixel.
   * @param x Column.
   * @param y Row, 0 at the top.
   * @return 0x00RRGGBB, or 0 outside the image.
   */
  uint32_t pixel(int x, int y) const;

  /**
   * @brief Write a pixel, quantised to what the format can hold.
   * @param x   Column.
   * @param y   Row, 0 at the top.
   * @param rgb 0x00RRGGBB.
   */
  void setPixel(int x, int y, uint32_t rgb);

  /** @brief The two colours a 1-bit image may use.
   *  @param lit true for the lit entry. @return 0x00RRGGBB. */
  uint32_t paletteColor(bool lit) const { return _palette[lit ? 1 : 0]; }

  /** @brief Pixels as 0xAABBGGRR, for uploading to a texture.
   *  @return Row-major, top row first. */
  const std::vector<uint32_t> &rgbaForUpload(void) const;

  // -- Undo ---------------------------------------------------------------
  //
  // A whole-buffer snapshot per stroke rather than a record of what changed.
  // The largest package bitmap is under a quarter of a megabyte and a stroke
  // takes one copy, so the simple thing is affordable and cannot disagree with
  // what the tools actually did.

  /** @brief Note the state before a stroke begins. */
  void beginStroke(void);
  /** @brief Finish a stroke, discarding it if nothing changed. */
  void endStroke(void);
  /** @brief Step back one stroke. @return true if anything was undone. */
  bool undo(void);
  /** @brief Step forward one stroke. @return true if anything was redone. */
  bool redo(void);
  /** @brief Is there anything to undo? @return true if so. */
  bool canUndo(void) const { return !_undo.empty(); }
  /** @brief Is there anything to redo? @return true if so. */
  bool canRedo(void) const { return !_redo.empty(); }

private:
  void touch(void);

  int _width = 0;              ///< Pixels across
  int _height = 0;             ///< Pixels down
  Kind _kind = Rgb24;          ///< Depth the file had
  std::vector<uint8_t> _rgb;   ///< 3 bytes per pixel, top row first
  uint32_t _palette[2] = {0x000000, 0xFFFFFF}; ///< 1-bit entries, dark first
  bool _dirty = false;         ///< Edited since load

  mutable std::vector<uint32_t> _upload; ///< Cache for rgbaForUpload()
  mutable bool _uploadStale = true;      ///< Cache needs rebuilding

  std::vector<uint8_t> _strokeBefore;            ///< Snapshot at beginStroke()
  bool _inStroke = false;                        ///< Between begin and end
  std::vector<std::vector<uint8_t>> _undo;       ///< Snapshots, oldest first
  std::vector<std::vector<uint8_t>> _redo;       ///< Snapshots, newest first
  static const size_t kMaxUndo = 32;             ///< Snapshots kept
};

#endif // _SIM_IMAGE_DOC_H_
