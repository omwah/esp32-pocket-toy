/**
 * @file esp_heap_caps.h
 * @brief ESP-IDF's capability allocator, mapped onto plain malloc.
 *
 * Eyes_Platform.h's ESP32 block steers the polar maps and textures into
 * internal RAM, away from PSRAM, because a random external read per pixel costs
 * more than the arithmetic around it. A Linux host has one flat heap and no
 * such trap, so the capability flags are simply ignored.
 *
 * The largest-free-block figure decides how much texture detail the loader
 * keeps. Reporting a generous number gives the simulator full-resolution
 * textures; hostHeapSetSize() narrows it deliberately when the question is what
 * the device will actually manage.
 */

#ifndef _HOST_ESP_HEAP_CAPS_H_
#define _HOST_ESP_HEAP_CAPS_H_

#include "Arduino.h"

#define MALLOC_CAP_INTERNAL 0x800 ///< Ignored on the host
#define MALLOC_CAP_SPIRAM 0x400   ///< Ignored on the host
#define MALLOC_CAP_8BIT 0x004     ///< Ignored on the host
#define MALLOC_CAP_DMA 0x008      ///< Ignored on the host

/** @brief Allocate, ignoring capabilities.
 *  @param n Bytes. @param caps Ignored. @return Pointer, or NULL. */
static inline void *heap_caps_malloc(size_t n, uint32_t caps) {
  (void)caps;
  return malloc(n);
}

/** @brief Largest allocation available.
 *  @param caps Ignored. @return Bytes, as set by hostHeapSetSize(). */
static inline uint32_t heap_caps_get_largest_free_block(uint32_t caps) {
  (void)caps;
  return ESP.getFreeHeap();
}

#endif // _HOST_ESP_HEAP_CAPS_H_
