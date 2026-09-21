#include "web_control.h"
#include <Preferences.h>
#include <FFat.h>
#include "web_page.h"

namespace {

Preferences prefs;

// This board brings no external-power signal out to a GPIO: every unassigned
// pin was measured holding the same state with USB in and out (see
// projects/power-diagnostics). The USB peripheral's own view is the best
// available, and it is narrower than "external power" -- it follows
// start-of-frame packets, so it means a USB host is connected. A dumb charger
// sends none and reads here as battery.
const char *powerSource() {
    return HWCDC::isPlugged() ? "usb" : "battery";
}

String esc(const char *s) {
    String o;
    while (*s) {
        char c = *s++;
        if (c == '"' || c == '\\') o += '\\';
        if (uint8_t(c) >= 32) o += c;
    }
    return o;
}

}  // namespace

void WebControl::begin(bool force) {
    _storage.begin();
    uint64_t m = ESP.getEfuseMac();
    char x[7];
    snprintf(x, sizeof(x), "%06llX", (unsigned long long)(m & 0xffffff));
    _apName = "MonsterEyes-" + String(x);
    _apPassword = "eyes-" + String(x);
    WiFi.setHostname(_apName.c_str());
    prefs.begin("monster-web", true);
    _cycle = prefs.getBool("cycle", false);
    _interval = constrain(prefs.getUInt("interval", 30000), 5000UL, 3600000UL);
    _enabled = prefs.getBool("wifi", true);
    prefs.end();
    _nextCycle = millis() + _interval;
    if (force) _enabled = true;
    if (!_enabled) {
        WiFi.mode(WIFI_OFF);
        Serial.println("Wi-Fi off (stored state)");
        return;
    }
    if (force)
        startProvisioning();
    else
        connectSaved();
}

void WebControl::connectSaved() {
    prefs.begin("monster-wifi", true);
    String s = prefs.getString("ssid"), p = prefs.getString("password");
    prefs.end();
    _configured = s.length();
    if (!_configured) {
        startProvisioning();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.c_str(), p.c_str());
    p = "";
    _connectStarted = millis();
    startServer();
}

void WebControl::startProvisioning() {
    if (_provisioning) return;
    _provisioning = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_apName.c_str(), _apPassword.c_str());
    _dns.start(53, "*", WiFi.softAPIP());
    Serial.printf("Wi-Fi setup: %s password %s http://%s/\n", _apName.c_str(),
                  _apPassword.c_str(), WiFi.softAPIP().toString().c_str());
    startServer();
}

