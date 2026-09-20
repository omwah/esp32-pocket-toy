/**
 * @file Arduino.cpp
 * @brief Host implementations of the Arduino core calls the eye library makes.
 */

#include "Arduino.h"

#include <time.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
//  CLOCK
// ---------------------------------------------------------------------------

static bool sRealTime = false;   ///< Follow the host clock rather than counting
static uint64_t sVirtualUs = 0;  ///< Virtual microseconds since start
static uint64_t sRealOriginNs = 0; ///< Host clock reading at first use

static uint64_t monotonicNs(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void hostClockUseRealTime(bool on) {
  sRealTime = on;
  if (on)
    sRealOriginNs = monotonicNs();
}

void hostClockAdvance(uint32_t us) { sVirtualUs += us; }

void hostClockSet(uint64_t us) { sVirtualUs = us; }

// The library subtracts timestamps and compares the difference, so both of
// these are free to wrap at 32 bits exactly as the core's do.
uint32_t micros(void) {
  if (sRealTime)
    return (uint32_t)((monotonicNs() - sRealOriginNs) / 1000ULL);
  return (uint32_t)sVirtualUs;
}

uint32_t millis(void) {
  if (sRealTime)
    return (uint32_t)((monotonicNs() - sRealOriginNs) / 1000000ULL);
  return (uint32_t)(sVirtualUs / 1000ULL);
}

// Sleeping for real would make a headless capture take as long as the sequence
// it renders. Under the virtual clock a delay is just time passing.
void delay(uint32_t ms) {
  if (sRealTime)
    usleep((useconds_t)ms * 1000);
  else
    sVirtualUs += (uint64_t)ms * 1000ULL;
}

void delayMicroseconds(uint32_t us) {
  if (sRealTime)
    usleep((useconds_t)us);
  else
    sVirtualUs += us;
}

void yield(void) {}

// ---------------------------------------------------------------------------
//  RANDOMNESS
// ---------------------------------------------------------------------------
//
// A 32-bit xorshift rather than the host's rand(): the sequence has to depend
// on nothing but the seed, so a capture made on one machine matches one made on
// another, and nothing else in the process can disturb it.

static uint32_t sRandState = 1;  ///< Generator state
static bool sSeedPinned = false; ///< Ignore the library's own randomSeed()

static uint32_t nextRandom(void) {
  uint32_t x = sRandState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  sRandState = x;
  return x;
}

void hostRandomForceSeed(uint32_t seed) {
  sRandState = seed ? seed : 1;
  sSeedPinned = true;
}

void randomSeed(uint32_t seed) {
  if (sSeedPinned)
    return;
  sRandState = seed ? seed : 1;
}

long random(long limit) {
  if (limit <= 0)
    return 0;
  return (long)(nextRandom() % (uint32_t)limit);
}

long random(long lower, long upper) {
  if (upper <= lower)
    return lower;
  return lower + random(upper - lower);
}

// ---------------------------------------------------------------------------
//  GPIO
// ---------------------------------------------------------------------------

void pinMode(int pin, int mode) {
  (void)pin;
  (void)mode;
}

void digitalWrite(int pin, int value) {
  (void)pin;
  (void)value;
}

// HIGH reads as "button not pressed" for the active-low safe-mode button, so
// the simulator never wanders into USB drive mode.
int digitalRead(int pin) {
  (void)pin;
  return HIGH;
}

long map(long x, long inMin, long inMax, long outMin, long outMax) {
  if (inMax == inMin)
    return outMin;
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// ---------------------------------------------------------------------------
//  PRINT AND STREAM
// ---------------------------------------------------------------------------

size_t Print::write(const uint8_t *buf, size_t len) {
  size_t n = 0;
  while (n < len)
    n += write(buf[n]);
  return n;
}

size_t Print::printf(const char *fmt, ...) {
  char stack[256];
  va_list ap;
  va_start(ap, fmt);
  const int need = vsnprintf(stack, sizeof(stack), fmt, ap);
  va_end(ap);
  if (need < 0)
    return 0;
  if ((size_t)need < sizeof(stack))
    return write((const uint8_t *)stack, (size_t)need);

  // Startup narration can run past the stack buffer; take the heap rather than
  // truncate, since a clipped diagnostic is worse than useless.
  char *heap = (char *)malloc((size_t)need + 1);
  if (!heap)
    return write((const uint8_t *)stack, sizeof(stack) - 1);
  va_start(ap, fmt);
  vsnprintf(heap, (size_t)need + 1, fmt, ap);
  va_end(ap);
  const size_t n = write((const uint8_t *)heap, (size_t)need);
  free(heap);
  return n;
}

size_t Print::print(const char *s) {
  return write((const uint8_t *)s, strlen(s));
}

size_t Print::println(const char *s) {
  const size_t n = print(s);
  return n + write((uint8_t)'\n');
}

// The default is one read() per byte. File overrides it with a single fread,
// which matters: config.eye goes through this a character at a time otherwise.
size_t Stream::readBytes(char *buf, size_t len) {
  size_t n = 0;
  while (n < len) {
    const int c = read();
    if (c < 0)
      break;
    buf[n++] = (char)c;
  }
  return n;
}

size_t HostSerial::write(uint8_t c) {
  return _out ? fwrite(&c, 1, 1, _out) : 0;
}

size_t HostSerial::write(const uint8_t *buf, size_t len) {
  return _out ? fwrite(buf, 1, len, _out) : 0;
}

void HostSerial::flush(void) {
  if (_out)
    fflush(_out);
}

// Diagnostics go to stderr so that stdout stays clean for anything the
// simulator is asked to emit as data.
HostSerial Serial(stderr);

// ---------------------------------------------------------------------------
//  ESP CORE OBJECT
// ---------------------------------------------------------------------------

static uint32_t sHeapSize = 8u * 1024u * 1024u; ///< Heap the shim claims

uint32_t HostEspClass::getFreeHeap(void) const { return sHeapSize; }

void HostEspClass::restart(void) {
  Serial.printf("Library asked for a reboot; exiting.\n");
  exit(0);
}

HostEspClass ESP;

uint32_t getCpuFrequencyMhz(void) { return 240; }

void hostHeapSetSize(uint32_t bytes) { sHeapSize = bytes; }
