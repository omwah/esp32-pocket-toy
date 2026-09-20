/**
 * @file main.cpp
 * @brief The Linux preview for the monster eyes renderer.
 *
 * Two modes over one code path:
 *
 *   windowed  An SDL3 window, Wayland where the session offers it, showing the
 *             simulated 320x240 panel at an integer scale. Keys drive the gaze,
 *             the blink and the eye package, and 'r' rereads config.eye from
 *             disk so a texture or a colour can be tried without a rebuild.
 *
 *   headless  No window at all. Renders a fixed number of frames on the virtual
 *             clock and writes each one as a PNG with a JSON sidecar, so an
 *             agent can look at a sequence and reason about what changed.
 *
 * The renderer, the asset loader and the config parser are the library's own,
 * compiled from lib/Adafruit_Monster_Eyes unmodified. What this file adds is a
 * display backend that writes to memory and a host for it to run in.
 */

#include "capture.h"
#include "linux_display.h"

#include <Adafruit_Monster_Eyes.h>
#include <FFat.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#ifndef SIM_HEADLESS_ONLY
#include "config_panel.h"
#include "image_doc.h"
#include "image_editor.h"
#include "package_io.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#endif

namespace {

/** @brief One eye package found on the asset filesystem. */
struct Package {
  std::string id;     ///< Directory name, e.g. "deer"
  std::string config; ///< Device path to its config.eye
};

/** @brief Everything the command line can set. */
struct Options {
  std::string assetRoot = "data"; ///< Directory standing in for flash
  std::string eyeId;              ///< Package to start on; empty means first
  std::string outPrefix;          ///< Where captures go, without extension
  int scale = 3;                  ///< Window pixels per panel pixel
  int frames = 0;                 ///< Headless frame count; 0 means windowed
  int fps = 30;                   ///< Frame rate: virtual clock step, and the
                                  ///< cap on the window loop. 0 uncaps the
                                  ///< window.
  int skip = 0;                   ///< Frames to render before capturing
  uint32_t seed = 1;              ///< Pseudo-random seed
  bool headless = false;          ///< Render without a window
  bool quiet = false;             ///< Silence the library's narration
  // Tracked separately from the values so that a config.eye saying
  // extensions.animation.autoGaze is not silently overridden by a default the
  // user never asked for. Only an explicit flag wins over the file.
  bool autoGazeSet = false;       ///< --no-auto-gaze was given
  bool autoBlinkSet = false;      ///< --no-auto-blink was given
  bool autoGaze = true;           ///< Let the gaze animator run
  bool gazeFixed = false;         ///< Hold the gaze at gazeFixedX/Y
  float gazeFixedX = 0.0f;        ///< Held gaze, -1..1
  float gazeFixedY = 0.0f;        ///< Held gaze, -1..1
  bool autoBlink = true;          ///< Let the blink animator run
  bool panel = false;             ///< Open the config editor at startup
  bool images = false;            ///< Open the bitmap editor at startup
  int panelWidth = 380;           ///< Editor width in window pixels
};

// ---------------------------------------------------------------------------
//  PACKAGES
// ---------------------------------------------------------------------------

// The device enumerates /eyes off FFat and skips dot-directories, which are
// upload staging and publish backups rather than styles. Same rule here, so the
// simulator's list matches the one the device would show.
std::vector<Package> findPackages(const std::string &assetRoot) {
  std::vector<Package> found;
  const std::string dirPath = assetRoot + "/eyes";
  DIR *dir = opendir(dirPath.c_str());
  if (!dir) {
    fprintf(stderr, "No eye packages under %s\n", dirPath.c_str());
    return found;
  }
  while (const struct dirent *entry = readdir(dir)) {
    if (entry->d_name[0] == '.')
      continue;
    const std::string id = entry->d_name;
    const std::string configHost = dirPath + "/" + id + "/config.eye";
    struct stat st;
    if (stat(configHost.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
      continue;
    found.push_back({id, "/eyes/" + id + "/config.eye"});
  }
  closedir(dir);
  std::sort(found.begin(), found.end(),
            [](const Package &a, const Package &b) { return a.id < b.id; });
  return found;
}

void setScreenGaze(Adafruit_Monster_Eyes &eyes, float x, float y);

/**
 * @brief Owns the eyes instance and rebuilds it when the package changes.
 *
 * Switching styles means tearing the renderer down and building a new one, as
 * the device's MonsterController does: the polar maps and textures are sized
 * from the config, so there is no meaningful way to change one in place.
 */
class EyeHost {
public:
  /** @param display Backend the renderer draws into. */
  explicit EyeHost(LinuxDisplay &display) : _display(display) {}
  ~EyeHost() { destroy(); }

  /**
   * @brief Load a package by index.
   * @param packages Known packages.
   * @param index    Which one.
   * @param verbose  Narrate startup to stderr.
   * @param autoGaze Let the gaze animator run.
   * @param autoBlink Let the blink animator run.
   * @return true if the renderer came up.
   */
  bool load(const std::vector<Package> &packages, size_t index, bool verbose,
            bool autoGaze, bool autoBlink, bool gazeSet = true,
            bool blinkSet = true) {
    if (index >= packages.size())
      return false;
    destroy();
    _index = index;
    _display.clear(0);

    _eyes = new Adafruit_Monster_Eyes(&_display);
    if (verbose)
      _eyes->setVerbose(Serial);
    _eyes->setStorageEnabled(true);
    _eyes->setDriveModeEnabled(false);
    _eyes->setSelfTest(false);
    _eyes->setConfigFile(packages[index].config.c_str());
    if (!_eyes->begin()) {
      const char *why = _eyes->errorString();
      fprintf(stderr, "Could not load %s: %s\n", packages[index].id.c_str(),
              why ? why : "unknown");
      destroy();
      return false;
    }
    // The device fills the panel with the eyelid colour so the eye sits on the
    // same background the config asked for rather than on black.
    _display.clear(_eyes->config().eyelidColor);
    // begin() has already applied extensions.animation from the config, so
    // these only override it where the command line actually said so.
    if (gazeSet)
      _eyes->setAutoGaze(autoGaze);
    if (blinkSet)
      _eyes->setAutoBlink(autoBlink);
    if (_fixedGaze)
      setScreenGaze(*_eyes, _fixedGazeX, _fixedGazeY);
    return true;
  }

  /**
   * @brief Hold the gaze at a fixed point across reloads.
   * @param x Gaze X, -1..1.
   * @param y Gaze Y, -1..1.
   */
  void holdGaze(float x, float y) {
    _fixedGaze = true;
    _fixedGazeX = x;
    _fixedGazeY = y;
  }

  /**
   * @brief Rebuild, putting the eye back the way it was looking.
   *
   * Applying an edit means a rebuild, because the polar maps and the texture
   * budget are sized from the config. A bare rebuild would re-centre the gaze
   * and restart the blink, which is very visible while dragging a slider, so
   * the animator's position is read out first and put back afterwards.
   *
   * @param packages  Known packages.
   * @param index     Package to load.
   * @param verbose   Narrate startup.
   * @param autoGaze  Let the gaze animator run.
   * @param autoBlink Let the blink animator run.
   * @param gazeOwned Something else is steering the gaze, so keep holding it.
   * @param autoGaze  Gaze animator setting from the command line.
   * @param autoBlink Blink animator setting from the command line.
   * @param gazeSet   The command line actually asked about the gaze.
   * @param blinkSet  The command line actually asked about the blink.
   * @return true if the renderer came up.
   */
  bool reload(const std::vector<Package> &packages, size_t index, bool verbose,
              bool gazeOwned, bool autoGaze, bool autoBlink, bool gazeSet,
              bool blinkSet) {
    float gazeX = 0.0f, gazeY = 0.0f, blink = 0.0f, iris = 0.5f;
    const bool had = _eyes != nullptr;
    if (had) {
      gazeX = _eyes->gazeX();
      gazeY = _eyes->gazeY();
      blink = _eyes->blinkPhase();
      iris = _eyes->irisFraction();
    }
    // Where the eye is LOOKING is carried across, so a slider drag does not
    // re-centre it. Whether the animators are RUNNING is not: the document
    // being applied may have just changed extensions.animation, and it is the
    // source of truth for that. The cost is that a g or b keypress does not
    // outlive the next edit, which is the right way round -- the config wins.
    if (!load(packages, index, verbose, autoGaze, autoBlink, gazeSet,
              blinkSet))
      return false;
    if (had) {
      _eyes->setGaze(gazeX, gazeY);
      if (!gazeOwned)
        _eyes->releaseGaze();
      // Set then release: the phase is restored, but the lids go back to the
      // animator rather than being frozen where they were.
      _eyes->setBlink(blink);
      _eyes->releaseBlink();
      _eyes->setIrisFraction(iris);
    }
    return true;
  }

  /** @brief The live renderer, or NULL. @return Instance. */
  Adafruit_Monster_Eyes *eyes(void) { return _eyes; }
  /** @brief Package currently loaded. @return Index. */
  size_t index(void) const { return _index; }

private:
  void destroy(void) {
    delete _eyes;
    _eyes = nullptr;
  }

  LinuxDisplay &_display;              ///< Backend passed to the renderer
  Adafruit_Monster_Eyes *_eyes = nullptr; ///< Live renderer
  size_t _index = 0;                   ///< Package index
  bool _fixedGaze = false;             ///< Reapply a held gaze on load
  float _fixedGazeX = 0.0f;            ///< Held gaze X
  float _fixedGazeY = 0.0f;            ///< Held gaze Y
};

// ---------------------------------------------------------------------------
//  COMMAND LINE
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  GAZE DIRECTION
// ---------------------------------------------------------------------------
//
// The simulator talks in SCREEN space: +X is right and +Y is up as the viewer
// sees it, so the pupil follows the mouse and the arrow keys point where they
// are drawn. Adafruit_Monster_Eyes::setGaze() is the other way round on both
// axes, and these two functions are the only place that is dealt with.
//
// The library's _frameEyeX/_frameEyeY are the point the renderer SAMPLES from
// in the polar map, not the point the pupil is drawn at. Raising them slides
// the sampling window one way, which slides the rendered pupil the other.
// Measured on the cat package against a centred pupil at x=87.8, y=121.3:
//
//     setGaze(+1, 0)  ->  pupil x 75.9   (left)
//     setGaze(-1, 0)  ->  pupil x 98.1   (right)
//     setGaze(0, +1)  ->  pupil y 144.6  (down)
//     setGaze(0, -1)  ->  pupil y 94.9   (up)
//
// The doc comment on setGaze() in Adafruit_Monster_Eyes.h claims the opposite
// ("-1.0 hard left to 1.0 hard right", "-1.0 down to 1.0 up"). Nothing in the
// firmware calls setGaze() -- MonsterController::setGaze() has no callers -- so
// the mismatch had never been exercised. Left alone rather than corrected in
// the library, which stays unmodified.

/**
 * @brief Point the gaze in screen space.
 * @param eyes Renderer to steer.
 * @param x    -1 hard left to 1 hard right, as the viewer sees it.
 * @param y    -1 hard down to 1 hard up, as the viewer sees it.
 */
void setScreenGaze(Adafruit_Monster_Eyes &eyes, float x, float y) {
  eyes.setGaze(-x, -y);
}

/** @brief Where the eye is looking, in screen space.
 *  @param eyes Renderer to read. @return X, -1 left to 1 right. */
float screenGazeX(const Adafruit_Monster_Eyes &eyes) { return -eyes.gazeX(); }

/** @brief Where the eye is looking, in screen space.
 *  @param eyes Renderer to read. @return Y, -1 down to 1 up. */
float screenGazeY(const Adafruit_Monster_Eyes &eyes) { return -eyes.gazeY(); }

#ifndef SIM_HEADLESS_ONLY
/**
 * @brief Which window an event was aimed at, or 0 for none.
 *
 * With two windows there are two ImGui contexts, and each must be shown only
 * its own input or they fight over the mouse. SDL puts the window id in a
 * different member per event type, so this picks the right one.
 */
Uint32 eventWindowId(const SDL_Event &e) {
  switch (e.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    return e.key.windowID;
  case SDL_EVENT_TEXT_INPUT:
    return e.text.windowID;
  case SDL_EVENT_TEXT_EDITING:
    return e.edit.windowID;
  case SDL_EVENT_MOUSE_MOTION:
    return e.motion.windowID;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    return e.button.windowID;
  case SDL_EVENT_MOUSE_WHEEL:
    return e.wheel.windowID;
  default:
    // Everything else worth routing is a window event, which shares a layout.
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST)
      return e.window.windowID;
    return 0;
  }
}
#endif

/** @brief One row of the key list. */
struct KeyHelp {
  const char *keys; ///< How the key is written
  const char *what; ///< What it does
};

// The overlay and --help both read this, so a key cannot be added to one and
// forgotten in the other.
const KeyHelp kKeyHelp[] = {
    {"arrows", "steer the gaze"},
    {"m", "toggle mouse-driven gaze"},
    {"space", "blink"},
    {"g / b", "toggle the gaze / blink animators"},
    {"[ / ]", "previous / next eye package"},
    {"r", "reload config.eye from disk"},
    {"c", "capture frames to --out"},
    {"tab", "toggle the status overlay"},
    {"p", "toggle the config.eye editor"},
    {"i", "toggle the image editor"},
    {"?", "this list"},
    {"q, escape", "quit"},
};

void usage(const char *argv0) {
  printf(
      "Usage: %s [options]\n"
      "\n"
      "A Linux preview of the monster eyes renderer, running the same C++ the\n"
      "firmware runs against a simulated 320x240 panel.\n"
      "\n"
      "  --assets DIR     Directory standing in for the device filesystem,\n"
      "                   containing eyes/<id>/config.eye (default: data)\n"
      "  --eye ID         Package to start on (default: the first found)\n"
      "  --list           Print the packages found and exit\n"
      "  --scale N        Window pixels per panel pixel (default: 3)\n"
      "  --panel          Open the config.eye editor beside the display\n"
      "  --images         Open the bitmap editor, in a window of its own\n"
      "  --panel-width N  Editor width in pixels (default: 380)\n"
      "  --fps N          Frame rate, for the virtual clock and as the cap on\n"
      "                   the window loop (default: 30; 0 uncaps the window)\n"
      "  --seed N         Pseudo-random seed (default: 1)\n"
      "\n"
      "Capture:\n"
      "  --frames N       Render N frames headless and exit\n"
      "  --skip N         Render N frames before capturing, to let the eye\n"
      "                   settle or to reach a particular moment\n"
      "  --out PREFIX     Write PREFIX-000.png and PREFIX-000.json per frame,\n"
      "                   plus PREFIX.json listing the sequence\n"
      "                   (default: eye-capture)\n"
      "\n"
      "Animation:\n"
      "  --gaze X,Y       Point the gaze and hold it, each -1..1; implies\n"
      "                   --no-auto-gaze. +X is right and +Y is up on screen\n"
      "  --no-auto-gaze   Hold the gaze still instead of letting it wander\n"
      "  --no-auto-blink  Never blink on its own\n"
      "  --quiet          Suppress the library's startup narration\n"
      "\n"
      "Windowed keys (press ? in the window for the same list):\n",
      argv0);
  for (const KeyHelp &row : kKeyHelp)
    printf("  %-10s %s\n", row.keys, row.what);
}

/**
 * @brief Parse the command line.
 * @param argc  Argument count.
 * @param argv  Arguments.
 * @param opt   Receives the settings.
 * @param list  Set when --list was given.
 * @return true if the arguments made sense.
 */
bool parseArgs(int argc, char **argv, Options &opt, bool &list) {
  list = false;
  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    const bool hasValue = (i + 1 < argc);
    auto value = [&](void) -> const char * { return argv[++i]; };

    if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
      usage(argv[0]);
      exit(0);
    } else if (!strcmp(a, "--list")) {
      list = true;
    } else if (!strcmp(a, "--panel")) {
      opt.panel = true;
    } else if (!strcmp(a, "--images")) {
      opt.images = true;
    } else if (!strcmp(a, "--quiet")) {
      opt.quiet = true;
    } else if (!strcmp(a, "--no-auto-gaze")) {
      opt.autoGaze = false;
      opt.autoGazeSet = true;
    } else if (!strcmp(a, "--no-auto-blink")) {
      opt.autoBlink = false;
      opt.autoBlinkSet = true;
    } else if (!hasValue) {
      fprintf(stderr, "%s needs a value\n", a);
      return false;
    } else if (!strcmp(a, "--assets")) {
      opt.assetRoot = value();
    } else if (!strcmp(a, "--eye")) {
      opt.eyeId = value();
    } else if (!strcmp(a, "--out")) {
      opt.outPrefix = value();
    } else if (!strcmp(a, "--scale")) {
      opt.scale = atoi(value());
    } else if (!strcmp(a, "--panel-width")) {
      opt.panelWidth = atoi(value());
    } else if (!strcmp(a, "--frames")) {
      opt.frames = atoi(value());
      opt.headless = true;
    } else if (!strcmp(a, "--skip")) {
      opt.skip = atoi(value());
    } else if (!strcmp(a, "--fps")) {
      opt.fps = atoi(value());
    } else if (!strcmp(a, "--gaze")) {
      const char *v = value();
      if (sscanf(v, "%f,%f", &opt.gazeFixedX, &opt.gazeFixedY) != 2) {
        fprintf(stderr, "--gaze wants two numbers, as in --gaze 1,0\n");
        return false;
      }
      opt.gazeFixed = true;
      opt.autoGaze = false;
    } else if (!strcmp(a, "--seed")) {
      opt.seed = (uint32_t)strtoul(value(), nullptr, 0);
    } else {
      fprintf(stderr, "Unknown option %s\n", a);
      return false;
    }
  }

