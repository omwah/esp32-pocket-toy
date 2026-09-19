#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Audio {
public:
  bool begin();
  void playStyle(uint8_t style);
  void update();
  void stop();
  bool present() const { return _ok; }
private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool initCodec();
  static void taskEntry(void *arg);
  void taskLoop();

  bool _ok = false;
  TaskHandle_t _task = nullptr;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
  const int16_t *_sample = nullptr;
  uint32_t _length = 0;
  uint32_t _position = 0;
  volatile uint32_t _blocksWritten = 0;
  volatile uint32_t _writeErrors = 0;
  volatile uint32_t _maxWriteUs = 0;
  uint32_t _lastDebugMs = 0;
};
