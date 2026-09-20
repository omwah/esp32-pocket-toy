/**
 * @file config_doc.h
 * @brief A config.eye held in memory, parsed and edited.
 *
 * Separate from the panel because it is not a user interface: the command line
 * edits configs too, through --set, and that has to work in a build with no
 * SDL and therefore no panel at all.
 *
 * The document IS the parsed config.eye, kept as JSON rather than unpacked
 * into a struct of fields. Two reasons. Keys the renderer never reads -- the
 * extensions block holding the device's audio, anything a future firmware adds
 * -- survive an edit untouched, because nothing ever drops them. And a save is
 * the same serialisation as an apply, so what was being looked at is exactly
 * what lands on disk.
 */

#ifndef _SIM_CONFIG_DOC_H_
#define _SIM_CONFIG_DOC_H_

#include <stdint.h>
#include <string>

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

  /**
   * @brief Parse config JSON already in hand.
   *
   * Used to reopen a package whose config was edited earlier in the session,
   * where the edit is held in memory rather than on disk.
   *
   * @param json Document text.
   * @return true if it parsed.
   */
  bool loadText(const std::string &json);

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
   * @brief Read a boolean out of extensions.<feature>.
   * @param feature Extension block name.
   * @param key     Key within it.
   * @param inForce Fallback when the file does not say.
   * @return Value.
   */
  bool getExtBool(const char *feature, const char *key, bool inForce) const;
  /**
   * @brief Write a boolean into extensions.<feature>, creating both if needed.
   * @param feature Extension block name.
   * @param key     Key within it.
   * @param value   Value.
   */
  void setExtBool(const char *feature, const char *key, bool value);
  /**
   * @brief Read a string out of extensions.<feature>.
   * @param feature Extension block name.
   * @param key     Key within it.
   * @param inForce Fallback when the file does not say.
   * @return Value.
   */
  std::string getExtString(const char *feature, const char *key,
                           const char *inForce) const;
  /**
   * @brief Write a string into extensions.<feature>, creating both if needed.
   * @param feature Extension block name.
   * @param key     Key within it.
   * @param value   Value.
   */
  void setExtString(const char *feature, const char *key, const char *value);
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

  /**
   * @brief Set any key from text, as --set gives it.
   *
   * The key may be dotted to reach into a block:
   * extensions.display.singleEye. Intermediate objects are created, so a
   * config with no extensions block gains a well-formed one.
   *
   * The value is typed the way config.eye would spell it: true and false
   * become booleans, 0x-prefixed and quoted text stay strings so the
   * renderer's colour decoder sees them as it would in a file, anything that
   * parses as a number becomes one, and the rest is a string.
   *
   * @param dottedKey Key, optionally dotted.
   * @param text      Value as written on the command line.
   * @param error     Receives a message on failure.
   * @return true if it was set.
   */
  bool setPath(const std::string &dottedKey, const std::string &text,
               std::string *error);

private:
  struct Impl;
  Impl *_impl;  ///< Holds the JsonDocument
  bool _dirty;  ///< Something has been edited
};

#endif // _SIM_CONFIG_DOC_H_