void WebControl::startServer() {
    if (_started) {
        if (!_listening) {
            _server.begin();
            _listening = true;
        }
        return;
    }
    _server.on("/", HTTP_GET, [this] {
        _server.sendHeader("Content-Encoding", "gzip");
        _server.send_P(200, "text/html; charset=utf-8", (PGM_P)WEB_PAGE_GZ,
                       WEB_PAGE_GZ_LEN);
    });
    _server.on("/api/status", HTTP_GET,
               [this] { _server.send(200, "application/json", statusJson()); });
    _server.on("/api/audio/mute", HTTP_POST, [this] {
        if (_server.hasArg("muted"))
            _audio.setMuted(_server.arg("muted") == "true" ||
                            _server.arg("muted") == "1");
        _server.send(204);
    });
    _server.on("/api/audio/volume", HTTP_POST, [this] {
        if (_server.hasArg("volume"))
            _audio.setVolume(_server.arg("volume").toInt());
        _server.send(204);
    });
    _server.on("/api/audio/play", HTTP_POST, [this] {
        // 409 rather than 400: the request is well formed, there is just
        // nothing to play right now.
        _server.send(_audio.playNext() ? 204 : 409);
    });
    _server.on("/api/display/brightness", HTTP_POST, [this] {
        if (_server.hasArg("brightness"))
            _backlight.setPercent(_server.arg("brightness").toInt());
        _server.send(204);
    });
    _server.on("/api/style", HTTP_POST, [this] {
        if (_server.hasArg("style")) {
            _eyes.setStyle(_server.arg("style").toInt());
            manualStyleSelected();
        }
        _server.send(204);
    });
    _server.on("/api/style/next", HTTP_POST, [this] {
        _eyes.nextStyle();
        manualStyleSelected();
        _server.send(204);
    });
    _server.on("/api/style/previous", HTTP_POST, [this] {
        _eyes.previousStyle();
        manualStyleSelected();
        _server.send(204);
    });
    _server.on("/api/eyes/enabled", HTTP_POST, [this] {
        if (!_server.hasArg("style") || !_server.hasArg("enabled")) {
            _server.send(400);
            return;
        }
        bool ok = _eyes.setStyleEnabled(
            _server.arg("style").toInt(),
            _server.arg("enabled") == "true" || _server.arg("enabled") == "1");
        _server.send(ok ? 204 : 409);
    });
    _server.on("/api/frame", HTTP_GET, [this] { handleFrame(); });
    _server.on("/api/packages/order", HTTP_POST, [this] {
        bool ok = _server.hasArg("order") &&
                  _eyes.setPackageOrder(_server.arg("order"));
        _server.send(ok ? 204 : 400, "text/plain",
                     ok ? "" : "invalid package order");
    });
    _server.on("/api/packages/upload/start", HTTP_POST, [this] {
        String token, error;
        bool ok = _server.hasArg("id") &&
                  _storage.startUpload(_server.arg("id"), token, error);
        _server.send(ok ? 200 : 400, ok ? "application/json" : "text/plain",
                     ok ? String("{\"token\":\"") + token + "\"}" : error);
    });
    _server.on(
        "/api/packages/upload/file", HTTP_POST,
        [this] {
            _server.send(_uploadError.length() ? 400 : 204, "text/plain",
                         _uploadError);
            _uploadError = "";
        },
        [this] { handleUploadData(); });
    _server.on("/api/packages/upload/commit", HTTP_POST, [this] {
        String error, id = _storage.uploadId(), token = _server.arg("token");
        bool ok = _storage.commit(token, error);
        if (ok) {
            // An unsaved edit belongs to the config that was here a moment
            // ago. Keeping it would make the upload look ignored: the file on
            // the drive would be the new one and the eyes would still be
            // running the old one with the edit on top.
            _eyes.clearLiveConfig();
            ok = _eyes.reloadPackages(id);
        }
        _server.send(ok ? 201 : 400, "text/plain", ok ? "" : error);
    });
    _server.on("/api/packages/download", HTTP_GET, [this] {
        String id = _server.arg("id"), path = _server.arg("path");
        if (!PackageStorage::validId(id) ||
            !PackageStorage::validRelativePath(path)) {
            _server.send(400, "text/plain", "invalid path");
            return;
        }
        File f = FFat.open(_storage.livePath(id) + "/" + path);
        if (!f) {
            _server.send(404, "text/plain", "not found");
            return;
        }
        _server.sendHeader("Content-Disposition",
                           "attachment; filename=\"" +
                               path.substring(path.lastIndexOf('/') + 1) +
                               "\"");
        _server.streamFile(f, "application/octet-stream");
        f.close();
    });
    _server.on("/api/packages/rename", HTTP_POST, [this] {
        String id = _server.arg("id"), n = _server.arg("newId"), error;
        if (id == _eyes.packageId(_eyes.style())) {
            _server.send(409, "text/plain", "select another package first");
            return;
        }
        bool ok = _storage.renamePackage(id, n, error);
        if (ok) ok = _eyes.reloadPackages();
        _server.send(ok ? 204 : 400, "text/plain", ok ? "" : error);
    });
    _server.on("/api/packages/delete", HTTP_POST, [this] {
        String id = _server.arg("id"), error;
        if (_eyes.styleCount() <= 1 || id == _eyes.packageId(_eyes.style())) {
            _server.send(409, "text/plain",
                         "cannot delete active or final package");
            return;
        }
        bool ok = _storage.deletePackage(id, error);
        if (ok) ok = _eyes.reloadPackages();
        _server.send(ok ? 204 : 400, "text/plain", ok ? "" : error);
    });
    // config.eye editing. GET hands back the text the eyes are running on,
    // which is the unsaved edit if there is one. POST /apply tries a config
    // without writing anything: it lives in RAM and is gone at the next
    // reboot. /overwrite and /save-as are the two ways to keep it.
    _server.on("/api/config", HTTP_GET, [this] {
        String text = _eyes.configText();
        _server.sendHeader("X-Config-Unsaved",
                           _eyes.hasLiveConfig() ? "1" : "0");
        _server.send(text.length() ? 200 : 404, "application/json",
                     text.length() ? text : String("{}"));
    });
    _server.on("/api/config/apply", HTTP_POST, [this] {
        String error;
        bool ok = _server.hasArg("plain") &&
                  _eyes.applyConfigText(_server.arg("plain"), error);
        _server.send(ok ? 204 : 400, "text/plain", ok ? "" : error);
    });
    _server.on("/api/config/overwrite", HTTP_POST, [this] {
        String error, id = _eyes.packageId(_eyes.style());
        bool ok = _server.hasArg("plain") &&
                  _eyes.applyConfigText(_server.arg("plain"), error) &&
                  _storage.writeConfig(id, _server.arg("plain"), error);
        if (ok) {
            // The file now says what RAM says, so drop the unsaved copy and
            // rebuild from the drive -- the state a reboot would land in.
            _eyes.clearLiveConfig();
            ok = _eyes.reloadPackages(id);
        }
        _server.send(ok ? 204 : 400, "text/plain", ok ? "" : error);
    });
    _server.on("/api/config/revert", HTTP_POST, [this] {
        // Drop the unsaved edit and rebuild from the drive.
        String id = _eyes.packageId(_eyes.style());
        _eyes.clearLiveConfig();
        _server.send(_eyes.reloadPackages(id) ? 204 : 500);
    });
    _server.on("/api/config/save-as", HTTP_POST, [this] {
        String error, id = _eyes.packageId(_eyes.style()),
                      newId = _server.arg("id");
        if (!_server.hasArg("plain") || !newId.length()) {
            _server.send(400, "text/plain", "id and config required");
            return;
        }
        bool ok = _storage.copyPackage(id, newId, _server.arg("plain"), error);
        if (ok) {
            _eyes.clearLiveConfig();
            ok = _eyes.reloadPackages(newId);
            manualStyleSelected();
        }
        _server.send(ok ? 201 : 400, "text/plain", ok ? "" : error);
    });
    _server.on("/api/cycle", HTTP_POST, [this] {
        if (_server.hasArg("interval"))
            _interval =
                constrain(_server.arg("interval").toInt(), 5, 3600) * 1000UL;
        if (_server.hasArg("enabled"))
            _cycle = _server.arg("enabled") == "true" ||
                     _server.arg("enabled") == "1";
        _nextCycle = millis() + _interval;
        prefs.begin("monster-web", false);
        prefs.putBool("cycle", _cycle);
        prefs.putUInt("interval", _interval);
        prefs.end();
        _server.send(204);
    });
    _server.on("/provision", HTTP_POST, [this] { handleProvision(); });
    _server.onNotFound([this] {
        _server.sendHeader("Location", "/", true);
        _server.send(302);
    });
    _server.begin();
    _started = true;
    _listening = true;
}