  if (opt.scale < 1)
    opt.scale = 1;
  if (opt.panelWidth < 200)
    opt.panelWidth = 200;
  if (opt.fps < 0)
    opt.fps = 0;
  if (opt.skip < 0)
    opt.skip = 0;
  if (opt.outPrefix.empty())
    opt.outPrefix = "eye-capture";
  return true;
}

// ---------------------------------------------------------------------------
//  CAPTURE
// ---------------------------------------------------------------------------

/**
 * @brief Render frames and write each one out.
 *
 * Always advances the virtual clock by exactly one frame's worth per frame, in
 * headless and windowed alike, so a capture taken from the window is as
 * reproducible as one taken from the command line.
 *
 * @param host      Live renderer.
 * @param display   Backend holding the pixels.
 * @param opt       Settings; fps, skip and the output prefix are used.
 * @param count     Frames to write.
 * @param eyeName   Package name to record in the sidecars.
 * @return Frames actually written.
 */
int captureSequence(EyeHost &host, LinuxDisplay &display, const Options &opt,
                    int count, const char *eyeName) {
  Adafruit_Monster_Eyes *eyes = host.eyes();
  if (!eyes || count <= 0)
    return 0;

  // A capture is always stepped, even when the window is running uncapped:
  // there is no such thing as a reproducible sequence without a fixed step.
  const int captureFps = opt.fps > 0 ? opt.fps : 30;
  const uint32_t stepUs = (uint32_t)(1000000 / captureFps);
  std::vector<FrameState> states;
  states.reserve((size_t)count);

  for (int i = 0; i < opt.skip; ++i) {
    eyes->animate();
    hostClockAdvance(stepUs);
  }

  char path[1024];
  for (int i = 0; i < count; ++i) {
    const uint64_t startedUs = hostWallMicros();
    eyes->animate();
    const float wallMs = (float)(hostWallMicros() - startedUs) / 1000.0f;

    snprintf(path, sizeof(path), "%s-%03d.png", opt.outPrefix.c_str(), i);
    if (!writePng(path, display.framebuffer(), LinuxDisplay::PANEL_W,
                  LinuxDisplay::PANEL_H)) {
      fprintf(stderr, "Could not write %s\n", path);
      return i;
    }
    const FrameState state = captureState(*eyes, i, eyeName, wallMs);
    states.push_back(state);

    snprintf(path, sizeof(path), "%s-%03d.json", opt.outPrefix.c_str(), i);
    writeStateJson(path, state);

    hostClockAdvance(stepUs);
  }

  snprintf(path, sizeof(path), "%s.json", opt.outPrefix.c_str());
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s-%%03d.png", opt.outPrefix.c_str());
  writeManifestJson(path, states.data(), states.size(), pattern);

  printf("Wrote %zu frames to %s-000.png .. %s-%03d.png, summary in %s\n",
         states.size(), opt.outPrefix.c_str(), opt.outPrefix.c_str(),
         (int)states.size() - 1, path);
  return (int)states.size();
}

