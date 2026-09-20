/**
 * @file SdFat_Adafruit_Fork.h
 * @brief Stub FAT volume, present only so Eyes_Assets.cpp compiles.
 *
 * Unused on ESP32, where assets come from FFat, but referenced by the USB
 * drive-mode code that is compiled for every architecture. See
 * Adafruit_SPIFlash.h for the same reasoning.
 */

#ifndef _HOST_SDFAT_ADAFRUIT_FORK_H_
#define _HOST_SDFAT_ADAFRUIT_FORK_H_

#include "Adafruit_SPIFlash.h"
#include "Arduino.h"

/** @brief Stand-in for SdFat's File32; never opens. */
class File32 {
public:
  /** @brief Is the file open? @return Always false. */
  explicit operator bool() const { return false; }
  /** @brief Close the file. */
  void close(void) {}
  /** @brief Seek to an absolute offset. @param pos Offset. @return false. */
  bool seekSet(uint32_t pos) {
    (void)pos;
    return false;
  }
  /** @brief Read bytes. @param buf Destination. @param len Count.
   *  @return -1. */
  int read(uint8_t *buf, size_t len) {
    (void)buf;
    (void)len;
    return -1;
  }
};

/** @brief Stand-in for SdFat's FatVolume; never mounts. */
class FatVolume {
public:
  /** @brief Mount a volume on a flash chip. @param flash Ignored.
   *  @return false. */
  bool begin(Adafruit_SPIFlash *flash) {
    (void)flash;
    return false;
  }
  /** @brief Drop the block cache. */
  void cacheClear(void) {}
  /** @brief Open a file. @param path Ignored. @param mode Ignored.
   *  @return A closed handle. */
  File32 open(const char *path, const char *mode) {
    (void)path;
    (void)mode;
    return File32();
  }
};

#endif // _HOST_SDFAT_ADAFRUIT_FORK_H_
