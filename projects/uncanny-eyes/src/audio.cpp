#include "audio.h"
#include "config.h"
#include "audio_assets.h"
#include <Wire.h>
#include <driver/i2s.h>

namespace { constexpr i2s_port_t PORT = I2S_NUM_0; constexpr int FRAMES = 256; }

bool Audio::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ES8311_I2C_ADDR); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool Audio::initCodec() {
  auto readReg=[](uint8_t reg) {
    Wire.beginTransmission(ES8311_I2C_ADDR); Wire.write(reg);
    if (Wire.endTransmission(false) || Wire.requestFrom((int)ES8311_I2C_ADDR,1)!=1) return -1;
    return Wire.read();
  };
  if (readReg(0xFD)!=0x83 || readReg(0xFE)!=0x11) return false;
  writeReg(0x00,0x1F); delay(20); writeReg(0x45,0x00);
  writeReg(0x01,0x3F); writeReg(0x02,0x00); writeReg(0x03,0x10);
  writeReg(0x16,0x24); writeReg(0x04,0x10); writeReg(0x05,0x00);
  writeReg(0x06,0x03); writeReg(0x07,0x00); writeReg(0x08,0xFF);
  writeReg(0x09,0x00); writeReg(0x0A,0x00); writeReg(0x0B,0x00);
  writeReg(0x0C,0x00); writeReg(0x10,0x1F); writeReg(0x11,0x7F);
  writeReg(0x00,0x80); delay(50); writeReg(0x0D,0x01); writeReg(0x0E,0x02);
  writeReg(0x12,0x00); writeReg(0x13,0x10); writeReg(0x14,0x1A);
  writeReg(0x37,0x08); writeReg(0x32,0xAF); writeReg(0x31,0x00);
  return true;
}

bool Audio::begin() {
  pinMode(SPK_ENABLE,OUTPUT); digitalWrite(SPK_ENABLE,HIGH);
  if (!initCodec()) { Serial.println("audio codec unavailable"); return false; }
  i2s_config_t cfg={}; cfg.mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX);
  cfg.sample_rate=AUDIO_RATE; cfg.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT; cfg.communication_format=I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags=ESP_INTR_FLAG_LEVEL1; cfg.dma_buf_count=6; cfg.dma_buf_len=FRAMES;
  cfg.tx_desc_auto_clear=true; cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;
  if (i2s_driver_install(PORT,&cfg,0,nullptr)!=ESP_OK) return false;
  i2s_pin_config_t pins={}; pins.mck_io_num=I2S_MCLK; pins.bck_io_num=I2S_BCLK;
  pins.ws_io_num=I2S_LRCK; pins.data_out_num=I2S_DOUT; pins.data_in_num=I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(PORT,&pins)!=ESP_OK) return false;
  i2s_zero_dma_buffer(PORT);
  _ok=true;
  if (xTaskCreatePinnedToCore(taskEntry,"eye-audio",4096,this,3,&_task,0)!=pdPASS) {
    Serial.println("audio: task creation failed"); _ok=false; return false;
  }
  digitalWrite(SPK_ENABLE,LOW);
  Serial.println("audio: streaming task started on core 0");
  return true;
}

void Audio::playStyle(uint8_t style) {
  if (!_ok || _muted || style>=11) return;
  // Hazel normally breathes quietly; roughly one event in five is a sigh.
  uint8_t sampleIndex = (style == 0 && random(5) == 0) ? 11 : style;
  portENTER_CRITICAL(&_mux);
  _sample=AUDIO_SAMPLES[sampleIndex].data;
  _length=AUDIO_SAMPLES[sampleIndex].length;
  _position=0;
  portEXIT_CRITICAL(&_mux);
  Serial.printf("audio: play style=%u sample=%u samples=%u\n",
                style,sampleIndex,(unsigned)_length);
}

void Audio::taskEntry(void *arg) { static_cast<Audio *>(arg)->taskLoop(); }

void Audio::taskLoop() {
  int16_t stereo[FRAMES*2];
  while (true) {
    // This task is the sole producer for I2S and blocks until DMA has room.
    // It runs independently of the expensive eye renderer on the other core.
    portENTER_CRITICAL(&_mux);
    for (int i=0;i<FRAMES;++i) {
      int16_t value=0;
      if (_sample && _position<_length) value=_sample[_position++];
      stereo[i*2]=value; stereo[i*2+1]=value;
      if (_sample && _position>=_length) _sample=nullptr;
    }
    portEXIT_CRITICAL(&_mux);

    size_t written=0; uint32_t started=micros();
    esp_err_t result=i2s_write(PORT,stereo,sizeof(stereo),&written,portMAX_DELAY);
    uint32_t elapsed=micros()-started;
    if (elapsed>_maxWriteUs) _maxWriteUs=elapsed;
    if (result!=ESP_OK || written!=sizeof(stereo)) ++_writeErrors;
    ++_blocksWritten;
  }
}

void Audio::update() {
  if (!_ok) return;
  uint32_t now=millis();
  if (now-_lastDebugMs>=5000) {
    _lastDebugMs=now;
    bool active;
    portENTER_CRITICAL(&_mux); active=_sample!=nullptr; portEXIT_CRITICAL(&_mux);
    Serial.printf("audio: blocks=%u errors=%u max_write_us=%u active=%u\n",
      (unsigned)_blocksWritten,(unsigned)_writeErrors,(unsigned)_maxWriteUs,active);
    _maxWriteUs=0;
  }
}

void Audio::setMuted(bool muted) {
  if (_muted == muted) return;
  _muted = muted;
  portENTER_CRITICAL(&_mux);
  if (muted) { _sample=nullptr; _position=_length=0; }
  portEXIT_CRITICAL(&_mux);
  if (_ok) digitalWrite(SPK_ENABLE, muted ? HIGH : LOW);
}

void Audio::stop() {
  portENTER_CRITICAL(&_mux);
  _sample=nullptr; _position=_length=0;
  portEXIT_CRITICAL(&_mux);
  if (_ok) { i2s_zero_dma_buffer(PORT); digitalWrite(SPK_ENABLE,HIGH); }
}