// ---------------------------------------------------------------------------
//  WINDOW
// ---------------------------------------------------------------------------

#ifndef SIM_HEADLESS_ONLY

/**
 * @brief Run the interactive preview until the window is closed.
 * @param host     Live renderer, reloaded in place when the eye changes.
 * @param display  Backend holding the pixels.
 * @param packages Packages the eye can be switched between; Save As adds to
 *                 this, so it is held by value.
 * @param opt      Settings.
 * @return Process exit status.
 */
int runWindow(EyeHost &host, LinuxDisplay &display,
              std::vector<Package> packages, Options &opt) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  // The eye keeps its integer scale whether or not the editor is open; the
  // window simply grows to the right to make room, so toggling the panel never
  // resamples the picture and the eye never moves.
  const int eyeW = LinuxDisplay::PANEL_W * opt.scale;
  const int eyeH = LinuxDisplay::PANEL_H * opt.scale;
  bool panelOpen = opt.panel;
  const int panelW = opt.panelWidth;
  // The help strip sits BELOW the eye rather than over it, so the preview is
  // never obscured. Three lines is enough for the longest explanation at this
  // width; it only exists while the editor is open.
  const int helpH = (int)(opt.scale * 0.5f * 13.0f * 3.0f) + 12;
  auto windowW = [&](bool open) { return eyeW + (open ? panelW : 0); };
  auto windowH = [&](bool open) { return eyeH + (open ? helpH : 0); };
  const int winW = windowW(panelOpen);
  const int winH = windowH(panelOpen);
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("Monster Eyes preview", winW, winH, 0,
                                   &window, &renderer)) {
    fprintf(stderr, "Could not open a window: %s\n", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Texture *texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                        SDL_TEXTUREACCESS_STREAMING, LinuxDisplay::PANEL_W,
                        LinuxDisplay::PANEL_H);
  if (!texture) {
    fprintf(stderr, "Could not create a texture: %s\n", SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  // Nearest neighbour, so what is on screen is the panel's own pixels enlarged
  // rather than a smoothed guess at them. Pixel-level artefacts are most of
  // what this tool exists to show.
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

  // Dear ImGui, for the config editor. Its font is scaled with the window so
  // the panel stays readable at a 3x or 4x eye scale.
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // Do not litter the working directory
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes((float)opt.scale * 0.5f);
  io.FontGlobalScale = (float)opt.scale * 0.5f;
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  // The editor writes config.eye files for the renderer to read, into a
  // scratch mirror of the asset tree rather than into the repository. Built on
  // first use, so a run that never opens the panel costs nothing.
  // Declared before the lambdas that capture it. The name also shadows POSIX
  // index(3), which is why it cannot simply be used earlier than this.
  size_t index = host.index();

  // A renderer that is constructed and never begun, purely to read the
  // library's defaults in config space. begin() is what rescales geometry to
  // the display, so these must be taken before it runs.
  const EyesSettings configDefaults = Adafruit_Monster_Eyes(&display).config();

  // Applying an edit means handing the renderer a config.eye to parse, since
  // that is the only way it takes settings. The shim serves that from memory,
  // so no file is written and no copy of the asset tree exists on disk.
  ConfigDocument doc;
  PanelState panelState;
  bool docLoaded = false;

  // The bitmap editor. Edited images live here and are pushed into the overlay
  // exactly as an edited config is, so the package on disk is untouched until
  // it is saved.
  // Its own OS window, so it can be moved, resized and put on another monitor
  // independently of the preview. That means a second renderer and a second
  // ImGui context: ImGui's own multi-viewport support is docking-branch only,
  // and a context holds exactly one window's input and draw state.
  ImGuiContext *mainCtx = ImGui::GetCurrentContext();
  ImGuiContext *imageCtx = nullptr;
  SDL_Window *imageWindow = nullptr;
  SDL_Renderer *imageRenderer = nullptr;
  bool imagesOpen = false;
  ImageDocument image;
  ImageEditorState imageState;
  SDL_Texture *imageTexture = nullptr;
  std::vector<std::string> imageNames;
  size_t imageIndex = 0;
  bool imageLoaded = false;
  // Every bitmap edited in this package, by device path, so a save writes the
  // edits rather than copying the originals over them.
  std::map<std::string, std::vector<uint8_t>> editedImages;
  // Edited config.eye text, by device path. Both maps are keyed by a path that
  // carries the package id, so an edit to one package is inert while another
  // is loaded and comes back into force on returning to it. That is what makes
  // edits survive switching eyes.
  std::map<std::string, std::string> editedConfigs;

  // Forget everything edited in one package, for Revert and for r.
  auto discardEdits = [&](size_t which) {
    const std::string prefix = "/eyes/" + packages[which].id + "/";
    for (auto it = editedImages.begin(); it != editedImages.end();) {
      if (it->first.compare(0, prefix.size(), prefix) == 0) {
        FFat.clearOverlay(it->first.c_str());
        it = editedImages.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = editedConfigs.begin(); it != editedConfigs.end();) {
      if (it->first.compare(0, prefix.size(), prefix) == 0) {
        FFat.clearOverlay(it->first.c_str());
        it = editedConfigs.erase(it);
      } else {
        ++it;
      }
    }
  };

  auto closeImageWindow = [&]() {
    if (!imageCtx)
      return;
    // Torn down in the reverse order it was built, with the context current
    // throughout: the backends free their own per-context state.
    ImGui::SetCurrentContext(imageCtx);
    if (imageTexture) {
      SDL_DestroyTexture(imageTexture);
      imageTexture = nullptr;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(imageCtx);
    imageCtx = nullptr;
    ImGui::SetCurrentContext(mainCtx);
    if (imageRenderer) {
      SDL_DestroyRenderer(imageRenderer);
      imageRenderer = nullptr;
    }
    if (imageWindow) {
      SDL_DestroyWindow(imageWindow);
      imageWindow = nullptr;
    }
    imagesOpen = false;
  };

  auto openImageWindow = [&]() -> bool {
    if (imageCtx)
      return true;
    if (!SDL_CreateWindowAndRenderer("Eye images", 1000, 640,
                                     SDL_WINDOW_RESIZABLE, &imageWindow,
                                     &imageRenderer)) {
      fprintf(stderr, "Could not open the image editor window: %s\n",
              SDL_GetError());
      imageWindow = nullptr;
      imageRenderer = nullptr;
      return false;
    }
    IMGUI_CHECKVERSION();
    imageCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(imageCtx);
    ImGuiIO &io2 = ImGui::GetIO();
    io2.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes((float)opt.scale * 0.5f);
    io2.FontGlobalScale = (float)opt.scale * 0.5f;
    ImGui_ImplSDL3_InitForSDLRenderer(imageWindow, imageRenderer);
    ImGui_ImplSDLRenderer3_Init(imageRenderer);
    ImGui::SetCurrentContext(mainCtx);
    imagesOpen = true;
    return true;
  };

  auto packageDir = [&](size_t which) {
    return opt.assetRoot + "/eyes/" + packages[which].id;
  };

  auto listImages = [&](size_t which) {
    imageNames.clear();
    DIR *dir = opendir(packageDir(which).c_str());
    if (!dir)
      return;
    while (const struct dirent *entry = readdir(dir)) {
      const std::string n = entry->d_name;
      if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bmp") == 0)
        imageNames.push_back(n);
    }
    closedir(dir);
    std::sort(imageNames.begin(), imageNames.end());
  };

  auto devicePathFor = [&](size_t which, const std::string &name) {
    return "/eyes/" + packages[which].id + "/" + name;
  };

  // Read a bitmap into the editor: an edit already made this session if there
  // is one, otherwise the file.
  auto openImage = [&](size_t which, size_t nameIndex) {
    if (nameIndex >= imageNames.size())
      return;
    imageIndex = nameIndex;
    const std::string devicePath = devicePathFor(which, imageNames[nameIndex]);
    std::vector<uint8_t> bytes;
    const auto edited = editedImages.find(devicePath);
    if (edited != editedImages.end()) {
      bytes = edited->second;
    } else {
      FILE *f = fopen((packageDir(which) + "/" + imageNames[nameIndex]).c_str(),
                      "rb");
      if (f) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
          bytes.insert(bytes.end(), buf, buf + n);
        fclose(f);
      }
    }
    std::string error;
    imageLoaded = image.load(bytes, &error);
    imageState.message = imageLoaded ? "" : error;
    if (imageLoaded) {
      // Start on a colour the image actually uses rather than an arbitrary one.
      imageState.color = image.kind() == ImageDocument::Indexed1
                             ? image.paletteColor(true)
                             : image.pixel(0, 0);
      imageState.altColor = image.kind() == ImageDocument::Indexed1
                                ? image.paletteColor(false)
                                : 0x000000;
    }
  };

  // Hand the edited bitmap to the renderer, which reloads it on the rebuild.
  auto applyImage = [&](size_t which) {
    if (!imageLoaded)
      return;
    const std::string devicePath = devicePathFor(which, imageNames[imageIndex]);
    std::vector<uint8_t> bytes = image.encode();
    FFat.setOverlay(devicePath.c_str(),
                    std::string((const char *)bytes.data(), bytes.size()));
    editedImages[devicePath] = std::move(bytes);
  };


  // Read the package's real config.eye into the editor. The file on disk is
  // only ever read; an edit shadows it in memory rather than replacing it.
  auto openDocument = [&](size_t which) {
    const auto edited = editedConfigs.find(packages[which].config);
    if (edited != editedConfigs.end())
      doc.loadText(edited->second);
    else
      doc.load(opt.assetRoot + "/eyes" + "/" + packages[which].id +
               "/config.eye");
    panelState.message.clear();
    docLoaded = true;
  };

  auto applyDocument = [&](size_t which) -> bool {
    const std::string json = doc.serialise();
    FFat.setOverlay(packages[which].config.c_str(), json);
    editedConfigs[packages[which].config] = json;
    return true;
  };

  // Everything that has to happen when the package changes. This exists
  // because it was open-coded at each of the four places that can change the
  // package, and two of them -- the style drop-down and Save As -- were left
  // without the image half, so the editor went on showing the previous
  // package's bitmaps.
  auto enterPackage = [&](size_t which) {
    const std::string keep =
        imageIndex < imageNames.size() ? imageNames[imageIndex] : std::string();
    index = which;
    // Nothing is cleared. The overlays and both edit maps are keyed by device
    // path, so another package's edits simply do not match anything the
    // renderer asks for while this one is loaded.
    if (panelOpen || docLoaded)
      openDocument(index);
    listImages(index);
    if (imageCtx) {
      // Stay on the same filename where the new package has one, since
      // stepping through packages to compare the same texture is the common
      // reason to be doing this at all.
      size_t want = 0;
      for (size_t i = 0; i < imageNames.size(); ++i)
        if (imageNames[i] == keep)
          want = i;
      openImage(index, want);
    } else {
      imageLoaded = false;
    }
    host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink,
              opt.autoGazeSet, opt.autoBlinkSet);
  };

  // The window follows the host clock: the frame rate the library reports here
  // is the one it is really achieving, not a figure derived from a fixed step.
  hostClockUseRealTime(true);

  // WITHOUT THIS THE PUPILS PULSATE. updateIris() is driven by a frame
  // counter, not by the clock -- it walks a 2^7 = 128 frame fractal cycle, one
  // step per animate(), with no time term anywhere in it. On the device that
  // cycle takes seconds because the panel is the bottleneck. Here, drawing into
  // memory with nothing to wait for, the loop free-runs into the thousands of
  // frames per second and the same cycle finishes in milliseconds.
  //
  // So the window is paced to --fps. The gaze and blink animators would not
  // care either way, being driven by micros(), but the iris is only meaningful
  // at something like the rate the hardware achieves.
  const uint64_t frameNs = opt.fps > 0 ? 1000000000ULL / (uint64_t)opt.fps : 0;
  uint64_t nextFrameNs = SDL_GetTicksNS();

  if (panelOpen)
    openDocument(index);
  listImages(index);
  if (opt.images && openImageWindow())
    openImage(index, 0);

  bool running = true;
  bool overlay = true;
  bool showKeys = false;
  bool mouseGaze = false;
  float gazeX = 0.0f, gazeY = 0.0f;
  int captureCount = 8;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      const Uint32 evWindow = eventWindowId(event);
      const Uint32 imageWindowId =
          imageWindow ? SDL_GetWindowID(imageWindow) : 0;
      const bool forImageWindow = imageCtx && evWindow == imageWindowId;

      if (forImageWindow) {
        ImGui::SetCurrentContext(imageCtx);
        ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui::SetCurrentContext(mainCtx);
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
          closeImageWindow();
        continue; // The preview never sees the editor's input
      }
      ImGui_ImplSDL3_ProcessEvent(&event);

      // While a text field has focus, 'q' means the letter q.
      const bool uiOpen = panelOpen;
      const bool imguiWantsKeys = uiOpen && ImGui::GetIO().WantCaptureKeyboard;
      const bool imguiWantsMouse = uiOpen && ImGui::GetIO().WantCaptureMouse;
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_KEY_DOWN && imguiWantsKeys) {
        // Swallowed by the editor.
      } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouseGaze &&
                 !imguiWantsMouse) {
        // Window coordinates to the -1..1 the library takes.
        gazeX = (event.motion.x / (float)eyeW) * 2.0f - 1.0f;
        gazeY = 1.0f - (event.motion.y / (float)eyeH) * 2.0f;
        gazeX = gazeX < -1.0f ? -1.0f : (gazeX > 1.0f ? 1.0f : gazeX);
        if (host.eyes())
          setScreenGaze(*host.eyes(), gazeX, gazeY);
      } else if (event.type == SDL_EVENT_KEY_DOWN) {
        Adafruit_Monster_Eyes *eyes = host.eyes();
        switch (event.key.key) {
        case SDLK_ESCAPE:
          // Escape dismisses the key list before it quits, which is what a
          // reader who opened it by accident will expect.
          if (showKeys)
            showKeys = false;
          else
            running = false;
          break;
        case SDLK_Q:
          running = false;
          break;
        // Both, deliberately. Whether a shifted slash arrives as SDLK_QUESTION
        // or SDLK_SLASH depends on how SDL translates keycodes for the current
        // layout, and on layouts where ? is not above the slash it may be
        // neither. Accepting both means ? works without having to care, and
        // costs only that a bare slash opens the list too.
        case SDLK_QUESTION:
        case SDLK_SLASH:
          showKeys = !showKeys;
          break;
        case SDLK_SPACE:
          if (eyes)
            eyes->blink();
          break;
        case SDLK_TAB:
          overlay = !overlay;
          break;
        case SDLK_I:
          if (imagesOpen) {
            closeImageWindow();
          } else if (openImageWindow()) {
            if (!imageLoaded)
              openImage(index, imageIndex);
          }
          break;
        case SDLK_P:
          panelOpen = !panelOpen;
          if (panelOpen && !docLoaded)
            openDocument(index);
          SDL_SetWindowSize(window, windowW(panelOpen), windowH(panelOpen));
          break;
        case SDLK_G:
          if (eyes) {
            const bool on = !eyes->autoGaze();
            eyes->setAutoGaze(on);
            if (on)
              eyes->releaseGaze();
          }
          break;
        case SDLK_B:
          if (eyes)
            eyes->setAutoBlink(!eyes->autoBlink());
          break;
        case SDLK_M:
          mouseGaze = !mouseGaze;
          if (!mouseGaze && eyes)
            eyes->releaseGaze();
          break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_UP:
        case SDLK_DOWN: {
          const float step = 0.1f;
          if (event.key.key == SDLK_LEFT)
            gazeX -= step;
          else if (event.key.key == SDLK_RIGHT)
            gazeX += step;
          else if (event.key.key == SDLK_UP)
            gazeY += step;
          else
            gazeY -= step;
          gazeX = gazeX < -1.0f ? -1.0f : (gazeX > 1.0f ? 1.0f : gazeX);
          gazeY = gazeY < -1.0f ? -1.0f : (gazeY > 1.0f ? 1.0f : gazeY);
          if (eyes)
            setScreenGaze(*eyes, gazeX, gazeY);
          break;
        }
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET: {
          if (packages.size() < 2)
            break;
          const int direction = (event.key.key == SDLK_RIGHTBRACKET) ? 1 : -1;
          enterPackage((index + packages.size() + (size_t)direction) %
                       packages.size());
          break;
        }
        case SDLK_R:
          // Rebuilding is the only honest reload: the polar maps and the
          // texture budget are both sized from the config being reread. This
          // one really does mean "go back to the file", so this package's
          // edits go -- but only this package's.
          discardEdits(index);
          enterPackage(index);
          break;
        case SDLK_C: {
          // Captures must be reproducible, so the clock stops following the
          // host for the length of the sequence and steps by --fps instead.
          hostClockUseRealTime(false);
          Options capture = opt;
          capture.skip = 0;
          captureSequence(host, display, capture, captureCount,
                          packages[index].id.c_str());
          hostClockUseRealTime(true);
          break;
        }
        default:
          break;
        }
      }
    }

    if (host.eyes())
      host.eyes()->animate();

    if (display.framebuffer())
      SDL_UpdateTexture(texture, nullptr, display.framebuffer(),
                        LinuxDisplay::PANEL_W * (int)sizeof(uint16_t));

    PanelResult panel;
    ImageEditorResult imageResult;
    const bool anyUi = panelOpen;
    if (anyUi) {
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      PanelLayout layout;
      layout.panelX = (float)eyeW;
      layout.panelW = (float)panelW;
      layout.panelH = (float)windowH(true);
      layout.helpX = 0.0f;
      layout.helpY = (float)eyeH;
      layout.helpW = (float)eyeW;
      layout.helpH = (float)helpH;
      std::vector<std::string> names;
      names.reserve(packages.size());
      for (const Package &p : packages)
        names.push_back(p.id);
      if (panelOpen)
        panel = drawConfigPanel(doc, configDefaults, panelState, layout, names,
                                index);
      ImGui::Render();
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    const SDL_FRect eyeRect = {0.0f, 0.0f, (float)eyeW, (float)eyeH};
    SDL_RenderTexture(renderer, texture, nullptr, &eyeRect);

    if (overlay) {
      // SDL's built-in debug font is 8px, which is unreadable at a 3x window
      // scale, so the render scale is raised for the text alone.
      const float textScale = (float)opt.scale * 0.5f;
      SDL_SetRenderScale(renderer, textScale, textScale);
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
      const float tx = 4.0f;
      char line[256];
      Adafruit_Monster_Eyes *eyes = host.eyes();
      snprintf(line, sizeof(line), "%s  %.1f fps  %dpx",
               packages[index].id.c_str(), eyes ? eyes->frameRate() : 0.0f,
               eyes ? eyes->eyeSize() : 0);
      SDL_RenderDebugText(renderer, tx, 4, line);
      snprintf(line, sizeof(line), "gaze %+.2f %+.2f  blink %.2f  iris %.2f",
               eyes ? screenGazeX(*eyes) : 0.0f,
               eyes ? screenGazeY(*eyes) : 0.0f,
               eyes ? eyes->blinkPhase() : 0.0f,
               eyes ? eyes->irisFraction() : 0.0f);
      SDL_RenderDebugText(renderer, tx, 16, line);
      snprintf(line, sizeof(line), "auto gaze %s  blink %s  mouse %s   ? keys",
               (eyes && eyes->autoGaze()) ? "on" : "off",
               (eyes && eyes->autoBlink()) ? "on" : "off",
               mouseGaze ? "on" : "off");
      SDL_RenderDebugText(renderer, tx, 28, line);
      SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    }

    if (showKeys) {
      const float textScale = (float)opt.scale * 0.5f;
      // Dim the eye rather than hide it, so a key can be judged against what is
      // on screen while the list is open.
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
      SDL_RenderFillRect(renderer, &eyeRect);

      SDL_SetRenderScale(renderer, textScale, textScale);
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

      // Laid out in the scaled coordinate space, where the debug font's glyphs
      // are 8 pixels wide and the window is winW/textScale across.
      const float lineH = 12.0f;
      const float kx = 10.0f;
      float y = 10.0f;
      SDL_RenderDebugText(renderer, kx, y, "KEYS");
      y += lineH * 1.5f;
      char line[128];
      for (const KeyHelp &row : kKeyHelp) {
        snprintf(line, sizeof(line), "%-10s %s", row.keys, row.what);
        SDL_RenderDebugText(renderer, kx, y, line);
        y += lineH;
      }
      y += lineH * 0.5f;
      snprintf(line, sizeof(line), "eye %s   %d fps cap   scale %dx",
               packages[index].id.c_str(), opt.fps, opt.scale);
      SDL_RenderDebugText(renderer, kx, y, line);
      y += lineH;
      SDL_RenderDebugText(renderer, kx, y, "? or escape closes this");

      SDL_SetRenderScale(renderer, 1.0f, 1.0f);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    if (anyUi)
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    SDL_RenderPresent(renderer);

    if (imageCtx && imageLoaded) {
      ImGui::SetCurrentContext(imageCtx);
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      int ww = 0, wh = 0;
      SDL_GetWindowSize(imageWindow, &ww, &wh);
      imageResult = drawImageEditor(image, imageState, imageNames, imageIndex,
                                    imageRenderer, &imageTexture, (float)ww,
                                    (float)wh);
      ImGui::Render();
      SDL_SetRenderDrawColor(imageRenderer, 30, 30, 34, 255);
      SDL_RenderClear(imageRenderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),
                                            imageRenderer);
      SDL_RenderPresent(imageRenderer);
      ImGui::SetCurrentContext(mainCtx);
    }

    // Acted on after presenting, so a rebuild happens once per frame however
    // many widgets a drag touched.
    if (imageResult.selectImage >= 0 &&
        (size_t)imageResult.selectImage != imageIndex) {
      openImage(index, (size_t)imageResult.selectImage);
    } else if (imageResult.revertRequested) {
      // Drop this one image's edit so openImage() falls back to its file.
      if (imageIndex < imageNames.size()) {
        const std::string p = devicePathFor(index, imageNames[imageIndex]);
        editedImages.erase(p);
        FFat.clearOverlay(p.c_str());
      }
      openImage(index, imageIndex);
      host.reload(packages, index, !opt.quiet, mouseGaze || opt.gazeFixed,
                  opt.autoGaze, opt.autoBlink, opt.autoGazeSet,
                  opt.autoBlinkSet);
    } else if (imageResult.imageChanged) {
      applyImage(index);
      host.reload(packages, index, !opt.quiet, mouseGaze || opt.gazeFixed,
                  opt.autoGaze, opt.autoBlink, opt.autoGazeSet,
                  opt.autoBlinkSet);
    }

    if (panel.selectPackage >= 0 &&
        (size_t)panel.selectPackage != index) {
      enterPackage((size_t)panel.selectPackage);
    } else if (panel.revertRequested) {
      FFat.clearOverlay(packages[index].config.c_str());
      editedConfigs.erase(packages[index].config);
      openDocument(index);
      host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink,
                opt.autoGazeSet, opt.autoBlinkSet);
    } else if (panel.configChanged && applyDocument(index)) {
      host.reload(packages, index, !opt.quiet, mouseGaze || opt.gazeFixed,
                  opt.autoGaze, opt.autoBlink, opt.autoGazeSet,
                  opt.autoBlinkSet);
    }

    if (panel.saveRequested) {
      const std::string fromDir = opt.assetRoot + "/eyes/" +
                                  packages[index].id;
      const std::string toDir = opt.assetRoot + "/eyes/" + panel.saveAsId;
      std::string error;
      if (!copyPackageAssets(fromDir, toDir, &error)) {
        panelState.message = error;
        panelState.messageIsError = true;
      } else {
        // copyPackageAssets() brought the originals across; anything edited
        // in this session overwrites its copy, or the save would quietly
        // discard exactly the work being saved.
        //
        // Only THIS package's edits: the map holds every package edited this
        // session, and matching on the basename alone would drop another
        // eye's iris.bmp into the one being saved.
        const std::string fromPrefix = "/eyes/" + packages[index].id + "/";
        for (const auto &edit : editedImages) {
          if (edit.first.compare(0, fromPrefix.size(), fromPrefix) != 0)
            continue;
          const std::string name = edit.first.substr(fromPrefix.size());
          FILE *bf = fopen((toDir + "/" + name).c_str(), "wb");
          if (bf) {
            fwrite(edit.second.data(), 1, edit.second.size(), bf);
            fclose(bf);
          }
        }
        const std::string configPath = toDir + "/config.eye";
        FILE *f = fopen(configPath.c_str(), "wb");
        const std::string json = doc.serialise();
        if (f && fwrite(json.data(), 1, json.size(), f) == json.size()) {
          fclose(f);
          doc.clearDirty();
          panelState.message = "Saved as " + panel.saveAsId;
          panelState.messageIsError = false;
          // Make it selectable with [ and ] straight away, and move onto it,
          // which also points the editor at the saved package's bitmaps.
          packages = findPackages(opt.assetRoot);
          size_t saved = index;
          for (size_t i = 0; i < packages.size(); ++i)
            if (packages[i].id == panel.saveAsId)
              saved = i;
          enterPackage(saved);
          panelState.saveAsId[0] = 0;
        } else {
          if (f)
            fclose(f);
          panelState.message = "Could not write config.eye";
          panelState.messageIsError = true;
        }
      }
    }

    if (frameNs) {
      const uint64_t now = SDL_GetTicksNS();
      nextFrameNs += frameNs;
      if (nextFrameNs > now)
        SDL_DelayNS(nextFrameNs - now);
      else
        nextFrameNs = now; // Fell behind; do not try to catch up in a burst
    }
  }

  closeImageWindow();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

