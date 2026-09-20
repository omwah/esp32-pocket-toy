/**
 * @file capture.h
 * @brief Writing a frame out for an AI agent to look at: a PNG of the panel and
 *        a JSON sidecar of the animator's state at that instant.
 *
 * The pixels answer "what does it look like"; the sidecar answers "why". An
 * agent asked whether a blink looks right can read blinkPhase across the
 * sequence instead of inferring it from eyelid pixels.
 */

#ifndef _SIM_CAPTURE_H_
#define _SIM_CAPTURE_H_

#include <stdint.h>
#include <stddef.h>

class Adafruit_Monster_Eyes;

/** @brief Everything the sidecar records about one rendered frame. */
struct FrameState {
  int index;          ///< Position in the captured sequence, from 0
  uint32_t timeUs;    ///< Simulated microseconds since startup
  const char *eyeName;///< Eye package being rendered
  int eyeSize;        ///< Rendered eye width and height in pixels
  float gazeMapX;     ///< Gaze in map pixels, the renderer's own units
  float gazeMapY;     ///< Gaze in map pixels
  float gazeX;        ///< Gaze as the library holds it, -1..1
  float gazeY;        ///< Gaze as the library holds it, -1..1
  float gazeScreenX;  ///< Gaze as the viewer sees it: -1 left, 1 right
  float gazeScreenY;  ///< Gaze as the viewer sees it: -1 down, 1 up
  float blinkPhase;   ///< 0 open, 1 fully shut
  float irisFraction; ///< Pupil dilation, 0..1
  float pupil;        ///< Pupil size as the library reports it
  float renderMs;     ///< The library's own figure; 0 under the virtual clock
  float transferMs;   ///< The library's own figure; 0 on a memory backend
  float wallMs;       ///< Host time this frame really took to render
  float frameRate;    ///< Frames per second the library reports
  bool autoGaze;      ///< Was the gaze animator driving?
  bool autoBlink;     ///< Was the blink animator driving?
  // Both come from the renderer, so they reflect extensions.animation in the
  // config as well as the command line and the g and b keys.
};

/**
 * @brief Fill a FrameState from a live eyes instance.
 * @param eyes    The instance to read.
 * @param index   Sequence position.
 * @param eyeName Package name to record.
 * @param wallMs  Host milliseconds the frame took, measured by the caller.
 * @return The populated state.
 */
FrameState captureState(Adafruit_Monster_Eyes &eyes, int index,
                        const char *eyeName, float wallMs);

/** @brief Host monotonic clock, for timing a frame.
 *  @return Microseconds; only differences are meaningful. */
uint64_t hostWallMicros(void);

/**
 * @brief Write an RGB565 framebuffer as an 8-bit RGB PNG.
 *
 * Deflated with zlib rather than stored, so a sequence of frames stays small
 * enough to hand around. 5-6 bit channels are expanded by replicating the high
 * bits, which is what the panel's own scaler does, so the PNG matches what the
 * hardware shows rather than being uniformly dark.
 *
 * @param path   Destination file.
 * @param pixels Native-endian RGB565, width * height of them.
 * @param width  Image width.
 * @param height Image height.
 * @return true if the file was written.
 */
bool writePng(const char *path, const uint16_t *pixels, int width, int height);

/**
 * @brief Write one frame's state as JSON.
 * @param path  Destination file.
 * @param state State to record.
 * @return true if the file was written.
 */
bool writeStateJson(const char *path, const FrameState &state);

/**
 * @brief Write a sequence summary listing every captured frame.
 * @param path   Destination file.
 * @param states Frames, in order.
 * @param count  Number of frames.
 * @param pngPattern printf pattern used for the frame filenames, recorded so a
 *        reader can find the images.
 * @return true if the file was written.
 */
bool writeManifestJson(const char *path, const FrameState *states, size_t count,
                       const char *pngPattern);

#endif // _SIM_CAPTURE_H_
