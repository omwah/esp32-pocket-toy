#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include "monster_controller.h"
#include "audio.h"
#include "package_storage.h"

class WebControl {
public:
    WebControl(MonsterController &eyes, Audio &audio, int &battery)
        : _eyes(eyes), _audio(audio), _battery(battery), _server(80) {}
    void begin(bool forceProvisioning = false);
    void update(uint32_t now);
    void stop();
    void setEnabled(bool on);
    bool enabled() const { return _enabled; }
    void manualStyleSelected();
    bool provisioning() const { return _provisioning; }
    bool configured() const { return _configured; }
    bool connected() const { return WiFi.status() == WL_CONNECTED; }
    bool cycleEnabled() const { return _cycle; }
    const char *setupSsid() const { return _apName.c_str(); }
    const char *setupPassword() const { return _apPassword.c_str(); }
    String ipAddress() const {
        return connected() ? WiFi.localIP().toString()
                           : WiFi.softAPIP().toString();
    }

private:
    MonsterController &_eyes;
    Audio &_audio;
    int &_battery;
    WebServer _server;
    DNSServer _dns;
    PackageStorage _storage;
    String _uploadError;
    bool _started = false, _listening = false, _enabled = true;
    bool _provisioning = false, _configured = false, _cycle = false;
    uint32_t _interval = 30000, _nextCycle = 0, _connectStarted = 0;
    String _serialLine, _apName, _apPassword;
    void connectSaved();
    void startProvisioning();
    void startServer();
    void handleProvision();
    void handleSerial();
    void handleUploadData();
    String statusJson() const;
};
