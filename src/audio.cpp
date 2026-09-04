#include "audio.h"

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>

namespace {

constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
// One DMA block. update() fills as many of these as the queue will take, so
// output keeps up regardless of the frame rate.
constexpr int FRAMES = 256;

// Fast approximate sine over a 0..1 phase. A parabola pair is plenty for
// effects work and avoids sinf() in the inner mixing loop.
inline float fastSin(float p) {
  p -= (int)p;
  float x = p * 2.0f - 1.0f;
  float y = 4.0f * x * (1.0f - fabsf(x));   // parabolic approximation
  return y;
}

// 32-bit xorshift, used as the noise source for explosions and impacts.
inline float noiseStep(uint32_t &s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return (float)(int32_t)s * (1.0f / 2147483648.0f);
}

// Simple one-pole low pass, per voice, to take the edge off raw noise.
inline float lp(float in, float &state, float k) {
  state += (in - state) * k;
  return state;
}

}  // namespace

bool Audio::writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool Audio::initCodec() {
  // Confirm the part before touching anything: 0xFD/0xFE read back 0x83/0x11 on
  // an ES8311. The touch controller shares this bus, so writing a register
  // sequence blind to the wrong address would be unpleasant.
  //
  // Each register must be addressed individually. The ES8311 does not
  // auto-increment its address pointer, so a two-byte sequential read returns
  // 0xFD's value followed by 0xFF rather than 0xFD and 0xFE.
  auto readReg = [](uint8_t reg) -> int {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((int)ES8311_I2C_ADDR, 1) != 1) return -1;
    return Wire.read();
  };

  int id1 = readReg(0xFD), id2 = readReg(0xFE);
  if (id1 != 0x83 || id2 != 0x11) {
    Serial.printf("audio: unexpected codec id %02X %02X\n", id1, id2);
    return false;
  }

  writeReg(0x00, 0x1F); delay(20);   // reset
  writeReg(0x45, 0x00);

  // Clocking for 16 kHz from MCLK = 256 * fs = 4.096 MHz.
  writeReg(0x01, 0x3F);              // all internal clocks on, MCLK from pad
  writeReg(0x02, 0x00);              // no MCLK pre-divider
  writeReg(0x03, 0x10);              // ADC oversample ratio
  writeReg(0x16, 0x24);
  writeReg(0x04, 0x10);              // DAC oversample ratio
  writeReg(0x05, 0x00);              // no ADC/DAC divider
  writeReg(0x06, 0x03);              // BCLK divider
  writeReg(0x07, 0x00);              // LRCK period, high byte
  writeReg(0x08, 0xFF);              // LRCK period, low byte
  writeReg(0x09, 0x00);              // serial input:  16-bit I2S
  writeReg(0x0A, 0x00);              // serial output: 16-bit I2S
  writeReg(0x0B, 0x00);
  writeReg(0x0C, 0x00);
  writeReg(0x10, 0x1F);              // bias and charge pump
  writeReg(0x11, 0x7F);

  // Start the chip state machine. Everything above is only staged
  // configuration: without this write the codec accepts every register and
  // reads them all back correctly, but never actually runs, and the output is
  // silent with no error anywhere to indicate why.
  writeReg(0x00, 0x80);
  delay(50);

  writeReg(0x0D, 0x01);              // power up analogue
  writeReg(0x0E, 0x02);              // power up the DAC
  writeReg(0x12, 0x00);              // DAC enabled
  writeReg(0x13, 0x10);              // output enabled
  writeReg(0x14, 0x1A);
  writeReg(0x37, 0x08);
  writeReg(0x32, 0xBF);              // DAC volume
  writeReg(0x31, 0x00);              // unmute
  return true;
}

