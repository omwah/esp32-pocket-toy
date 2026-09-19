#include "monster_controller.h"
#include <FFat.h>
#include <Preferences.h>
#include <algorithm>
#include <new>

namespace {
String enabledKey(const String &id) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < id.length(); ++i) {
    hash ^= uint8_t(id[i]);
    hash *= 16777619u;
  }
  char key[12];
  snprintf(key, sizeof(key), "e%08lx", (unsigned long)hash);
  return key;
}
}

MonsterController::~MonsterController() {
  if (_eyes) _eyes->~Adafruit_Monster_Eyes();
}

const char *MonsterController::styleName(uint8_t style) const {
  return style < _packages.size() ? _packages[style].name.c_str() : "Unknown";
}
const char *MonsterController::packageId(uint8_t style) const {
  return style < _packages.size() ? _packages[style].id.c_str() : "";
}
int MonsterController::packageIndex(const String &id) const {
  for (size_t i=0;i<_packages.size();++i) if (_packages[i].id==id) return i;
  return -1;
}

bool MonsterController::refreshPackages() {
  _packages.clear();
  if (!FFat.begin(false)) return false;
  File root = FFat.open("/eyes");
  if (!root || !root.isDirectory()) return false;
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) continue;
    String path = entry.path();
    String config = path + "/config.eye";
    if (!FFat.exists(config)) continue;
    String id = path.substring(path.lastIndexOf('/') + 1);
    String name = id;
    bool capitalize = true;
    for (size_t i = 0; i < name.length(); ++i) {
      if (name[i] == '_' || name[i] == '-') { name.setCharAt(i, ' '); capitalize = true; }
      else if (capitalize) { name.setCharAt(i, toupper(name[i])); capitalize = false; }
    }
    _packages.push_back({id, name, config, true});
  }
  std::sort(_packages.begin(), _packages.end(), [](const Package &a, const Package &b) {
    return a.id.compareTo(b.id) < 0;
  });
  Preferences orderPrefs;
  orderPrefs.begin("monster-eyes", true);
  String savedOrder = orderPrefs.getString("order", "");
  orderPrefs.end();
  std::vector<Package> ordered;
  int start = 0;
  while (start < savedOrder.length()) {
    int end = savedOrder.indexOf(',', start); if (end < 0) end = savedOrder.length();
    String id = savedOrder.substring(start, end);
    for (auto it = _packages.begin(); it != _packages.end(); ++it) if (it->id == id) {
      ordered.push_back(*it); _packages.erase(it); break;
    }
    start = end + 1;
  }
  ordered.insert(ordered.end(), _packages.begin(), _packages.end());
  _packages.swap(ordered);
  String reconciled;
  for (const auto &p : _packages) { if (reconciled.length()) reconciled += ','; reconciled += p.id; }
  if (reconciled != savedOrder) { orderPrefs.begin("monster-eyes", false); orderPrefs.putString("order", reconciled); orderPrefs.end(); }
  Serial.printf("discovered %u eye packages\n", unsigned(_packages.size()));
  return !_packages.empty();
}

bool MonsterController::begin() {
  if (!refreshPackages()) return false;
  Preferences prefs;
  prefs.begin("monster-eyes", true);
  String saved = prefs.getString("current", "hazel");
  uint8_t initial = 0;
  for (uint8_t i = 0; i < _packages.size(); ++i) {
    _packages[i].enabled = prefs.getBool(enabledKey(_packages[i].id).c_str(), true);
    if (_packages[i].id == saved) initial = i;
  }
  prefs.end();
  if (!enabledStyleCount()) _packages[initial].enabled = true;
  if (!_packages[initial].enabled) {
    for (uint8_t i = 0; i < _packages.size(); ++i)
      if (_packages[i].enabled) { initial = i; break; }
  }
  return setStyle(initial);
}

bool MonsterController::setStyle(uint8_t style) {
  if (style >= _packages.size()) return false;
  if (_eyes) {
    _eyes->~Adafruit_Monster_Eyes();
    _eyes = nullptr;
  }
  _display.clear(TFT_BLACK);
  _eyes = new (_storage) Adafruit_Monster_Eyes(&_display);
  _eyes->setVerbose(Serial);
  _eyes->setStorageEnabled(true);
  _eyes->setDriveModeEnabled(false);
  _eyes->setConfigFile(_packages[style].config.c_str());
  _eyes->setSelfTest(false);
  if (!_eyes->begin()) {
    Serial.printf("style load failed: %s (%s)\n", _packages[style].name.c_str(),
                  _eyes->errorString() ? _eyes->errorString() : "unknown");
    return false;
  }
  _style = style;
  _display.clear(_eyes->config().eyelidColor);
  persistCurrent();
  Serial.printf("active style: %s, background: 0x%04X\n",
                _packages[style].name.c_str(), _eyes->config().eyelidColor);
  return true;
}

bool MonsterController::navigate(int direction) {
  if (_packages.empty()) return false;
  int candidate = _style;
  for (size_t attempts = 0; attempts < _packages.size(); ++attempts) {
    candidate = (candidate + direction + _packages.size()) % _packages.size();
    if (_packages[candidate].enabled) return setStyle(candidate);
  }
  return false;
}

bool MonsterController::nextStyle() { return navigate(1); }
bool MonsterController::previousStyle() { return navigate(-1); }

bool MonsterController::styleEnabled(uint8_t style) const {
  return style < _packages.size() && _packages[style].enabled;
}

uint8_t MonsterController::enabledStyleCount() const {
  uint8_t count = 0;
  for (const auto &package : _packages) if (package.enabled) ++count;
  return count;
}

bool MonsterController::setPackageOrder(const String &csv) {
  if (_packages.empty()) return false;
  std::vector<Package> ordered;
  int start=0;
  while (start<csv.length()) {
    int end=csv.indexOf(',',start); if(end<0) end=csv.length();
    String id=csv.substring(start,end); int idx=packageIndex(id);
    if(idx<0) return false;
    for(const auto&p:ordered) if(p.id==id) return false;
    ordered.push_back(_packages[idx]); start=end+1;
  }
  if(ordered.size()!=_packages.size()) return false;
  String current=_packages[_style].id; _packages.swap(ordered); _style=packageIndex(current);
  Preferences prefs; prefs.begin("monster-eyes",false); prefs.putString("order",csv); prefs.end();
  return true;
}

bool MonsterController::reloadPackages(const String &preferredId) {
  String id=preferredId.length()?preferredId:(_packages.empty()?String():_packages[_style].id);
  if(!refreshPackages()) return false;
  Preferences prefs; prefs.begin("monster-eyes",true);
  for(auto &p:_packages) p.enabled=prefs.getBool(enabledKey(p.id).c_str(),true);
  prefs.end();
  int idx=packageIndex(id); if(idx<0) idx=0;
  if(!enabledStyleCount()) _packages[idx].enabled=true;
  return setStyle(idx);
}

bool MonsterController::setStyleEnabled(uint8_t style, bool enabled) {
  if (style >= _packages.size()) return false;
  if (!enabled && _packages[style].enabled && enabledStyleCount() == 1) return false;
  _packages[style].enabled = enabled;
  Preferences prefs;
  prefs.begin("monster-eyes", false);
  prefs.putBool(enabledKey(_packages[style].id).c_str(), enabled);
  prefs.end();
  if (!enabled && style == _style) return nextStyle();
  return true;
}

void MonsterController::persistCurrent() {
  Preferences prefs;
  prefs.begin("monster-eyes", false);
  prefs.putString("current", _packages[_style].id);
  prefs.end();
}
