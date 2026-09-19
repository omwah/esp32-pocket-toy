#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>

class Audio {
public:
  bool begin();
  void setPackage(const char *configPath);
  void update(uint32_t now);
  void setMuted(bool muted);
  bool muted() const { return _muted; }
  bool present() const { return _ok; }
  bool hasSound() const { return !_sounds.empty(); }
  void stop();
private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool initCodec();
  bool loadWav(const String &path);
  static void taskEntry(void *arg);
  void taskLoop();
  void schedule(uint32_t now);

  bool _ok=false, _muted=false;
  TaskHandle_t _task=nullptr;
  portMUX_TYPE _mux=portMUX_INITIALIZER_UNLOCKED;
  std::vector<String> _sounds;
  uint8_t *_data=nullptr;
  size_t _capacity=0, _length=0, _position=0;
  uint8_t _bits=0, _channels=0;
  uint32_t _rate=0, _nextPlay=0, _minInterval=12000, _maxInterval=30000;
  volatile bool _playing=false;
};