void WebControl::handleProvision() {
    if (!_provisioning || !_server.hasArg("ssid")) {
        _server.send(403);
        return;
    }
    prefs.begin("monster-wifi", false);
    prefs.putString("ssid", _server.arg("ssid"));
    prefs.putString("password", _server.arg("password"));
    prefs.end();
    _server.send(200, "text/html", "Credentials saved. Rebooting…");
    delay(500);
    ESP.restart();
}

String WebControl::statusJson() const {
    String o =
        "{\"mode\":\"" + String(_cycle ? "Cycle" : "Manual") +
        "\",\"style\":" + String(_eyes.style()) + ",\"styleName\":\"" +
        esc(_eyes.styleName(_eyes.style())) +
        "\",\"styleCount\":" + String(_eyes.styleCount()) +
        ",\"batteryPercent\":" +
        (_battery < 0 ? String("null") : String(_battery)) +
        ",\"audioPresent\":" + (_audio.present() ? "true" : "false") +
        ",\"hasSound\":" + (_audio.hasSound() ? "true" : "false") +
        ",\"muted\":" + (_audio.muted() ? "true" : "false") +
        ",\"volume\":" + String(_audio.volume()) +
        ",\"brightness\":" + String(_backlight.percent()) +
        ",\"externalPower\":\"" + powerSource() +
        "\",\"cycle\":" + (_cycle ? "true" : "false") +
        ",\"interval\":" + String(_interval / 1000) + ",\"wifi\":\"" +
        (connected() ? "connected"
                     : (_provisioning ? "provisioning" : "connecting")) +
        // Which network, since "connected" tells a page that has already
        // loaded over that network nothing it does not know. While
        // provisioning it is the setup access point the phone is looking for;
        // while connecting it is what the board is reaching for.
        "\",\"ssid\":\"" +
        esc(_provisioning ? _apName.c_str() : WiFi.SSID().c_str()) +
        "\",\"ip\":\"" +
        (connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) +
        "\",\"configured\":" + (_configured ? "true" : "false") +
        ",\"provisioning\":" + (_provisioning ? "true" : "false") +
        ",\"styles\":[";
    for (uint8_t i = 0; i < _eyes.styleCount(); ++i) {
        if (i) o += ',';
        o += '"' + esc(_eyes.styleName(i)) + '"';
    }
    o += "],\"enabled\":[";
    for (uint8_t i = 0; i < _eyes.styleCount(); ++i) {
        if (i) o += ',';
        o += _eyes.styleEnabled(i) ? "true" : "false";
    }
    o += "],\"ids\":[";
    for (uint8_t i = 0; i < _eyes.styleCount(); ++i) {
        if (i) o += ',';
        o += '"' + esc(_eyes.packageId(i)) + '"';
    }
    return o + "]}";
}

