/**
 * @file image_editor.h
 * @brief A paint window for the bitmaps in an eye package.
 *
 * Edits never reach the disk. A finished stroke is encoded straight back to a
 * BMP in memory and handed to the FFat shim's overlay, so the renderer reloads
 * and the eye beside the editor changes as it is painted.
 *
 * The palette offered depends on what the file can hold, which is not a
 * cosmetic distinction:
 *
 *   Eyelids are 1-bit, so there are two colours and no others. More than that,
 *   the loader reads only the topmost and bottommost lit pixel of each column,
 *   so what is being drawn is a silhouette envelope and not a picture -- a hole
 *   punched in the middle of a lid changes nothing at all. For an upper lid the
 *   top edge of the lit band is where the lid sits fully open and the bottom
 *   edge where it sits fully shut, reversed for a lower lid; a config's squint
 *   decides where between the two it rests. So a lid is drawn in two steps:
 *   stroke its edge with the Bezier tool, then Fill the side that should be
 *   solid. A bare line puts the open and shut positions on the same row, which
 *   pins the lid still.
 *
 *   Iris and sclera are 24-bit, quantised to RGB565 on load, so the picker
 *   offers full colour but shows it rounded to what will survive. They are
 *   polar: x is the angle around the eye and y is distance out from the pupil.
 */

#ifndef _SIM_IMAGE_EDITOR_H_
#define _SIM_IMAGE_EDITOR_H_

#include <stdint.h>
#include <string>
#include <vector>

class ImageDocument;
struct SDL_Renderer;
struct SDL_Texture;

/** @brief What the editor is asking the host to do after a frame. */
struct ImageEditorResult {
  bool imageChanged = false; ///< Re-encode into the overlay and rebuild
  int selectImage = -1;      ///< Switch to this image, or -1
  bool revertRequested = false; ///< Reload this image from its file
};

/** @brief Which tool the pointer is holding. */
enum class ImageTool {
  Pencil,     ///< Freehand
  Eraser,     ///< Freehand, painting the secondary colour
  Fill,       ///< Flood fill a contiguous region
  Eyedropper, ///< Take a colour off the canvas
  Line,       ///< Straight line, drag to place
  Rect,       ///< Rectangle outline, drag to place
  Bezier      ///< Place control points, then stroke the curve
};

/** @brief Editor state that has to live between frames. */
struct ImageEditorState {
  ImageTool tool = ImageTool::Pencil; ///< Current tool
  uint32_t color = 0xFFFFFF;          ///< Primary colour
  uint32_t altColor = 0x000000;       ///< What the eraser paints
  int brush = 1;                      ///< Brush width in image pixels
  float zoom = 0.0f;                  ///< Pixels per image pixel; 0 fits
  bool grid = false;                  ///< Draw a pixel grid when zoomed in

  // Drag and curve state, in image coordinates.
  bool dragging = false;   ///< A drag is in progress
  int dragX0 = 0;          ///< Where the drag started
  int dragY0 = 0;          ///< Where the drag started
  int dragX1 = 0;          ///< Where the pointer is now
  int dragY1 = 0;          ///< Where the pointer is now
  int lastX = -1;          ///< Previous freehand point
  int lastY = -1;          ///< Previous freehand point
  std::vector<int> bezierX; ///< Bezier control points
  std::vector<int> bezierY; ///< Bezier control points
  int bezierDrag = -1;      ///< Control point being dragged, or -1

  std::string message;     ///< A line shown under the toolbar
};

/**
 * @brief Draw the editor for one frame.
 *
 * Emits ImGui commands and may modify @p doc. The caller owns the frame, the
 * overlay and the rebuild.
 *
 * @param doc      Image being edited.
 * @param state    Persistent editor state.
 * @param names    Every bitmap in the package, for the chooser.
 * @param current  Index into @p names of the one open.
 * The editor owns its OS window, so it fills it edge to edge and wears no
 * title bar, border or close button of its own: the window manager already
 * provides those, and a second set inside them is just a window drawn inside a
 * window.
 *
 * @param renderer Renderer the canvas texture belongs to.
 * @param texture  Canvas texture, created and resized as needed.
 * @param width    Host window width in pixels.
 * @param height   Host window height in pixels.
 * @return What the host should do next.
 */
ImageEditorResult drawImageEditor(ImageDocument &doc, ImageEditorState &state,
                                  const std::vector<std::string> &names,
                                  size_t current, SDL_Renderer *renderer,
                                  SDL_Texture **texture, float width,
                                  float height);

#endif // _SIM_IMAGE_EDITOR_H_
