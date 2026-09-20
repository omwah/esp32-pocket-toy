/**
 * @file FFat.h
 * @brief The Arduino-ESP32 FFat API, backed by an ordinary Linux directory.
 *
 * Eyes_Assets.cpp's ESP32 branch opens every asset through the global FFat
 * object, so pointing that object at a directory is the whole of the port: the
 * BMP parser, the config parser and the path resolution are the same code that
 * runs on the board, reading the same files out of data/ that the build uploads
 * to flash.
 *
 * Paths are absolute on the device ("/eyes/deer/config.eye"). Here they are
 * resolved against a root directory, and a path that tries to climb out of that
 * root with ".." is refused rather than followed.
 */

#ifndef _HOST_FFAT_H_
#define _HOST_FFAT_H_

#include "Arduino.h"

/** Open mode; the simulator never writes, so only reading is offered. */
#define FILE_READ "r"

/** @brief Origin for File::seek(), matching the ESP32 core's enum. */
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

/**
 * @brief An open file on the host, wearing the ESP32 core's File interface.
 *
 * Copying one hands over the handle rather than duplicating it, because
 * Eyes_Assets.cpp assigns the result of FFat.open() into an existing File and
 * expects the old handle to go away.
 */
class File : public Stream {
public:
  File() : _f(nullptr) {}
  /** @param f Handle to adopt. */
  explicit File(FILE *f) : _f(f) {}
  File(File &&other) noexcept : _f(other._f) { other._f = nullptr; }
  /** @brief Adopt another File's handle, closing whatever this one held. */
  File &operator=(File &&other) noexcept {
    if (this != &other) {
      close();
      _f = other._f;
      other._f = nullptr;
    }
    return *this;
  }
  File(const File &) = delete;
  File &operator=(const File &) = delete;
  ~File() { close(); }

  /** @brief Is the file open? @return true if usable. */
  explicit operator bool() const { return _f != nullptr; }

  /** @brief Close the file; safe to call twice. */
  void close(void) {
    if (_f) {
      fclose(_f);
      _f = nullptr;
    }
  }

  /**
   * @brief Move the read position.
   * @param pos  Offset in bytes.
   * @param mode Origin.
   * @return true on success.
   */
  bool seek(uint32_t pos, SeekMode mode = SeekSet) {
    if (!_f)
      return false;
    const int whence = (mode == SeekCur) ? SEEK_CUR
                       : (mode == SeekEnd) ? SEEK_END
                                           : SEEK_SET;
    return fseek(_f, (long)pos, whence) == 0;
  }

  /** @brief Current read position. @return Byte offset. */
  uint32_t position(void) { return _f ? (uint32_t)ftell(_f) : 0; }

  /** @brief File length in bytes. @return Size, or 0 if not open. */
  uint32_t size(void) {
    if (!_f)
      return 0;
    const long here = ftell(_f);
    fseek(_f, 0, SEEK_END);
    const long end = ftell(_f);
    fseek(_f, here, SEEK_SET);
    return (uint32_t)end;
  }

  /**
   * @brief Read a block.
   * @param buf Destination.
   * @param len Bytes wanted.
   * @return Bytes read; the library treats a short read as failure.
   */
  int read(uint8_t *buf, size_t len) {
    if (!_f)
      return -1;
    return (int)fread(buf, 1, len, _f);
  }

  // Stream, for ArduinoJson: read() must give -1 at end of file rather than 0,
  // or the parser sees an endless run of NUL bytes instead of a clean end.
  int read(void) override {
    if (!_f)
      return -1;
    const int c = fgetc(_f);
    return (c == EOF) ? -1 : c;
  }
  int peek(void) override {
    if (!_f)
      return -1;
    const int c = fgetc(_f);
    if (c == EOF)
      return -1;
    ungetc(c, _f);
    return c;
  }
  int available(void) override {
    if (!_f)
      return 0;
    const long here = ftell(_f);
    fseek(_f, 0, SEEK_END);
    const long end = ftell(_f);
    fseek(_f, here, SEEK_SET);
    return (int)(end - here);
  }
  size_t readBytes(char *buf, size_t len) override {
    if (!_f)
      return 0;
    return fread(buf, 1, len, _f);
  }
  size_t write(uint8_t c) override {
    (void)c;
    return 0; // Read-only by design
  }

private:
  FILE *_f; ///< Open handle, or NULL
};

/**
 * @brief The ESP32 core's FFat filesystem object, rooted at a host directory.
 */
class HostFFatFS {
public:
  /**
   * @brief Point the filesystem at a directory before begin() is called.
   * @param path Directory standing in for the FAT partition.
   */
  void setRoot(const char *path);

  /**
   * @brief Mount the filesystem.
   * @param formatOnFail Ignored; the host never formats anything.
   * @return true if the root directory exists and is readable.
   */
  bool begin(bool formatOnFail = false);

  /**
   * @brief Open a file below the root.
   * @param path Device-absolute path, e.g. "/eyes/deer/config.eye".
   * @param mode Ignored; reading only.
   * @return An open File, or a closed one if the path is missing or escapes
   *         the root.
   */
  File open(const char *path, const char *mode = FILE_READ);

  /** @brief The directory standing in for flash. @return Root path. */
  const char *root(void) const { return _root; }

private:
  char _root[1024] = "."; ///< Directory the device paths resolve against
};

extern HostFFatFS FFat; ///< Stands in for the core's FFat object

#endif // _HOST_FFAT_H_