#endif // SIM_HEADLESS_ONLY

} // namespace

int main(int argc, char **argv) {
  Options opt;
  bool list = false;
  if (!parseArgs(argc, argv, opt, list)) {
    fprintf(stderr, "Try --help.\n");
    return 2;
  }

  // Pinned before begin(), which seeds itself from micros(). Without this the
  // sequence would still be reproducible under the virtual clock, but only by
  // accident of when begin() happened to run.
  hostRandomForceSeed(opt.seed);

  const std::vector<Package> packages = findPackages(opt.assetRoot);
  if (packages.empty()) {
    fprintf(stderr,
            "No eye packages found. Point --assets at the directory holding "
            "eyes/<id>/config.eye.\n");
    return 1;
  }

  if (list) {
    for (const Package &p : packages)
      printf("%s\n", p.id.c_str());
    return 0;
  }

  size_t index = 0;
  if (!opt.eyeId.empty()) {
    bool matched = false;
    for (size_t i = 0; i < packages.size(); ++i) {
      if (packages[i].id == opt.eyeId) {
        index = i;
        matched = true;
        break;
      }
    }
    if (!matched) {
      fprintf(stderr, "No eye package called '%s'. Try --list.\n",
              opt.eyeId.c_str());
      return 1;
    }
  }

  // The library reaches the assets through FFat, exactly as it does on the
  // device; all that changes is which directory the device paths resolve to.
  FFat.setRoot(opt.assetRoot.c_str());

  LinuxDisplay display;
  EyeHost host(display);
  if (opt.gazeFixed)
    host.holdGaze(opt.gazeFixedX, opt.gazeFixedY);
  if (!host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink,
                 opt.autoGazeSet, opt.autoBlinkSet))
    return 1;

  if (opt.headless) {
    if (opt.frames <= 0) {
      fprintf(stderr, "--frames needs a positive count.\n");
      return 2;
    }
    const int written = captureSequence(host, display, opt, opt.frames,
                                        packages[index].id.c_str());
    return written == opt.frames ? 0 : 1;
  }

#ifdef SIM_HEADLESS_ONLY
  fprintf(stderr, "Built without SDL3; only --frames captures are available.\n");
  return 2;
#else
  return runWindow(host, display, packages, opt);
#endif
}
