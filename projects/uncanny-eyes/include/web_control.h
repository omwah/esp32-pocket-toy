#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include "audio.h"
#include "eyes.h"

class WebControl {
public:
  WebControl(Eyes &eyes, Audio &audio);
  void begin(bool forceProvisioning = false);
  void update(uint32_t nowMs);
  void stop();
  void manualStyleSelected();
  bool cycleEnabled() const { return _cycleEnabled; }
  const char *modeName() const { return _cycleEnabled ? "Cycle" : "Manual"; }
  bool provisioning() const { return _provisioning; }
  bool configured() const { return _configured; }
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  const char *setupSsid() const { return _apName.c_str(); }
  const char *setupPassword() const { return _apPassword.c_str(); }
  String ipAddress() const {
    return connected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  }

private:
  Eyes &_eyes;
  Audio &_audio;
  WebServer _server;
  DNSServer _dns;
  bool _serverStarted = false;
  bool _provisioning = false;
  bool _configured = false;
  bool _cycleEnabled = false;
  uint32_t _cycleIntervalMs = 30000;
  uint32_t _nextCycleAt = 0;
  uint32_t _connectStartedAt = 0;
  String _serialLine;
  String _apName;
  String _apPassword;

  void connectSaved();
  void startProvisioning();
  void startServer();
  void handleRoot();
  void handleStatus();
  void handleProvision();
  void handleSerial();
  void clearCredentials();
  String statusJson() const;
};
