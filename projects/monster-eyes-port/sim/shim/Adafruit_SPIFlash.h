/**
 * @file Adafruit_SPIFlash.h
 * @brief Stub flash driver, present only so Eyes_Assets.cpp compiles.
 *
 * On ESP32 the asset filesystem is FFat and this driver is unused, but the USB
 * drive-mode code that references it is not behind an #if, so the type and its
 * methods have to exist. Every one of them fails, which is the honest answer:
 * there is no flash chip on a Linux host, and the simulator never calls
 * runDriveMode() anyway.
 */

#ifndef _HOST_ADAFRUIT_SPIFLASH_H_
#define _HOST_ADAFRUIT_SPIFLASH_H_

#include "Arduino.h"

/** @brief Stand-in for the ESP32 flash transport. */
class Adafruit_FlashTransport_ESP32 {};

/** @brief Stand-in for the QSPI flash transport. */
class Adafruit_FlashTransport_QSPI {};

/** @brief Flash chip driver that reports no chip. */
class Adafruit_SPIFlash {
public:
  /** @param transport Ignored. */
  explicit Adafruit_SPIFlash(Adafruit_FlashTransport_ESP32 *transport) {
    (void)transport;
  }
  /** @brief Bring the chip up. @return Always false; there is no chip. */
  bool begin(void) { return false; }
  /** @brief Chip identity. @return Zero. */
  uint32_t getJEDECID(void) { return 0; }
  /** @brief Chip capacity. @return Zero. */
  uint32_t size(void) { return 0; }
  /** @brief Read 512-byte blocks.
   *  @param lba Block. @param buf Destination. @param count Blocks.
   *  @return false. */
  bool readBlocks(uint32_t lba, uint8_t *buf, uint32_t count) {
    (void)lba;
    (void)buf;
    (void)count;
    return false;
  }
  /** @brief Write 512-byte blocks.
   *  @param lba Block. @param buf Source. @param count Blocks.
   *  @return false. */
  bool writeBlocks(uint32_t lba, uint8_t *buf, uint32_t count) {
    (void)lba;
    (void)buf;
    (void)count;
    return false;
  }
  /** @brief Flush pending writes. @return false. */
  bool syncBlocks(void) { return false; }
};

#endif // _HOST_ADAFRUIT_SPIFLASH_H_
