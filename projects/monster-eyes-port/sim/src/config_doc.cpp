/**
 * @file config_doc.cpp
 * @brief Parsing, editing and serialising a config.eye.
 */

#include "config_doc.h"

#include <ArduinoJson.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ConfigDocument::Impl {
  JsonDocument doc; ///< The parsed config.eye, edited in place
};

ConfigDocument::ConfigDocument() : _impl(new Impl), _dirty(false) {
  _impl->doc.to<JsonObject>();
}

ConfigDocument::~ConfigDocument() { delete _impl; }

bool ConfigDocument::load(const std::string &hostPath) {
  FILE *f = fopen(hostPath.c_str(), "rb");
  if (!f) {
    _dirty = false;
    _impl->doc.clear();
    _impl->doc.to<JsonObject>();
    return false;
  }
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    text.append(buf, n);
  fclose(f);
  if (!loadText(text)) {
    fprintf(stderr, "Could not parse %s\n", hostPath.c_str());
    return false;
  }
  return true;
}

bool ConfigDocument::loadText(const std::string &json) {
  _dirty = false;
  _impl->doc.clear();
  _impl->doc.to<JsonObject>();
  const DeserializationError err = deserializeJson(_impl->doc, json);
  if (err) {
    _impl->doc.clear();
    _impl->doc.to<JsonObject>();
    return false;
  }
  return true;
}

std::string ConfigDocument::serialise(void) const {
  std::string out;
  serializeJsonPretty(_impl->doc, out);
  out += "\n";
  return out;
}

int ConfigDocument::getInt(const char *key, int inForce) const {
  JsonVariantConst v = _impl->doc[key];
  if (v.is<int>())
    return v.as<int>();
  if (v.is<float>())
    return (int)(v.as<float>() + 0.5f);
  return inForce;
}

void ConfigDocument::setInt(const char *key, int value) {
  _impl->doc[key] = value;
  _dirty = true;
}

float ConfigDocument::getFloat(const char *key, float inForce) const {
  JsonVariantConst v = _impl->doc[key];
  if (v.is<float>() || v.is<int>())
    return v.as<float>();
  return inForce;
}

void ConfigDocument::setFloat(const char *key, float value) {
  _impl->doc[key] = value;
  _dirty = true;
}

bool ConfigDocument::getBool(const char *key, bool inForce) const {
  JsonVariantConst v = _impl->doc[key];
  if (v.is<bool>())
    return v.as<bool>();
  if (v.is<int>())
    return v.as<int>() != 0;
  return inForce;
}

void ConfigDocument::setBool(const char *key, bool value) {
  _impl->doc[key] = value;
  _dirty = true;
}

bool ConfigDocument::getExtBool(const char *feature, const char *key,
                                bool inForce) const {
  JsonVariantConst v = _impl->doc["extensions"][feature][key];
  if (v.is<bool>())
    return v.as<bool>();
  if (v.is<int>())
    return v.as<int>() != 0;
  return inForce;
}

// Assigning through the subscripts creates the intermediate objects, so a
// config with no extensions block at all gains a well-formed one rather than
// silently dropping the setting.
void ConfigDocument::setExtBool(const char *feature, const char *key,
                                bool value) {
  _impl->doc["extensions"][feature][key] = value;
  _dirty = true;
}

float ConfigDocument::getExtFloat(const char *feature, const char *key,
                                  float inForce) const {
  JsonVariantConst v = _impl->doc["extensions"][feature][key];
  if (v.is<float>() || v.is<int>())
    return v.as<float>();
  return inForce;
}

void ConfigDocument::setExtFloat(const char *feature, const char *key,
                                 float value) {
  _impl->doc["extensions"][feature][key] = value;
  _dirty = true;
}

std::string ConfigDocument::getExtString(const char *feature, const char *key,
                                         const char *inForce) const {
  JsonVariantConst v = _impl->doc["extensions"][feature][key];
  if (v.is<const char *>())
    return v.as<const char *>();
  return inForce ? inForce : "";
}

void ConfigDocument::setExtString(const char *feature, const char *key,
                                  const char *value) {
  _impl->doc["extensions"][feature][key] = value;
  _dirty = true;
}

// Mirrors the library's own dwim() decoder, so the panel reads a colour the
// same way the renderer will. Kept in step by hand; the alternative is
// exporting dwim() from the library, which would mean changing it.
uint16_t ConfigDocument::getColor(const char *key, uint16_t inForce) const {
  JsonVariantConst v = _impl->doc[key];
  if (v.is<int>())
    return (uint16_t)v.as<int>();
  if (v.is<const char *>())
    return (uint16_t)strtol(v.as<const char *>(), nullptr, 0);
  if (v.is<JsonArrayConst>()) {
    JsonArrayConst a = v.as<JsonArrayConst>();
    if (a.size() >= 3) {
      long c[3];
      for (int i = 0; i < 3; i++) {
        if (a[i].is<int>())
          c[i] = a[i].as<int>();
        else if (a[i].is<float>())
          c[i] = (long)(a[i].as<float>() * 255.999f);
        else if (a[i].is<const char *>())
          c[i] = strtol(a[i].as<const char *>(), nullptr, 0);
        else
          c[i] = 0;
        c[i] = c[i] > 255 ? 255 : (c[i] < 0 ? 0 : c[i]);
      }
      return (uint16_t)(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) |
                        (c[2] >> 3));
    }
  }
  return inForce;
}

bool ConfigDocument::setPath(const std::string &dottedKey,
                             const std::string &text, std::string *error) {
  if (dottedKey.empty()) {
    if (error)
      *error = "empty key";
    return false;
  }

  // Walk the dots, creating objects on the way so a key can be set in a block
  // the file does not have yet.
  JsonVariant node = _impl->doc.as<JsonVariant>();
  size_t at = 0;
  for (;;) {
    const size_t dot = dottedKey.find('.', at);
    const std::string part = dottedKey.substr(
        at, dot == std::string::npos ? std::string::npos : dot - at);
    if (part.empty()) {
      if (error)
        *error = "empty key component";
      return false;
    }
    if (dot == std::string::npos) {
      // The leaf. Typed as config.eye would have spelled it.
      if (text == "true" || text == "false") {
        node[part] = (text == "true");
      } else {
        char *end = nullptr;
        const double number = strtod(text.c_str(), &end);
        const bool numeric = end && *end == '\0' && end != text.c_str();
        // A 0x colour stays a string: that is a form the renderer's decoder
        // understands, and turning it into an integer here would only lose
        // the way it was written.
        const bool hex = text.size() > 2 && text[0] == '0' &&
                         (text[1] == 'x' || text[1] == 'X');
        if (numeric && !hex) {
          if (text.find('.') == std::string::npos &&
              text.find('e') == std::string::npos &&
              text.find('E') == std::string::npos)
            node[part] = (long)number;
          else
            node[part] = (float)number;
        } else {
          node[part] = text;
        }
      }
      _dirty = true;
      return true;
    }
    // Created and re-fetched through the parent rather than through a copy:
    // a JsonVariant taken from node[part] is detached, so building the object
    // in it leaves the document untouched and the write silently disappears.
    if (!node[part].is<JsonObject>())
      node[part].to<JsonObject>();
    node = node[part];
    at = dot + 1;
  }
}

void ConfigDocument::setColor(const char *key, uint16_t value) {
  char hex[16];
  snprintf(hex, sizeof(hex), "0x%04X", value);
  _impl->doc[key] = hex;
  _dirty = true;
}