void WebControl::manualStyleSelected() {
    _cycle = false;
    _nextCycle = 0;
    prefs.begin("monster-web", false);
    prefs.putBool("cycle", false);
    prefs.end();
}

void WebControl::handleUploadData() {
    HTTPUpload &u = _server.upload();
    if (u.status == UPLOAD_FILE_START) {
        _uploadError = "";
        String token = _server.arg("token");
        if (!_storage.beginFile(token, u.filename, _uploadError)) return;
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (!_uploadError.length())
            _storage.writeFile(u.buf, u.currentSize, _uploadError);
    } else if (u.status == UPLOAD_FILE_END) {
        if (!_uploadError.length()) _storage.endFile(_uploadError);
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        _storage.cancel();
        _uploadError = "upload aborted";
    }
}

// GET /api/frame -- a screenshot of the panel as a 24-bit BMP.
//
// The panel cannot be read back, so the renderer draws one more frame with the
// backend's mirror armed and the mirror is what gets served. Only the two eye
// squares and the background colour are in it: the Wi-Fi and sound icons and
// the touch controls are drawn straight to the TFT by the sketch and never
// pass through the backend.
//
// BMP rather than PNG because it needs no compressor: the rows go out as they
// are converted, so nothing but one row is ever held in memory.
void WebControl::handleFrame() {
    const uint16_t *px = _eyes.captureFrame();
    if (!px) {
        _server.send(503, "text/plain", "no capture buffer");
        return;
    }
    const int w = CompositeTftDisplay::PANEL_W;
    const int h = CompositeTftDisplay::PANEL_H;
    const uint32_t rowBytes = uint32_t(w) * 3;  // 960: already 4-byte aligned
    const uint32_t body = rowBytes * h, total = 54 + body;

    uint8_t head[54] = {0};
    auto put32 = [&head](int at, uint32_t v) {
        head[at] = v;
        head[at + 1] = v >> 8;
        head[at + 2] = v >> 16;
        head[at + 3] = v >> 24;
    };
    head[0] = 'B';
    head[1] = 'M';
    put32(2, total);
    put32(10, 54);
    put32(14, 40);
    put32(18, uint32_t(w));
    put32(22, uint32_t(h));
    head[26] = 1;               // planes
    head[28] = 24;              // bits per pixel
    put32(34, body);
    put32(38, 2835);            // 72 dpi, in pixels per metre
    put32(42, 2835);

    uint8_t *row = (uint8_t *)malloc(rowBytes);
    if (!row) {
        _server.send(503, "text/plain", "out of memory");
        return;
    }
    _server.setContentLength(total);
    _server.send(200, "image/bmp", "");
    _server.sendContent((const char *)head, sizeof(head));
    // BMP rows run bottom-up, and each pixel is stored blue first.
    for (int y = h - 1; y >= 0; --y) {
        const uint16_t *src = &px[size_t(y) * w];
        for (int x = 0; x < w; ++x) {
            const uint16_t v = src[x];
            const uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            // Replicate the high bits into the low ones so full-scale values
            // land on 255 rather than 248.
            row[x * 3] = uint8_t((b << 3) | (b >> 2));
            row[x * 3 + 1] = uint8_t((g << 2) | (g >> 4));
            row[x * 3 + 2] = uint8_t((r << 3) | (r >> 2));
        }
        _server.sendContent((const char *)row, rowBytes);
    }
    free(row);
}

