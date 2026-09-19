#include "monster_controller.h"
#include <FFat.h>
#include <algorithm>
#include <new>

MonsterController::~MonsterController() {
  if (_eyes) _eyes->~Adafruit_Monster_Eyes();
}

const char *MonsterController::styleName(uint8_t style) const {
  return style < _packages.size() ? _packages[style].name.c_str() : "Unknown";
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
    _packages.push_back({id, name, config});
  }
  std::sort(_packages.begin(), _packages.end(), [](const Package &a, const Package &b) {
    return a.id.compareTo(b.id) < 0;
  });
  Serial.printf("discovered %u eye packages\n", unsigned(_packages.size()));
  return !_packages.empty();
}

bool MonsterController::begin() {
  if (!refreshPackages()) return false;
  uint8_t initial = 0;
  for (uint8_t i = 0; i < _packages.size(); ++i)
    if (_packages[i].id == "hazel") { initial = i; break; }
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
  Serial.printf("active style: %s\n", _packages[style].name.c_str());
  return true;
}

bool MonsterController::nextStyle() {
  return !_packages.empty() && setStyle((_style + 1) % _packages.size());
}
bool MonsterController::previousStyle() {
  return !_packages.empty() && setStyle((_style + _packages.size() - 1) % _packages.size());
}
