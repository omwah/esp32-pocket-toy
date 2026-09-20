/**
 * @file Arduino.h
 * @brief The slice of the Arduino core the eye library actually calls,
 *        implemented for a Linux host.
 *
 * The library is deliberately plain C++ apart from a handful of core calls, so
 * this header is small: a clock, a pseudo-random generator, Print/Stream, and
 * enough GPIO to satisfy code paths the simulator never takes.
 *
 * The simulator compiles the library sources unmodified, with
 * -DARDUINO_ARCH_ESP32, so Eyes_Platform.h and Eyes_Assets.cpp select the same
 * preprocessor branches they do on the device. The ESP-specific headers those
 * branches reach for are shimmed alongside this one.
 *
 * THE CLOCK IS VIRTUAL BY DEFAULT. millis() and micros() return a counter the
 * simulator advances by a fixed step per rendered frame, so a headless capture
 * is reproducible: same seed and same frame count give byte-identical output.
 * Windowed mode switches the clock to the real monotonic clock, where the frame
 * rate the library reports is the one it is really achieving.
 */

#ifndef _HOST_ARDUINO_H_
#define _HOST_ARDUINO_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

// ---------------------------------------------------------------------------
//  CLOCK
// ---------------------------------------------------------------------------

/** @brief Run on the host's monotonic clock rather than the virtual one. */
void hostClockUseRealTime(bool on);

/** @brief Advance the virtual clock. @param us Microseconds to add. */
void hostClockAdvance(uint32_t us);

/** @brief Set the virtual clock. @param us Microseconds since start. */
void hostClockSet(uint64_t us);

uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);

// ---------------------------------------------------------------------------
//  RANDOMNESS
// ---------------------------------------------------------------------------
//
// The library seeds itself from micros() at begin(). That is reproducible under
// the virtual clock, but only accidentally, so the simulator can also pin the
// seed outright with hostRandomForceSeed() and have randomSeed() ignored.

/** @brief Pin the generator and make the library's randomSeed() a no-op.
 *  @param seed Seed value. */
void hostRandomForceSeed(uint32_t seed);

void randomSeed(uint32_t seed);
long random(long limit);
long random(long lower, long upper);

// ---------------------------------------------------------------------------
//  GPIO
// ---------------------------------------------------------------------------
//
// Nothing here reaches hardware. The library calls these only on paths the
// simulator does not take (drive mode, the safe-mode button), and the panel
// backend's reset pins are all -1.

#define INPUT 0        ///< Pin direction, for source compatibility
#define OUTPUT 1       ///< Pin direction, for source compatibility
#define INPUT_PULLUP 2 ///< Pin direction, for source compatibility
#define LOW 0          ///< Pin level, for source compatibility
#define HIGH 1         ///< Pin level, for source compatibility

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);

/** @brief Arduino's integer rescale. */
long map(long x, long inMin, long inMax, long outMin, long outMax);

// The core defines min() and max() as macros, so sketches use them on mixed
// types without a cast. Templates here instead: a macro would collide with
// std::min and std::max, which <math.h> drags in through <cmath>. The common
// type is worked out rather than fixed, because the library calls
// min((uint32_t)1000000, someUint32) and max(1, someInt) in the same file.

/** @brief Smaller of two values, of possibly different types. */
template <typename A, typename B>
constexpr typename std::common_type<A, B>::type min(A a, B b) {
  using C = typename std::common_type<A, B>::type;
  return (C)a < (C)b ? (C)a : (C)b;
}

/** @brief Larger of two values, of possibly different types. */
template <typename A, typename B>
constexpr typename std::common_type<A, B>::type max(A a, B b) {
  using C = typename std::common_type<A, B>::type;
  return (C)a > (C)b ? (C)a : (C)b;
}

// ---------------------------------------------------------------------------
//  PRINT AND STREAM
// ---------------------------------------------------------------------------

/**
 * @brief Arduino's output base class, enough of it for the library's logging.
 *
 * Only printf() and write() are used; the rest of Print is not worth carrying.
 */
class Print {
public:
  virtual ~Print() {}
  /** @brief Write one byte. @param c Byte. @return Bytes written. */
  virtual size_t write(uint8_t c) = 0;
  /** @brief Write a buffer.
   *  @param buf Bytes. @param len Count. @return Bytes written. */
  virtual size_t write(const uint8_t *buf, size_t len);
  /** @brief printf-style output. @param fmt Format. @return Bytes written. */
  size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  /** @brief Write a C string. @param s String. @return Bytes written. */
  size_t print(const char *s);
  /** @brief Write a C string and a newline.
   *  @param s String. @return Bytes written. */
  size_t println(const char *s);
};

/**
 * @brief Arduino's interface for objects that can print themselves.
 *
 * Nothing in the eye library implements it, but ArduinoJson offers a converter
 * for it whenever it sees an Arduino core, so the type has to exist.
 */
class Printable {
public:
  virtual ~Printable() {}
  /** @brief Write a representation of this object.
   *  @param p Destination. @return Bytes written. */
  virtual size_t printTo(Print &p) const = 0;
};

/**
 * @brief Arduino's input base class.
 *
 * ArduinoJson reads config.eye straight out of one of these, so available()
 * and read() have to behave: read() returns -1 at end of file, not 0.
 */
class Stream : public Print {
public:
  /** @brief Bytes readable without blocking. @return Count. */
  virtual int available(void) { return 0; }
  /** @brief Read one byte. @return Byte, or -1 at end of input. */
  virtual int read(void) { return -1; }
  /** @brief Look at the next byte without consuming it.
   *  @return Byte, or -1. */
  virtual int peek(void) { return -1; }
  /** @brief Flush buffered output. */
  virtual void flush(void) {}
  /**
   * @brief Read a block; ArduinoJson's stream reader calls this per character.
   * @param buf Destination.
   * @param len Bytes wanted.
   * @return Bytes actually read, short at end of input.
   */
  virtual size_t readBytes(char *buf, size_t len);
};

/** @brief A Stream that writes to a C stdio handle and reads nothing. */
class HostSerial : public Stream {
public:
  /** @param out Destination handle, e.g. stderr. */
  explicit HostSerial(FILE *out) : _out(out) {}
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buf, size_t len) override;
  void flush(void) override;

private:
  FILE *_out; ///< Where bytes go
};

/** Stands in for the Arduino core's Serial; writes to stderr. */
extern HostSerial Serial;

// ---------------------------------------------------------------------------
//  ESP CORE OBJECT
// ---------------------------------------------------------------------------
//
// Eyes_Platform.h's ESP32 block calls these three. The heap numbers are
// fictions large enough that the texture loader never decimates on the host --
// the point of the simulator is to see the eye the renderer would draw with
// memory to spare, and MONSTER_EYES_TEXTURE_BUDGET can narrow it deliberately
// when the question is what the device will manage.

/** @brief The Arduino-ESP32 core's global system object, host version. */
class HostEspClass {
public:
  /** @brief Reported free heap. @return Bytes. */
  uint32_t getFreeHeap(void) const;
  /** @brief Pretend to reboot; the simulator exits instead. */
  void restart(void);
};

extern HostEspClass ESP; ///< Stands in for the core's ESP object

/** @brief Reported CPU clock. @return Megahertz. */
uint32_t getCpuFrequencyMhz(void);

/** @brief Set the heap size the shim reports. @param bytes Heap to claim. */
void hostHeapSetSize(uint32_t bytes);

#endif // _HOST_ARDUINO_H_
