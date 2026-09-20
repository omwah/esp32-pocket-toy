/**
 * @file config_panel.cpp
 * @brief The config.eye document, and the ImGui panel that edits it.
 */

#include "config_panel.h"

#include <Adafruit_Monster_Eyes.h>
#include <ArduinoJson.h>
#include <imgui.h>

#include <stdio.h>
#include <string.h>

// ===========================================================================
//  DOCUMENT
// ===========================================================================

struct ConfigDocument::Impl {
  JsonDocument doc; ///< The parsed config.eye, edited in place
};

ConfigDocument::ConfigDocument() : _impl(new Impl), _dirty(false) {
  _impl->doc.to<JsonObject>();
}

ConfigDocument::~ConfigDocument() { delete _impl; }

bool ConfigDocument::load(const std::string &hostPath) {
  _dirty = false;
  _impl->doc.clear();
  _impl->doc.to<JsonObject>();

  FILE *f = fopen(hostPath.c_str(), "rb");
  if (!f)
    return false;
  std::string text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    text.append(buf, n);
  fclose(f);

  const DeserializationError err = deserializeJson(_impl->doc, text);
  if (err) {
    fprintf(stderr, "Could not parse %s: %s\n", hostPath.c_str(), err.c_str());
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

void ConfigDocument::setColor(const char *key, uint16_t value) {
  char hex[16];
  snprintf(hex, sizeof(hex), "0x%04X", value);
  _impl->doc[key] = hex;
  _dirty = true;
}

// ===========================================================================
//  PANEL
// ===========================================================================

namespace {

// Where the hovered control's explanation goes. A file-static rather than a
// parameter threaded through thirty call sites: the panel is immediate mode and
// single threaded, so there is exactly one frame in flight, and it is reset at
// the top of every frame.
//
// A tooltip would be the obvious thing, but ImGui draws those at the pointer
// without wrapping, so a sentence long enough to be useful runs off the edge of
// the screen. A fixed area at the foot of the panel can wrap, sits still long
// enough to read, and never leaves the window.
const char *gHoveredHelp = nullptr;

/** @brief Record @p tip if the control just submitted is hovered. */
void noteHelp(const char *tip) {
  if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    gHoveredHelp = tip;
}

/** @brief RGB565 to the 0..1 triple ImGui's colour picker wants. */
void rgb565ToFloat(uint16_t c, float *rgb) {
  const uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  rgb[0] = (float)((r << 3) | (r >> 2)) / 255.0f;
  rgb[1] = (float)((g << 2) | (g >> 4)) / 255.0f;
  rgb[2] = (float)((b << 3) | (b >> 3)) / 255.0f;
}

/** @brief The 0..1 triple back to RGB565. */
uint16_t floatToRgb565(const float *rgb) {
  auto clamp8 = [](float v) -> int {
    const int i = (int)(v * 255.0f + 0.5f);
    return i < 0 ? 0 : (i > 255 ? 255 : i);
  };
  return (uint16_t)(((clamp8(rgb[0]) & 0xF8) << 8) |
                    ((clamp8(rgb[1]) & 0xFC) << 3) | (clamp8(rgb[2]) >> 3));
}

/** @brief A slider bound to an integer key. @return true if it changed. */
bool intRow(ConfigDocument &doc, const char *label, const char *key,
            int inForce, int lo, int hi, const char *tip = nullptr) {
  int value = doc.getInt(key, inForce);
  const bool changed = ImGui::SliderInt(label, &value, lo, hi);
  if (changed)
    doc.setInt(key, value);
  noteHelp(tip);
  return changed;
}

/** @brief A slider bound to a float key. @return true if it changed. */
bool floatRow(ConfigDocument &doc, const char *label, const char *key,
              float inForce, float lo, float hi, const char *fmt = "%.3f",
              const char *tip = nullptr) {
  float value = doc.getFloat(key, inForce);
  const bool changed = ImGui::SliderFloat(label, &value, lo, hi, fmt);
  if (changed)
    doc.setFloat(key, value);
  noteHelp(tip);
  return changed;
}

/** @brief A checkbox bound to a boolean key. @return true if it changed. */
bool boolRow(ConfigDocument &doc, const char *label, const char *key,
             bool inForce, const char *tip = nullptr) {
  bool value = doc.getBool(key, inForce);
  const bool changed = ImGui::Checkbox(label, &value);
  if (changed)
    doc.setBool(key, value);
  noteHelp(tip);
  return changed;
}

/** @brief A colour picker bound to a colour key. @return true if it changed. */
bool colorRow(ConfigDocument &doc, const char *label, const char *key,
              uint16_t inForce, const char *tip = nullptr) {
  const uint16_t current = doc.getColor(key, inForce);
  float rgb[3];
  rgb565ToFloat(current, rgb);
  if (!ImGui::ColorEdit3(label, rgb,
                         ImGuiColorEditFlags_NoInputs |
                             ImGuiColorEditFlags_AlphaBar)) {
    noteHelp(tip);
    return false;
  }
  noteHelp(tip);
  const uint16_t packed = floatToRgb565(rgb);
  // The picker has 8 bits a channel and the panel has 5 or 6, so most nudges
  // land on the colour already stored. Writing anyway would mark the document
  // dirty and rebuild the renderer for no visible change.
  if (packed == current)
    return false;
  doc.setColor(key, packed);
  return true;
}

} // namespace

PanelResult drawConfigPanel(ConfigDocument &doc, const EyesSettings &defaults,
                            PanelState &state, const PanelLayout &layout,
                            const std::vector<std::string> &packages,
                            size_t current) {
  PanelResult result;
  const std::string &packageId =
      current < packages.size() ? packages[current] : std::string();

  ImGui::SetNextWindowPos(ImVec2(layout.panelX, 0));
  ImGui::SetNextWindowSize(ImVec2(layout.panelW, layout.panelH));
  ImGui::Begin("config.eye", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

  gHoveredHelp = nullptr;

  // The style list doubles as the header: it says which package is loaded and
  // switches to another in one click, which [ and ] only do one step at a time.
  ImGui::SetNextItemWidth(layout.panelW * 0.5f);
  if (ImGui::BeginCombo("##style", packageId.c_str())) {
    for (size_t i = 0; i < packages.size(); ++i) {
      const bool selected = (i == current);
      if (ImGui::Selectable(packages[i].c_str(), selected))
        result.selectPackage = (int)i;
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  noteHelp("Switch to another eye package. Unsaved edits are discarded.");
  ImGui::SameLine();
  ImGui::TextDisabled(doc.dirty() ? "(edited)" : "(unchanged)");
  ImGui::Separator();

  // The controls scroll; the save controls stay put at the foot. The help text
  // is not here at all -- it goes in a strip under the preview, where there is
  // room for it.
  const float footer = ImGui::GetTextLineHeightWithSpacing() * 6.0f;
  ImGui::BeginChild("controls", ImVec2(0, -footer), false);

  // Negative width means "leave this many pixels for the label", which is what
  // keeps the longer names from running off the edge of the panel.
  ImGui::PushItemWidth(-ImGui::GetFontSize() * 9.0f);

  // Used only where the file is silent. These are config-space defaults, not
  // what the running renderer settled on -- see the note in the header.
  const EyesSettings &now = defaults;
  bool changed = false;

  if (ImGui::CollapsingHeader("Geometry and pupil",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    changed |= intRow(doc, "eyeRadius", "eyeRadius", now.eyeRadius, 0, 250,
                      "Eyeball radius, 0 to derive it. In the config's own "
                      "pixel space, which begin() rescales to the display.");
    changed |= intRow(doc, "irisRadius", "irisRadius", now.irisRadius, 0, 250,
                      "Iris radius in the config's own pixel space; 0 derives "
                      "it.");
    changed |= intRow(doc, "slitPupilRadius", "slitPupilRadius",
                      now.slitPupilRadius, -1, 250,
                      "0 for a round pupil, -1 to derive it, or the slit "
                      "length in pixels.");
    changed |= boolRow(doc, "slitPupilHorizontal", "slitPupilHorizontal",
                       now.slitPupilHorizontal, "Lay the slit on its side.");
    changed |= boolRow(doc, "slitPupilRounded", "slitPupilRounded",
                       now.slitPupilRounded, "Round the ends of the slit.");
    changed |= intRow(doc, "displaySize", "displaySize", now.displaySize, 0, 240,
                      "Eye width and height in pixels; 0 fills the display.");
    changed |= floatRow(doc, "coverage", "coverage", now.coverageRequested,
                        0.0f, 1.5f, "%.3f",
                        "How much of the eyeball the display shows. "
                        "begin() may raise it to fit.");
    changed |= floatRow(doc, "pupilMin", "pupilMin", now.pupilMin, 0.0f, 1.0f,
                        "%.3f", "Smallest pupil as a fraction of the iris.");
    changed |= floatRow(doc, "pupilMax", "pupilMax", now.pupilMax, 0.0f, 1.0f,
                        "%.3f", "Largest pupil as a fraction of the iris.");
    // The file stores squint; the renderer holds 1 - squint as trackFactor.
    changed |= floatRow(doc, "squint", "squint", 1.0f - now.trackFactor, 0.0f,
                        1.0f, "%.3f", "How far the lid rests down.");
    changed |= intRow(doc, "fixate", "fixate", now.fixate, -60, 60,
                      "Convergence toward the face, in map pixels.");
    changed |= boolRow(doc, "tracking", "tracking", now.tracking,
                       "Let the upper lid follow the gaze.");
  }

  if (ImGui::CollapsingHeader("Colours", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Used where a texture is missing.");
    ImGui::PopTextWrapPos();
    changed |= colorRow(doc, "irisColor", "irisColor", now.irisColor,
                        "Flat iris colour, used when irisTexture is missing "
                        "or is a 1x1 bitmap.");
    changed |= colorRow(doc, "scleraColor", "scleraColor", now.scleraColor,
                        "Flat sclera colour, used when scleraTexture is "
                        "missing.");
    changed |= colorRow(doc, "pupilColor", "pupilColor", now.pupilColor,
                        "Fill colour of the pupil.");
    changed |= colorRow(doc, "backColor", "backColor", now.backColor,
                        "Shown outside the eyeball, where no eyelid covers "
                        "it.");
    changed |= colorRow(doc, "eyelidColor", "eyelidColor", now.eyelidColor,
                        "The eyelids, and the background the panel is "
                        "cleared to.");
  }

  if (ImGui::CollapsingHeader("Motion and iris flow")) {
    changed |= floatRow(doc, "irisSpin", "irisSpin", now.irisSpin, -30.0f,
                        30.0f, "%.2f rpm", "Positive is clockwise.");
    changed |= floatRow(doc, "scleraSpin", "scleraSpin", now.scleraSpin, -30.0f,
                        30.0f, "%.2f rpm", nullptr);
    // Stored as 1023 - angle, so the panel shows what the file would say.
    changed |= intRow(doc, "irisAngle", "irisAngle",
                      (1023 - now.irisStartAngle) & 1023, 0, 1023,
                      "Initial iris rotation, 0-1023 counter-clockwise.");
    changed |= intRow(doc, "scleraAngle", "scleraAngle",
                      (1023 - now.scleraStartAngle) & 1023, 0, 1023, nullptr);
    changed |= boolRow(doc, "irisMirror", "irisMirror", now.irisMirror != 0,
                       "Mirror the iris texture.");
    changed |= boolRow(doc, "scleraMirror", "scleraMirror",
                       now.scleraMirror != 0, nullptr);
    changed |= intRow(doc, "gazeMax", "gazeMax", (int)now.gazeMax, 100000,
                      10000000,
                      "Longest wait between major eye movements, in "
                      "microseconds.");
    changed |= floatRow(doc, "irisFlow", "irisFlow", now.irisFlow, 0.0f, 1.0f,
                        "%.3f", "Peak sample shift, as a fraction of iris "
                                "depth.");
    changed |= floatRow(doc, "irisFlowSpeed", "irisFlowSpeed",
                        now.irisFlowSpeed, -10.0f, 10.0f, "%.2f",
                        "Wave crests leaving the pupil per second.");
    changed |= floatRow(doc, "irisFlowWaves", "irisFlowWaves",
                        now.irisFlowWaves, 0.0f, 20.0f, "%.2f",
                        "Crests between the pupil and the rim.");
  }

  ImGui::PopItemWidth();
  ImGui::EndChild();
  ImGui::Separator();

  if (ImGui::Button("Revert to file"))
    result.revertRequested = true;
  noteHelp("Reload this package's config.eye, discarding every edit.");

  ImGui::PushTextWrapPos(0.0f); // Wrap the footer hints at the panel edge too
  ImGui::TextDisabled("Save as a new package");
  ImGui::SetNextItemWidth(layout.panelW * 0.55f);
  ImGui::InputTextWithHint("##saveas", "new-package-id", state.saveAsId,
                           sizeof(state.saveAsId));
  ImGui::SameLine();
  if (ImGui::Button("Save") && state.saveAsId[0]) {
    result.saveRequested = true;
    result.saveAsId = state.saveAsId;
  }
  ImGui::TextDisabled("Bitmaps are copied; the original is untouched.");
  ImGui::PopTextWrapPos();
  if (!state.message.empty()) {
    if (state.messageIsError)
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                         state.message.c_str());
    else
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "%s",
                         state.message.c_str());
  }

  ImGui::End();

  // The help strip. Borderless and unpadded at the sides so it reads as part of
  // the window furniture rather than as a second panel.
  ImGui::SetNextWindowPos(ImVec2(layout.helpX, layout.helpY));
  ImGui::SetNextWindowSize(ImVec2(layout.helpW, layout.helpH));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("##help", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PushTextWrapPos(0.0f);
  if (gHoveredHelp)
    ImGui::TextUnformatted(gHoveredHelp);
  else
    ImGui::TextDisabled("Hover a control for what it does.");
  ImGui::PopTextWrapPos();
  ImGui::End();
  ImGui::PopStyleVar();

  result.configChanged = changed;
  return result;
}
