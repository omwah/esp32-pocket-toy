#include "audio.h"
#include "board_config.h"
#include <ArduinoJson.h>
#include <FFat.h>
#include <Preferences.h>
#include <Wire.h>
#include <driver/i2s.h>

namespace {

constexpr i2s_port_t PORT = I2S_NUM_0;
constexpr int FRAMES = 256;

uint8_t dacVolume(uint8_t percent) {
    return percent ? uint8_t(111 + percent * 80 / 100) : 0;
}

uint16_t le16(const uint8_t *p) {
    return p[0] | uint16_t(p[1]) << 8;
}

uint32_t le32(const uint8_t *p) {
    return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
           uint32_t(p[3]) << 24;
}

}  // namespace

bool Audio::writeReg(uint8_t r, uint8_t v) {
    Wire.beginTransmission(ES8311_I2C_ADDR);
    Wire.write(r);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool Audio::initCodec() {
    auto rd = [](uint8_t r) {
        Wire.beginTransmission(ES8311_I2C_ADDR);
        Wire.write(r);
        if (Wire.endTransmission(false) ||
            Wire.requestFrom((int)ES8311_I2C_ADDR, 1) != 1)
            return -1;
        return Wire.read();
    };
    if (rd(0xFD) != 0x83 || rd(0xFE) != 0x11) return false;
    writeReg(0x00, 0x1F);
    delay(20);
    writeReg(0x45, 0);
    writeReg(0x01, 0x3F);
    writeReg(0x02, 0);
    writeReg(0x03, 0x10);
    writeReg(0x16, 0x24);
    writeReg(0x04, 0x10);
    writeReg(0x05, 0);
    writeReg(0x06, 3);
    writeReg(0x07, 0);
    writeReg(0x08, 0xFF);
    writeReg(0x09, 0);
    writeReg(0x0A, 0);
    writeReg(0x0B, 0);
    writeReg(0x0C, 0);
    writeReg(0x10, 0x1F);
    writeReg(0x11, 0x7F);
    writeReg(0x00, 0x80);
    delay(50);
    writeReg(0x0D, 1);
    writeReg(0x0E, 2);
    writeReg(0x12, 0);
    writeReg(0x13, 0x10);
    writeReg(0x14, 0x1A);
    writeReg(0x37, 8);
    writeReg(0x32, 0xAF);
    writeReg(0x31, 0);
    return true;
}

bool Audio::begin() {
    pinMode(AUDIO_AMP_ENABLE, OUTPUT);
    digitalWrite(AUDIO_AMP_ENABLE, HIGH);
    Preferences p;
    p.begin("monster-audio", true);
    _muted = p.getBool("muted", false);
    _volume = constrain(p.getUChar("volume", 70), 0, 100);
    p.end();
    if (!initCodec()) {
        Serial.println("audio codec unavailable");
        return false;
    }
    writeReg(0x32, dacVolume(_volume));
    i2s_config_t c = {};
    c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    c.sample_rate = 22050;
    c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    c.dma_buf_count = 6;
    c.dma_buf_len = FRAMES;
    c.tx_desc_auto_clear = true;
    c.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_driver_install(PORT, &c, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_MCLK;
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(PORT, &pins) != ESP_OK) return false;
    _ok = true;
    if (xTaskCreatePinnedToCore(taskEntry, "eye-audio", 4096, this, 3, &_task,
                                0) != pdPASS) {
        _ok = false;
        return false;
    }
    digitalWrite(AUDIO_AMP_ENABLE, _muted ? HIGH : LOW);
    return true;
}

void Audio::schedule(uint32_t now) {
    _nextPlay = now + random(_minInterval, _maxInterval + 1);
}

void Audio::setPackage(const char *configPath) {
    portENTER_CRITICAL(&_mux);
    _playing = false;
    _length = _position = 0;
    portEXIT_CRITICAL(&_mux);
    _sounds.clear();
    _minInterval = 12000;
    _maxInterval = 30000;
    File f = FFat.open(configPath);
    JsonDocument d;
    if (!f || deserializeJson(d, f)) {
        schedule(millis());
        return;
    }
    JsonVariant a = d["extensions"]["audio"];
    _minInterval = max(uint32_t(1000), a["minInterval"] | uint32_t(12000));
    _maxInterval = max(_minInterval, a["maxInterval"] | uint32_t(30000));
    String base = configPath;
    base = base.substring(0, base.lastIndexOf('/') + 1);
    for (JsonVariant v : a["sounds"].as<JsonArray>()) {
        String p = v.as<String>();
        if (!p.startsWith("/")) p = base + p;
        if (FFat.exists(p)) _sounds.push_back(p);
    }
    schedule(millis());
    Serial.printf("audio: package has %u sound(s)\n", unsigned(_sounds.size()));
}

bool Audio::loadWav(const String &path) {
    File f = FFat.open(path);
    if (!f) return false;
    uint8_t h[12];
    if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return false;
    uint16_t format = 0;
    uint32_t dataSize = 0, dataAt = 0;
    while (f.available()) {
        uint8_t ch[8];
        if (f.read(ch, 8) != 8) break;
        uint32_t n = le32(ch + 4), at = f.position();
        if (!memcmp(ch, "fmt ", 4)) {
            uint8_t fmt[16];
            if (n < 16 || f.read(fmt, 16) != 16) return false;
            format = le16(fmt);
            _channels = le16(fmt + 2);
            _rate = le32(fmt + 4);
            _bits = le16(fmt + 14);
        } else if (!memcmp(ch, "data", 4)) {
            dataAt = at;
            dataSize = n;
            break;
        }
        f.seek(at + n + (n & 1));
    }
    if (format != 1 || (_bits != 8 && _bits != 16) || !_channels ||
        _channels > 2 || !_rate || !dataSize)
        return false;
    if (dataSize > _capacity) {
        uint8_t *p = (uint8_t *)ps_realloc(_data, dataSize);
        if (!p) return false;
        _data = p;
        _capacity = dataSize;
    }
    f.seek(dataAt);
    if (f.read(_data, dataSize) != dataSize) return false;
    i2s_set_clk(PORT, _rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    portENTER_CRITICAL(&_mux);
    _length = dataSize;
    _position = 0;
    _playing = true;
    portEXIT_CRITICAL(&_mux);
    Serial.printf("audio: playing %s\n", path.c_str());
    return true;
}

void Audio::update(uint32_t now) {
    if (!_ok || _muted || _sounds.empty() || _playing ||
        int32_t(now - _nextPlay) < 0)
        return;
    loadWav(_sounds[random(_sounds.size())]);
    schedule(now);
}

void Audio::setMuted(bool m) {
    if (_muted == m) return;
    _muted = m;
    if (m) {
        portENTER_CRITICAL(&_mux);
        _playing = false;
        _length = _position = 0;
        portEXIT_CRITICAL(&_mux);
    }
    digitalWrite(AUDIO_AMP_ENABLE, m ? HIGH : LOW);
    Preferences p;
    p.begin("monster-audio", false);
    p.putBool("muted", m);
    p.end();
}

void Audio::setVolume(uint8_t p) {
    p = constrain(p, 0, 100);
    if (_volume == p) return;
    _volume = p;
    if (_ok) writeReg(0x32, dacVolume(p));
    Preferences pr;
    pr.begin("monster-audio", false);
    pr.putUChar("volume", p);
    pr.end();
}

void Audio::taskEntry(void *x) {
    static_cast<Audio *>(x)->taskLoop();
}

void Audio::taskLoop() {
    int16_t out[FRAMES * 2];
    for (;;) {
        portENTER_CRITICAL(&_mux);
        for (int i = 0; i < FRAMES; i++) {
            int16_t v = 0;
            if (_playing && _position < _length) {
                if (_bits == 8)
                    v = (int16_t(int(_data[_position]) - 128)) << 8;
                else
                    v = int16_t(_data[_position] |
                                uint16_t(_data[_position + 1]) << 8);
                _position += (_bits / 8) * _channels;
                if (_position >= _length) _playing = false;
            }
            out[2 * i] = out[2 * i + 1] = v;
        }
        portEXIT_CRITICAL(&_mux);
        size_t n;
        i2s_write(PORT, out, sizeof(out), &n, portMAX_DELAY);
    }
}

void Audio::stop() {
    portENTER_CRITICAL(&_mux);
    _playing = false;
    _length = _position = 0;
    portEXIT_CRITICAL(&_mux);
    if (_ok) {
        i2s_zero_dma_buffer(PORT);
        digitalWrite(AUDIO_AMP_ENABLE, HIGH);
    }
}
