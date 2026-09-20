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
#include <string>
#include <vector>

#ifndef SIM_HEADLESS_ONLY
#include <SDL3/SDL.h>
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
  bool autoGaze = true;           ///< Let the gaze animator run
  bool gazeFixed = false;         ///< Hold the gaze at gazeFixedX/Y
  float gazeFixedX = 0.0f;        ///< Held gaze, -1..1
  float gazeFixedY = 0.0f;        ///< Held gaze, -1..1
  bool autoBlink = true;          ///< Let the blink animator run
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
            bool autoGaze, bool autoBlink) {
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
    _eyes->setAutoGaze(autoGaze);
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
    } else if (!strcmp(a, "--quiet")) {
      opt.quiet = true;
    } else if (!strcmp(a, "--no-auto-gaze")) {
      opt.autoGaze = false;
    } else if (!strcmp(a, "--no-auto-blink")) {
      opt.autoBlink = false;
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
    const FrameState state =
        captureState(*eyes, i, eyeName, opt.autoGaze, opt.autoBlink, wallMs);
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
 * @param packages Packages the eye can be switched between.
 * @param opt      Settings.
 * @return Process exit status.
 */
int runWindow(EyeHost &host, LinuxDisplay &display,
              const std::vector<Package> &packages, Options &opt) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }

  const int winW = LinuxDisplay::PANEL_W * opt.scale;
  const int winH = LinuxDisplay::PANEL_H * opt.scale;
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

  bool running = true;
  bool overlay = true;
  bool showKeys = false;
  bool mouseGaze = false;
  float gazeX = 0.0f, gazeY = 0.0f;
  int captureCount = 8;
  size_t index = host.index();

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_MOUSE_MOTION && mouseGaze) {
        // Window coordinates to the -1..1 the library takes.
        gazeX = (event.motion.x / (float)winW) * 2.0f - 1.0f;
        gazeY = 1.0f - (event.motion.y / (float)winH) * 2.0f;
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
        case SDLK_G:
          opt.autoGaze = !opt.autoGaze;
          if (eyes) {
            eyes->setAutoGaze(opt.autoGaze);
            if (opt.autoGaze)
              eyes->releaseGaze();
          }
          break;
        case SDLK_B:
          opt.autoBlink = !opt.autoBlink;
          if (eyes)
            eyes->setAutoBlink(opt.autoBlink);
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
          index = (index + packages.size() + (size_t)direction) %
                  packages.size();
          host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink);
          break;
        }
        case SDLK_R:
          // Rebuilding is the only honest reload: the polar maps and the
          // texture budget are both sized from the config being reread.
          host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink);
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

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);

    if (overlay) {
      // SDL's built-in debug font is 8px, which is unreadable at a 3x window
      // scale, so the render scale is raised for the text alone.
      const float textScale = (float)opt.scale * 0.5f;
      SDL_SetRenderScale(renderer, textScale, textScale);
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
      char line[256];
      Adafruit_Monster_Eyes *eyes = host.eyes();
      snprintf(line, sizeof(line), "%s  %.1f fps  %dpx",
               packages[index].id.c_str(), eyes ? eyes->frameRate() : 0.0f,
               eyes ? eyes->eyeSize() : 0);
      SDL_RenderDebugText(renderer, 4, 4, line);
      snprintf(line, sizeof(line), "gaze %+.2f %+.2f  blink %.2f  iris %.2f",
               eyes ? screenGazeX(*eyes) : 0.0f,
               eyes ? screenGazeY(*eyes) : 0.0f,
               eyes ? eyes->blinkPhase() : 0.0f,
               eyes ? eyes->irisFraction() : 0.0f);
      SDL_RenderDebugText(renderer, 4, 16, line);
      snprintf(line, sizeof(line), "auto gaze %s  blink %s  mouse %s   ? keys",
               opt.autoGaze ? "on" : "off", opt.autoBlink ? "on" : "off",
               mouseGaze ? "on" : "off");
      SDL_RenderDebugText(renderer, 4, 28, line);
      SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    }

    if (showKeys) {
      const float textScale = (float)opt.scale * 0.5f;
      // Dim the eye rather than hide it, so a key can be judged against what is
      // on screen while the list is open.
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
      SDL_RenderFillRect(renderer, nullptr);

      SDL_SetRenderScale(renderer, textScale, textScale);
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

      // Laid out in the scaled coordinate space, where the debug font's glyphs
      // are 8 pixels wide and the window is winW/textScale across.
      const float lineH = 12.0f;
      float y = 10.0f;
      SDL_RenderDebugText(renderer, 10, y, "KEYS");
      y += lineH * 1.5f;
      char line[128];
      for (const KeyHelp &row : kKeyHelp) {
        snprintf(line, sizeof(line), "%-10s %s", row.keys, row.what);
        SDL_RenderDebugText(renderer, 10, y, line);
        y += lineH;
      }
      y += lineH * 0.5f;
      snprintf(line, sizeof(line), "eye %s   %d fps cap   scale %dx",
               packages[index].id.c_str(), opt.fps, opt.scale);
      SDL_RenderDebugText(renderer, 10, y, line);
      y += lineH;
      SDL_RenderDebugText(renderer, 10, y, "? or escape closes this");

      SDL_SetRenderScale(renderer, 1.0f, 1.0f);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_RenderPresent(renderer);

    if (frameNs) {
      const uint64_t now = SDL_GetTicksNS();
      nextFrameNs += frameNs;
      if (nextFrameNs > now)
        SDL_DelayNS(nextFrameNs - now);
      else
        nextFrameNs = now; // Fell behind; do not try to catch up in a burst
    }
  }

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
  if (!host.load(packages, index, !opt.quiet, opt.autoGaze, opt.autoBlink))
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
