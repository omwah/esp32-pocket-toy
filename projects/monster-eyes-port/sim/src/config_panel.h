/**
 * @file config_panel.h
 * @brief The live config.eye editor, and the document it edits.
 *
 * The document IS the parsed config.eye, kept as JSON rather than unpacked into
 * a struct of fields. Two reasons. Keys the renderer never reads — the
 * extensions block holding the device's audio, anything a future firmware adds
 * — survive an edit untouched, because nothing ever drops them. And a save is
 * the same serialisation as an apply, so what you were just looking at is
 * exactly what lands on disk.
 *
 * A value the file does not mention is shown as the library's own default,
 * taken from a renderer that has been constructed but not begun. That
 * distinction matters: begin() rewrites geometry from the config's coordinate
 * space into screen space, so a config saying eyeRadius 125 in a 240px space
 * reads back as 53 once it has been fitted to a 128px eye. Showing the fitted
 * number would be wrong twice over -- it is not what the file says, and
 * touching the control would write it back, permanently baking one display's
 * scaling into the package. The panel therefore edits and shows config space
 * throughout, exactly what the file means.
 */

#ifndef _SIM_CONFIG_PANEL_H_
#define _SIM_CONFIG_PANEL_H_

#include <stdint.h>
#include <string>
#include <vector>

struct EyesSettings;

/**
 * @brief A config.eye being edited.
 *
 * Wraps an ArduinoJson document; the ArduinoJson types stay out of this header
 * so that only one translation unit pays for them.
 */
class ConfigDocument {
public:
  ConfigDocument();
  ~ConfigDocument();

  ConfigDocument(const ConfigDocument &) = delete;
  ConfigDocument &operator=(const ConfigDocument &) = delete;

  /**
   * @brief Parse a config.eye.
   * @param hostPath Path on the host filesystem, not a device path.
   * @return true if it parsed; an unreadable file leaves an empty document,
   *         which is still editable.
   */
  bool load(const std::string &hostPath);

  /** @brief Serialise to JSON text. @return The file content to write. */
  std::string serialise(void) const;

  /** @brief Has anything been changed since load()? @return true if edited. */
  bool dirty(void) const { return _dirty; }

  /** @brief Forget that anything was changed, after a save. */
  void clearDirty(void) { _dirty = false; }

  // Typed access. Each getter takes the value in force, used when the file does
  // not mention the key; each setter marks the document dirty.

  /** @brief Read an integer key. @param key Name. @param inForce Fallback.
   *  @return Value. */
  int getInt(const char *key, int inForce) const;
  /** @brief Write an integer key. @param key Name. @param value Value. */
  void setInt(const char *key, int value);
  /** @brief Read a float key. @param key Name. @param inForce Fallback.
   *  @return Value. */
  float getFloat(const char *key, float inForce) const;
  /** @brief Write a float key. @param key Name. @param value Value. */
  void setFloat(const char *key, float value);
  /** @brief Read a boolean key. @param key Name. @param inForce Fallback.
   *  @return Value. */
  bool getBool(const char *key, bool inForce) const;
  /** @brief Write a boolean key. @param key Name. @param value Value. */
  void setBool(const char *key, bool value);
  /**
   * @brief Read a colour key as native-endian RGB565.
   *
   * Accepts everything config.eye may hold: a number, a "0xF800" string, or an
   * [r,g,b] array of bytes or floats.
   *
   * @param key     Name.
   * @param inForce Fallback.
   * @return RGB565.
   */
  uint16_t getColor(const char *key, uint16_t inForce) const;
  /**
   * @brief Write a colour key as a hex string.
   *
   * "0xF800" rather than [r,g,b], because it round-trips exactly: an array goes
   * through an 8-bit-per-channel form that cannot represent every RGB565 value.
   *
   * @param key   Name.
   * @param value RGB565.
   */
  void setColor(const char *key, uint16_t value);

private:
  struct Impl;
  Impl *_impl;  ///< Holds the JsonDocument
  bool _dirty;  ///< Something has been edited
};

/** @brief What the panel is asking the host to do after a frame. */
struct PanelResult {
  bool configChanged = false; ///< Rebuild the renderer from the document
  bool saveRequested = false; ///< Write the document as a new package
  bool revertRequested = false; ///< Reload the original config.eye
  std::string saveAsId;       ///< Package id to save as, when saveRequested
  int selectPackage = -1;     ///< Package chosen from the list, or -1
};

/**
 * @brief Where the panel and its help strip go, in window pixels.
 *
 * The help strip runs along the bottom of the preview rather than sitting
 * inside the panel: the panel is the scarce space, and a strip as wide as the
 * eye holds a sentence in one or two lines instead of five.
 */
struct PanelLayout {
  float panelX = 0;  ///< Left edge of the editor
  float panelW = 0;  ///< Editor width
  float panelH = 0;  ///< Editor height
  float helpX = 0;   ///< Left edge of the help strip
  float helpY = 0;   ///< Top of the help strip
  float helpW = 0;   ///< Help strip width
  float helpH = 0;   ///< Help strip height
};

/** @brief Panel state that has to live between frames. */
struct PanelState {
  char saveAsId[64] = "";  ///< Text being typed into the Save As field
  std::string message;     ///< Result of the last save, shown under the button
  bool messageIsError = false; ///< Colour the message red
};

/**
 * @brief Draw the editor for one frame.
 *
 * Emits ImGui commands only; the caller owns the frame and the rebuild.
 *
 * @param doc      Document to edit.
 * @param defaults Library defaults, from a renderer that has NOT been begun;
 *                 used for keys the file leaves out.
 * @param state    Persistent panel state.
 * @param layout   Where to put the editor and its help strip.
 * @param packages Every package id, for the style chooser.
 * @param current  Index into @p packages of the one being edited.
 * @return What the host should do next.
 */
PanelResult drawConfigPanel(ConfigDocument &doc, const EyesSettings &defaults,
                            PanelState &state, const PanelLayout &layout,
                            const std::vector<std::string> &packages,
                            size_t current);

#endif // _SIM_CONFIG_PANEL_H_