bool Audio::begin() {
  for (auto &v : _voices) v.active = false;

  // The amplifier enable is active LOW on this board -- driving it high mutes.
  pinMode(SPK_ENABLE, OUTPUT);
  digitalWrite(SPK_ENABLE, HIGH);  // keep muted until the codec is configured

  if (!initCodec()) {
    Serial.println("audio: codec init failed, running silent");
    return false;
  }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AUDIO_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = FRAMES;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;   // emit silence rather than repeat on underrun
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  if (i2s_driver_install(I2S_PORT, &cfg, 0, nullptr) != ESP_OK) {
    Serial.println("audio: i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_MCLK;
  pins.bck_io_num   = I2S_BCLK;
  pins.ws_io_num    = I2S_LRCK;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin(I2S_PORT, &pins) != ESP_OK) {
    Serial.println("audio: i2s_set_pin failed");
    return false;
  }

  i2s_zero_dma_buffer(I2S_PORT);
  // Amplifier enable is active LOW on this board (confirmed by test, and it
  // matches the vendor documentation).
  digitalWrite(SPK_ENABLE, LOW);
  _ok = true;
  Serial.println("audio: ES8311 ready");
  return true;
}

int Audio::allocVoice(float gain) {
  for (int i = 0; i < AUDIO_VOICES; i++)
    if (!_voices[i].active) return i;

  // All busy: steal whichever voice is furthest through its envelope, so a
  // sustained explosion is not cut short by a stream of little laser shots.
  int best = 0;
  float bestProgress = -1;
  for (int i = 0; i < AUDIO_VOICES; i++) {
    float p = (float)_voices[i].t / (float)_voices[i].len;
    if (p > bestProgress) { bestProgress = p; best = i; }
  }
  return (bestProgress > 0.45f) ? best : -1;
}

void Audio::play(SfxKind kind, float gain, float pan) {
  if (!_ok) return;

  int idx = allocVoice(gain);
  if (idx < 0) return;

  Voice &v = _voices[idx];
  v.kind  = kind;
  v.t     = 0;
  v.phase = 0;
  v.noise = 0x2545F491u ^ (uint32_t)micros();

  // Equal-ish power panning, cheap form.
  pan = constrain(pan, -1.0f, 1.0f);
  v.gainL = gain * (0.5f - 0.5f * pan) + gain * 0.25f;
  v.gainR = gain * (0.5f + 0.5f * pan) + gain * 0.25f;

  switch (kind) {
    case SFX_LASER:
      // Short descending chirp: the classic pew, built from a fast downward
      // sweep so it cuts through without needing much level.
      v.len  = AUDIO_RATE * 0.09f;
      v.freq = 1500.0f + (float)random(0, 500);
      v.freqK = 0.99965f;
      break;

    case SFX_CANNON:
      // Lower, longer, and sweeping further -- reads as a heavier weapon.
      v.len  = AUDIO_RATE * 0.26f;
      v.freq = 420.0f + (float)random(0, 90);
      v.freqK = 0.99985f;
      break;

    case SFX_HIT:
      v.len  = AUDIO_RATE * 0.06f;
      v.freq = 900.0f;
      v.freqK = 0.9990f;
      break;

    case SFX_EXPLOSION:
      v.len  = AUDIO_RATE * 0.55f;
      v.freq = 220.0f;
      v.freqK = 0.99990f;
      break;

    case SFX_RUMBLE:
      v.len  = AUDIO_RATE * 1.10f;
      v.freq = 70.0f;
      v.freqK = 0.99996f;
      break;
  }
  v.active = true;
}

void Audio::update() {
  if (!_ok) return;

  static int16_t buf[FRAMES * 2];
  static float   lpState[AUDIO_VOICES] = {0};

  // Generate against elapsed time, not once per frame. A frame at 20 fps is
  // 50 ms apart, while one buffer is only 16 ms of audio, so producing a single
  // buffer per frame starves the DMA queue and the output is mostly silence
  // broken by fragments. Keep filling until the queue refuses more.
  for (int block = 0; block < 8; block++) {
    bool anyActive = false;
    for (const auto &v : _voices) if (v.active) { anyActive = true; break; }

    for (int n = 0; n < FRAMES; n++) {
      float l = 0, r = 0;

      for (int i = 0; i < AUDIO_VOICES; i++) {
        Voice &v = _voices[i];
        if (!v.active) continue;

        float prog = (float)v.t / (float)v.len;
        float s = 0;

        switch (v.kind) {
          case SFX_LASER:
          case SFX_CANNON:
          case SFX_HIT: {
            v.phase += v.freq / AUDIO_RATE;
            if (v.phase > 1.0f) v.phase -= 1.0f;
            float tone = fastSin(v.phase);
            float grit = noiseStep(v.noise) * (v.kind == SFX_HIT ? 0.7f : 0.18f);
            float env = expf(-prog * (v.kind == SFX_HIT ? 9.0f : 4.5f));
            s = (tone + grit) * env;
            v.freq *= v.freqK;
            break;
          }

          case SFX_EXPLOSION: {
            // Filtered noise over a falling tone. The low pass sweeps down with
            // the envelope, which gives it a body rather than a burst of static.
            float nz = noiseStep(v.noise);
            float k = 0.55f * (1.0f - prog) + 0.05f;
            float body = lp(nz, lpState[i], k);
            v.phase += v.freq / AUDIO_RATE;
            if (v.phase > 1.0f) v.phase -= 1.0f;
            float env = expf(-prog * 3.2f);
            s = (body * 1.5f + fastSin(v.phase) * 0.55f) * env;
            v.freq *= v.freqK;
            break;
          }

          case SFX_RUMBLE: {
            v.phase += v.freq / AUDIO_RATE;
            if (v.phase > 1.0f) v.phase -= 1.0f;
            float env = expf(-prog * 2.4f) * (prog < 0.06f ? prog / 0.06f : 1.0f);
            s = (fastSin(v.phase) + fastSin(v.phase * 1.5f) * 0.4f) * env;
            v.freq *= v.freqK;
            break;
          }
        }

        l += s * v.gainL;
        r += s * v.gainR;

        if (++v.t >= v.len) v.active = false;
      }

      // Soft clip. A hard clamp on a busy mix sounds like tearing; saturation
      // keeps loud moments intact.
      auto sat = [](float x) -> float {
        if (x > 1.0f)  return 1.0f - 0.25f / x;
        if (x < -1.0f) return -1.0f + 0.25f / -x;
        return x - x * x * x * 0.16f;
      };

      l = sat(l * 0.85f);
      r = sat(r * 0.85f);

      buf[n * 2]     = (int16_t)(l * 30000.0f);
      buf[n * 2 + 1] = (int16_t)(r * 30000.0f);
    }

    // Non-blocking: if the queue is full the frame loop must not stall waiting
    // on audio. A short timeout lets the first block land when the queue has
    // just drained, without ever holding up the renderer for long.
    size_t written = 0;
    i2s_write(I2S_PORT, buf, sizeof(buf), &written, block == 0 ? 2 : 0);
    if (written < sizeof(buf)) break;   // queue full, done for this frame
    if (!anyActive) break;              // nothing playing, no point filling more
  }
}