void WebControl::handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c != '\n' && _serialLine.length() < 96) {
            _serialLine += c;
            continue;
        }
        _serialLine.trim();
        if (_serialLine == "wifi status")
            Serial.printf("wifi=%s radio=%s ip=%s provisioning=%s\n",
                          connected() ? "connected" : "disconnected",
                          _enabled ? "on" : "off",
                          (connected() ? WiFi.localIP() : WiFi.softAPIP())
                              .toString()
                              .c_str(),
                          _provisioning ? "yes" : "no");
        else if (_serialLine == "wifi provision")
            startProvisioning();
        else if (_serialLine == "wifi reconnect")
            WiFi.reconnect();
        else if (_serialLine == "wifi on")
            setEnabled(true);
        else if (_serialLine == "wifi off")
            setEnabled(false);
        else if (_serialLine == "wifi clear")
            Serial.println("Type: confirm wifi clear");
        else if (_serialLine == "confirm wifi clear") {
            prefs.begin("monster-wifi", false);
            prefs.clear();
            prefs.end();
            ESP.restart();
        } else if (_serialLine == "next")
            _eyes.nextStyle();
        else if (_serialLine == "previous")
            _eyes.previousStyle();
        _serialLine = "";
    }
}

void WebControl::update(uint32_t now) {
    handleSerial();
    if (_enabled) {
        _server.handleClient();
        if (_provisioning) _dns.processNextRequest();
        if (!_provisioning && !connected() &&
            int32_t(now - _connectStarted) > 15000)
            startProvisioning();
    }
    if (_cycle && int32_t(now - _nextCycle) >= 0) {
        _eyes.nextStyle();
        _nextCycle = now + _interval;
    }
}

void WebControl::setEnabled(bool on) {
    if (_enabled == on) return;
    _enabled = on;
    prefs.begin("monster-web", false);
    prefs.putBool("wifi", on);
    prefs.end();
    if (on) {
        _provisioning = false;
        connectSaved();
        Serial.println("Wi-Fi on");
        return;
    }
    _dns.stop();
    _server.stop();
    _listening = false;
    _provisioning = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("Wi-Fi off");
}

void WebControl::stop() {
    _dns.stop();
    _server.stop();
    _listening = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}
