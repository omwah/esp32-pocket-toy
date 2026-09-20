/**
 * @file Adafruit_TinyUSB.h
 * @brief Stub USB mass-storage device, present only so Eyes_Assets.cpp
 *        compiles. The simulator never enters drive mode.
 */

#ifndef _HOST_ADAFRUIT_TINYUSB_H_
#define _HOST_ADAFRUIT_TINYUSB_H_

#include "Arduino.h"

/** @brief Callback the host uses to read blocks. */
typedef int32_t (*msc_read_cb_t)(uint32_t lba, void *buffer, uint32_t bufsize);
/** @brief Callback the host uses to write blocks. */
typedef int32_t (*msc_write_cb_t)(uint32_t lba, uint8_t *buffer,
                                  uint32_t bufsize);
/** @brief Callback run when the host flushes. */
typedef void (*msc_flush_cb_t)(void);

/** @brief Stand-in for TinyUSB's mass-storage class; does nothing. */
class Adafruit_USBD_MSC {
public:
  /** @brief Set the SCSI inquiry strings.
   *  @param vendor Vendor. @param product Product. @param revision Revision. */
  void setID(const char *vendor, const char *product, const char *revision) {
    (void)vendor;
    (void)product;
    (void)revision;
  }
  /** @brief Declare the medium size.
   *  @param blockCount Blocks. @param blockSize Bytes per block. */
  void setCapacity(uint32_t blockCount, uint16_t blockSize) {
    (void)blockCount;
    (void)blockSize;
  }
  /** @brief Install the block callbacks.
   *  @param rd Read. @param wr Write. @param fl Flush. */
  void setReadWriteCallback(msc_read_cb_t rd, msc_write_cb_t wr,
                            msc_flush_cb_t fl) {
    (void)rd;
    (void)wr;
    (void)fl;
  }
  /** @brief Report the medium present. @param ready State. */
  void setUnitReady(bool ready) { (void)ready; }
  /** @brief Start the device. @return false. */
  bool begin(void) { return false; }
};

#endif // _HOST_ADAFRUIT_TINYUSB_H_
