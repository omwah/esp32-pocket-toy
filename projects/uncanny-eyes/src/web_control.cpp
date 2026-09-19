#include "web_control.h"
#include <Preferences.h>

namespace {
Preferences prefs;

String jsonEscape(const char *text) {
  String out;
  while (*text) {
    char c = *text++;
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<meta charset="utf-8"><title>Uncanny Eyes</title><style>
body{font:16px system-ui;max-width:42rem;margin:auto;padding:1rem;background:#10091a;color:#fff}
section{background:#241634;padding:1rem;margin:.8rem 0;border-radius:.8rem}button,select,input{font:inherit;padding:.65rem;margin:.25rem;border-radius:.4rem;border:0}button{background:#7851a9;color:#fff}.grid{display:grid;grid-template-columns:1fr 1fr;gap:.5rem}.wide{width:100%}small{color:#bbb}
</style></head><body><h1>Uncanny Eyes</h1><section id=status>Connecting...</section>
<section><label>Style</label><select id=style class=wide></select><div class=grid><button onclick="post('/api/style/previous')">Previous</button><button onclick="post('/api/style/next')">Next</button></div></section>
<section><div class=grid><button id=mute onclick=toggleMute()>Mute</button><button id=cycle onclick=toggleCycle()>Enable cycle</button></div><label>Cycle interval (seconds)</label><input id=interval type=number min=5 max=3600 value=30><button onclick=saveCycle()>Apply</button></section>
<section id=setup hidden><h2>Wi-Fi setup</h2><form method=post action=/provision><input name=ssid placeholder="Network name" required class=wide><input name=password type=password placeholder="Password" class=wide><button type=submit>Connect and save</button></form><small>Credentials are stored in device NVS, not in firmware.</small></section>
<script>
const ui={status:document.getElementById('status'),style:document.getElementById('style'),mute:document.getElementById('mute'),cycle:document.getElementById('cycle'),interval:document.getElementById('interval'),setup:document.getElementById('setup')};let s={}; async function post(url,data={}){await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});await refresh()}
async function refresh(){try{s=await(await fetch('/api/status')).json();ui.status.innerHTML=`<b>Mode:</b> ${s.mode}<br><b>Style:</b> ${s.styleName}<br><b>Battery:</b> ${s.batteryPercent===null?'Unavailable':s.batteryPercent+'%'}<br><b>External power:</b> ${s.externalPower}<br><b>Wi-Fi:</b> ${s.wifi} ${s.ip||''}`;if(ui.style.options.length!==s.styles.length){ui.style.innerHTML=s.styles.map((x,i)=>`<option value=${i}>${x}</option>`).join('');ui.style.onchange=()=>post('/api/style',{style:ui.style.value})}ui.style.value=s.style;ui.mute.textContent=s.muted?'Unmute':'Mute';ui.cycle.textContent=s.cycleEnabled?'Disable cycle':'Enable cycle';ui.interval.value=s.cycleIntervalSeconds;ui.setup.hidden=!s.provisioning}catch(e){ui.status.textContent='Device unavailable'}}
function toggleMute(){post('/api/audio',{muted:!s.muted})}function toggleCycle(){post('/api/cycle',{enabled:!s.cycleEnabled,interval:ui.interval.value})}function saveCycle(){post('/api/cycle',{enabled:s.cycleEnabled,interval:ui.interval.value})}refresh();setInterval(refresh,2500)
</script></body></html>)HTML";
}

WebControl::WebControl(Eyes &eyes, Audio &audio)
    : _eyes(eyes), _audio(audio), _server(80) {}

void WebControl::begin(bool forceProvisioning) {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06llX", (unsigned long long)(mac & 0xFFFFFF));
  _apName = "UncannyEyes-" + String(suffix);
  _apPassword = "eyes-" + String(suffix);
  WiFi.setHostname(_apName.c_str());
  if (forceProvisioning) startProvisioning(); else connectSaved();
}

void WebControl::connectSaved() {
  prefs.begin("uncanny-wifi", false);
  String ssid = prefs.getString("ssid");
  String password = prefs.getString("password");
  prefs.end();
  _configured = ssid.length() > 0;
  if (!_configured) { startProvisioning(); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  password = String();
  _connectStartedAt = millis();
  Serial.printf("Wi-Fi connecting to %s\n", ssid.c_str());
  startServer();
}

void WebControl::startProvisioning() {
  if (_provisioning) return;
  _provisioning = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_apName.c_str(), _apPassword.c_str());
  _dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("Wi-Fi setup: join %s with password %s, then open http://%s/\n",
                _apName.c_str(), _apPassword.c_str(), WiFi.softAPIP().toString().c_str());
  startServer();
}

void WebControl::startServer() {
  if (_serverStarted) return;
  _server.on("/", HTTP_GET, [this] { handleRoot(); });
  _server.on("/api/status", HTTP_GET, [this] { handleStatus(); });
  _server.on("/api/style", HTTP_POST, [this] {
    if (_server.hasArg("style")) { _eyes.setStyle(_server.arg("style").toInt()); manualStyleSelected(); }
    _server.send(204);
  });
  _server.on("/api/style/next", HTTP_POST, [this] { _eyes.nextStyle(); manualStyleSelected(); _server.send(204); });
  _server.on("/api/style/previous", HTTP_POST, [this] { _eyes.previousStyle(); manualStyleSelected(); _server.send(204); });
  _server.on("/api/audio", HTTP_POST, [this] {
    if (_server.hasArg("muted")) _audio.setMuted(_server.arg("muted") == "true" || _server.arg("muted") == "1");
    _server.send(204);
  });
  _server.on("/api/cycle", HTTP_POST, [this] {
    if (_server.hasArg("interval")) _cycleIntervalMs = constrain(_server.arg("interval").toInt(), 5, 3600) * 1000UL;
    if (_server.hasArg("enabled")) _cycleEnabled = _server.arg("enabled") == "true" || _server.arg("enabled") == "1";
    _nextCycleAt = millis() + _cycleIntervalMs;
    _server.send(204);
  });
  _server.on("/provision", HTTP_POST, [this] { handleProvision(); });
  _server.onNotFound([this] { _server.sendHeader("Location", "/", true); _server.send(302); });
  _server.begin();
  _serverStarted = true;
}

void WebControl::handleRoot() { _server.send_P(200, "text/html; charset=utf-8", PAGE); }
void WebControl::handleStatus() { _server.send(200, "application/json", statusJson()); }

void WebControl::handleProvision() {
  if (!_provisioning || !_server.hasArg("ssid")) { _server.send(403, "text/plain", "Provisioning is not active"); return; }
  String ssid = _server.arg("ssid");
  String password = _server.arg("password");
  prefs.begin("uncanny-wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();
  _configured = true;
  password = String();
  _server.send(200, "text/html; charset=utf-8", "<p>Credentials saved. Rebooting...</p>");
  delay(500);
  ESP.restart();
}

String WebControl::statusJson() const {
  String out = "{\"mode\":\"" + String(modeName()) + "\",\"style\":" + _eyes.style();
  out += ",\"styleName\":\"" + jsonEscape(_eyes.styleName()) + "\",\"styleCount\":" + _eyes.styleCount();
  out += ",\"muted\":" + String(_audio.muted() ? "true" : "false");
  out += ",\"soundAvailable\":" + String((_audio.present() && _eyes.style() < 11) ? "true" : "false");
  out += ",\"cycleEnabled\":" + String(_cycleEnabled ? "true" : "false");
  out += ",\"cycleIntervalSeconds\":" + String(_cycleIntervalMs / 1000);
  int battery = _eyes.batteryPercent();
  out += ",\"batteryPercent\":" + String(battery < 0 ? "null" : String(battery));
  out += ",\"externalPower\":\"unknown\"";
  out += ",\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "connected" : (_provisioning ? "provisioning" : "connecting")) + "\"";
  out += ",\"ip\":\"" + String(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  out += ",\"provisioning\":" + String(_provisioning ? "true" : "false") + ",\"styles\":[";
  for (uint8_t i = 0; i < _eyes.styleCount(); ++i) {
    if (i) out += ',';
    out += "\"" + jsonEscape(_eyes.styleNameAt(i)) + "\"";
  }
  out += "]}";
  return out;
}

void WebControl::manualStyleSelected() {
  _cycleEnabled = false;
  _nextCycleAt = 0;
}

void WebControl::clearCredentials() {
  prefs.begin("uncanny-wifi", false);
  prefs.clear();
  prefs.end();
  Serial.println("Wi-Fi credentials cleared; restarting");
  delay(200);
  ESP.restart();
}

void WebControl::handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n' && _serialLine.length() < 96) { _serialLine += c; continue; }
    _serialLine.trim();
    if (_serialLine == "wifi status") {
      Serial.printf("wifi=%s ip=%s provisioning=%s\n", WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                    WiFi.localIP().toString().c_str(), _provisioning ? "yes" : "no");
    } else if (_serialLine == "wifi provision") {
      startProvisioning();
    } else if (_serialLine == "wifi clear") {
      Serial.println("Type: confirm wifi clear");
    } else if (_serialLine == "confirm wifi clear") {
      clearCredentials();
    } else if (_serialLine == "wifi reconnect") {
      WiFi.reconnect();
    } else if (_serialLine.length()) {
      Serial.println("Commands: wifi status | wifi provision | wifi clear | wifi reconnect");
    }
    _serialLine = "";
  }
}

void WebControl::update(uint32_t nowMs) {
  handleSerial();
  _server.handleClient();
  if (_provisioning) _dns.processNextRequest();
  if (!_provisioning && WiFi.status() != WL_CONNECTED && nowMs - _connectStartedAt > 15000) startProvisioning();
  if (_cycleEnabled && (int32_t)(nowMs - _nextCycleAt) >= 0) {
    _eyes.nextStyle();
    _nextCycleAt = nowMs + _cycleIntervalMs;
  }
}

void WebControl::stop() {
  _dns.stop();
  _server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}
