/**
 * @file config_panel.cpp
 * @brief The config.eye document, and the ImGui panel that edits it.
 *
 * The device's web interface has an "Eye config" tab that is a hand-written
 * copy of this panel: the same sections in the same order, the same controls
 * in each, and the same help text. The two are meant to match, so an edit here
 * belongs in web/app.js (CONFIG_SECTIONS) as well.
 */

#include "config_panel.h"

#include <Adafruit_Monster_Eyes.h>
#include <imgui.h>

#include <stdio.h>
#include <string.h>

// ===========================================================================
//  DOCUMENT
// ===========================================================================

// ===========================================================================
//  PANEL
// ===========================================================================

namespace {

// The star on a control's name and this note are a footnote pair: the name
// carries the mark, the help says what it means. Between them they show which
// settings a stock Adafruit Monster Eyes package will not understand. The
// additions are the horizontal and rounded slit pupils (5d930ad), iris flow
// (f863dec) and the animation switches (d6079ff); everything else came over
// with the migration and behaves as upstream does.
#define EXTENSION_NOTE " *Extension, not in upstream Monster Eyes."

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

/** @brief One line under a section header saying what the section is for. */
void sectionNote(const char *text) {
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", text);
  ImGui::PopTextWrapPos();
}

/** @brief A checkbox bound to extensions.<feature>.<key>.
 *  @return true if it changed. */
bool extBoolRow(ConfigDocument &doc, const char *label, const char *feature,
                const char *key, bool inForce, const char *tip = nullptr) {
  bool value = doc.getExtBool(feature, key, inForce);
  const bool changed = ImGui::Checkbox(label, &value);
  if (changed)
    doc.setExtBool(feature, key, value);
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

  if (ImGui::CollapsingHeader("Geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
    sectionNote("Eyeball and iris sizes, in config pixels not screen.");
    changed |= intRow(doc, "eyeRadius", "eyeRadius", now.eyeRadius, 0, 250,
                      "Eyeball radius, 0 to derive it. In the config's own "
                      "pixel space, which begin() rescales to the display.");
    changed |= intRow(doc, "irisRadius", "irisRadius", now.irisRadius, 0, 250,
                      "Iris radius in the config's own pixel space; 0 derives "
                      "it.");
    changed |= intRow(doc, "displaySize", "displaySize", now.displaySize, 0, 240,
                      "Eye width and height in pixels; 0 fills the display.");
    changed |= floatRow(doc, "coverage", "coverage", now.coverageRequested,
                        0.0f, 1.5f, "%.3f",
                        "How much of the eyeball the display shows. "
                        "begin() may raise it to fit.");
    changed |= intRow(doc, "fixate", "fixate", now.fixate, -60, 60,
                      "Convergence toward the face, in map pixels.");
    changed |= boolRow(doc, "tracking", "tracking", now.tracking,
                       "Let the lids follow the gaze. Squint does nothing "
                       "without it.");
    // Squint is the resting offset of that tracking and is read nowhere else,
    // so it is inert while tracking is off rather than merely subtle.
    const bool tracking = doc.getBool("tracking", now.tracking);
    ImGui::BeginDisabled(!tracking);
    // The file stores squint; the renderer holds 1 - squint as trackFactor.
    changed |= floatRow(doc, "squint", "squint", 1.0f - now.trackFactor, 0.0f,
                        1.0f, "%.3f",
                        "Where the lids rest while they track the gaze; inert "
                        "with tracking off. Raising it lowers the upper lid "
                        "and drops the lower with it, scaled by irisRadius.");
    ImGui::EndDisabled();
  }

  if (ImGui::CollapsingHeader("Pupil", ImGuiTreeNodeFlags_DefaultOpen)) {
    sectionNote("Its shape, and how far it opens and closes.");
    changed |= intRow(doc, "slitPupilRadius", "slitPupilRadius",
                      now.slitPupilRadius, -1, 250,
                      "0 for a round pupil, -1 to derive it, or the slit "
                      "length in pixels.");
    changed |= floatRow(doc, "pupilMin", "pupilMin", now.pupilMin, 0.0f, 1.0f,
                        "%.3f",
                        "Smallest pupil as a fraction of the iris, or the "
                        "smallest the iris disc gets with irisDilation on.");
    changed |= floatRow(doc, "pupilMax", "pupilMax", now.pupilMax, 0.0f, 1.0f,
                        "%.3f",
                        "Largest pupil as a fraction of the iris, or the "
                        "largest the iris disc gets with irisDilation on.");
    changed |= boolRow(doc, "slitPupilHorizontal*", "slitPupilHorizontal",
                       now.slitPupilHorizontal,
                       "Lay the slit on its side." EXTENSION_NOTE);
    changed |= boolRow(doc, "slitPupilRounded*", "slitPupilRounded",
                       now.slitPupilRounded,
                       "Round the ends of the slit." EXTENSION_NOTE);
    changed |= boolRow(doc, "texturedPupil*", "texturedPupil",
                       now.texturedPupil,
                       "Fill the pupil from the iris texture's centre instead "
                       "of with a flat colour, for a drawn eye whose pattern "
                       "runs all the way in. Dilating still grows and shrinks "
                       "it." EXTENSION_NOTE);
    changed |= boolRow(doc, "irisDilation*", "irisDilation", now.irisDilation,
                       "Dilate by resizing the iris instead of opening a pupil "
                       "in it. The disc grows and shrinks whole, pattern and "
                       "edge intact, with the sclera showing behind it. "
                       "pupilMin and pupilMax then read as the smallest and "
                       "largest the disc gets." EXTENSION_NOTE);
  }

  if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
    sectionNote("How many eyes the panel shows.");
    const bool wasSingle = doc.getExtBool("display", "singleEye", false);
    bool single = wasSingle;
    if (ImGui::Checkbox("singleEye*", &single)) {
      doc.setExtBool("display", "singleEye", single);
      changed = true;
    }
    noteHelp("One eye filling the panel, 240px centred, instead of two 128px "
             "eyes side by side. The eye is rebuilt, so its textures reload "
             "at the new size." EXTENSION_NOTE);

    // Which side only means anything with one eye, and it picks which of the
    // config's left and right blocks applies.
    ImGui::BeginDisabled(!single);
    const std::string side = doc.getExtString("display", "side", "left");
    const bool isRight = !side.empty() && (side[0] == 'r' || side[0] == 'R');
    int sideIndex = isRight ? 1 : 0;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    if (ImGui::Combo("side*", &sideIndex, "left\0right\0")) {
      doc.setExtString("display", "side", sideIndex ? "right" : "left");
      changed = true;
    }
    ImGui::EndDisabled();
    noteHelp("Which eye the single one is, and so which of the config's left "
             "and right blocks applies to it." EXTENSION_NOTE);

    // Only means anything with two eyes, since one eye is centred.
    ImGui::BeginDisabled(single);
    {
      int gap = doc.getExtInt("display", "eyeGap", 28);
      ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
      if (ImGui::SliderInt("eyeGap*", &gap, -64, 96, "%d px")) {
        doc.setExtInt("display", "eyeGap", gap);
        changed = true;
      }
    }
    ImGui::EndDisabled();
    noteHelp("Pixels between the two eye squares; each eye moves half the "
             "difference, so the pair stays centred. The panel's own layout "
             "leaves 28. Negative overlaps them, which suits a face whose "
             "eyes nearly touch -- but past the point where one square "
             "reaches the other eye's artwork it writes its background over "
             "it." EXTENSION_NOTE);
  }

  if (ImGui::CollapsingHeader("Colours", ImGuiTreeNodeFlags_DefaultOpen)) {
    sectionNote("Used where a texture is missing.");
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

  if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
    // These live under extensions rather than at the root, alongside the
    // audio block the sketch reads, because they are behaviour rather than
    // the renderer's geometry. Both default to on.
    sectionNote("What the eye does when nothing is steering it.");
    changed |= intRow(doc, "gazeMax", "gazeMax", (int)now.gazeMax, 100000,
                      10000000,
                      "Longest wait between major eye movements, in "
                      "microseconds. Only matters with autoGaze on.");
    changed |= extBoolRow(doc, "autoGaze*", "animation", "autoGaze", true,
                          "Let the eye look around on its own. Off holds the "
                          "gaze still, for a package that should "
                          "stare." EXTENSION_NOTE);
    changed |= extBoolRow(doc, "autoBlink*", "animation", "autoBlink", true,
                          "Let the eye blink on its own. Off means it never "
                          "blinks." EXTENSION_NOTE);
    // Behaviour rather than geometry: it follows the gaze, so it sits with
    // the animators and not with the fixed roll in Rotation.
    {
      float value = doc.getExtFloat("animation", "cyclovergence",
                                    now.cyclovergence);
      if (ImGui::SliderFloat("cyclovergence*", &value, 0.0f, 90.0f,
                             "%.1f deg")) {
        doc.setExtFloat("animation", "cyclovergence", value);
        changed = true;
      }
      noteHelp("Roll the eyes as the gaze goes down, the way a grazing animal "
               "keeps its slit level with the horizon while its head is "
               "lowered. This is the angle at full downward gaze; looking "
               "level or up leaves the eyes level." EXTENSION_NOTE);
    }
    {
      float range = doc.getExtFloat("animation", "gazeRange", now.gazeRange);
      if (ImGui::SliderFloat("gazeRange*", &range, 0.0f, 1.0f, "%.2f")) {
        doc.setExtFloat("animation", "gazeRange", range);
        changed = true;
      }
      noteHelp("How far the eye may look, as a fraction of what the geometry "
               "allows. 1 is everything; less keeps a drawn eye's iris inside "
               "its own white and its artwork inside the box it is drawn "
               "in." EXTENSION_NOTE);
    }
  }

  if (ImGui::CollapsingHeader("Rotation")) {
    sectionNote("Rolls the whole eyeball, and spins its textures in place.");
    changed |= floatRow(doc, "irisSpin", "irisSpin", now.irisSpin, -30.0f,
                        30.0f, "%.2f rpm",
                        "Turn the iris texture continuously. Positive is "
                        "clockwise, 0 holds it still.");
    changed |= floatRow(doc, "scleraSpin", "scleraSpin", now.scleraSpin, -30.0f,
                        30.0f, "%.2f rpm",
                        "Turn the sclera texture continuously.");
    // Stored as 1023 - angle, so the panel shows what the file would say.
    changed |= intRow(doc, "irisAngle", "irisAngle",
                      (1023 - now.irisStartAngle) & 1023, 0, 1023,
                      "Where the iris texture starts, 0-1023 "
                      "counter-clockwise.");
    changed |= intRow(doc, "scleraAngle", "scleraAngle",
                      (1023 - now.scleraStartAngle) & 1023, 0, 1023,
                      "Where the sclera texture starts, 0-1023 "
                      "counter-clockwise.");
    changed |= boolRow(doc, "irisMirror", "irisMirror", now.irisMirror != 0,
                       "Mirror the iris texture, reversing which way its "
                       "detail runs.");
    changed |= boolRow(doc, "scleraMirror", "scleraMirror",
                       now.scleraMirror != 0, "Mirror the sclera texture.");
    changed |= floatRow(doc, "roll*", "roll", now.roll, -90.0f, 90.0f,
                        "%.1f deg",
                        "Roll the eyeball about its own optic axis. The two "
                        "eyes take opposite angles, as a grazing animal's do "
                        "when its head goes down and it keeps the slit level "
                        "with the horizon. Pupil, iris and sclera turn "
                        "together; the lids do not." EXTENSION_NOTE);
  }

  if (ImGui::CollapsingHeader("Iris flow")) {
    sectionNote("Iris creeps along a moving wave, without turning.");
    changed |= floatRow(doc, "irisFlow*", "irisFlow", now.irisFlow, 0.0f, 1.0f,
                        "%.3f",
                        "How far the sampling shifts at the peak, as a "
                        "fraction of iris depth. 0 switches the effect "
                        "off." EXTENSION_NOTE);
    changed |= floatRow(doc, "irisFlowSpeed*", "irisFlowSpeed",
                        now.irisFlowSpeed, -10.0f, 10.0f, "%.2f",
                        "Wave crests leaving the pupil per second. Negative "
                        "draws them inward." EXTENSION_NOTE);
    changed |= floatRow(doc, "irisFlowWaves*", "irisFlowWaves",
                        now.irisFlowWaves, 0.0f, 20.0f, "%.2f",
                        "How many crests sit between the pupil and the "
                        "rim." EXTENSION_NOTE);
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
