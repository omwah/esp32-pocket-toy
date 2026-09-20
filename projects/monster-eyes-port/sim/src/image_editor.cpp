/**
 * @file image_editor.cpp
 * @brief The paint window: tools, palette and canvas.
 */

#include "image_editor.h"

#include "image_doc.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <map>
#include <math.h>
#include <stdio.h>

namespace {

// ---------------------------------------------------------------------------
//  DRAWING PRIMITIVES
// ---------------------------------------------------------------------------

/** @brief Paint a square of @p brush pixels centred on (x, y). */
void dab(ImageDocument &doc, int x, int y, int brush, uint32_t rgb) {
  const int half = brush / 2;
  for (int dy = 0; dy < brush; ++dy)
    for (int dx = 0; dx < brush; ++dx)
      doc.setPixel(x - half + dx, y - half + dy, rgb);
}

/** @brief Bresenham, so a fast drag leaves a continuous line. */
void drawLine(ImageDocument &doc, int x0, int y0, int x1, int y1, int brush,
              uint32_t rgb) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    dab(doc, x0, y0, brush, rgb);
    if (x0 == x1 && y0 == y1)
      break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

/** @brief Rectangle outline. */
void drawRect(ImageDocument &doc, int x0, int y0, int x1, int y1, int brush,
              uint32_t rgb) {
  drawLine(doc, x0, y0, x1, y0, brush, rgb);
  drawLine(doc, x1, y0, x1, y1, brush, rgb);
  drawLine(doc, x1, y1, x0, y1, brush, rgb);
  drawLine(doc, x0, y1, x0, y0, brush, rgb);
}

/** @brief Flood fill the region of matching colour containing (x, y). */
void floodFill(ImageDocument &doc, int x, int y, uint32_t rgb) {
  if (x < 0 || y < 0 || x >= doc.width() || y >= doc.height())
    return;
  const uint32_t target = doc.pixel(x, y);
  doc.setPixel(x, y, rgb);
  if (doc.pixel(x, y) == target)
    return; // Already that colour, or the format snapped it back

  std::vector<std::pair<int, int>> stack;
  stack.push_back({x, y});
  while (!stack.empty()) {
    const auto p = stack.back();
    stack.pop_back();
    const int px = p.first, py = p.second;
    const int neighbours[4][2] = {
        {px + 1, py}, {px - 1, py}, {px, py + 1}, {px, py - 1}};
    for (const auto &n : neighbours) {
      if (n[0] < 0 || n[1] < 0 || n[0] >= doc.width() || n[1] >= doc.height())
        continue;
      if (doc.pixel(n[0], n[1]) != target)
        continue;
      doc.setPixel(n[0], n[1], rgb);
      stack.push_back({n[0], n[1]});
    }
  }
}

/** @brief A point on a Bezier through 2, 3 or 4 control points. */
void bezierPoint(const std::vector<int> &xs, const std::vector<int> &ys,
                 float t, float *ox, float *oy) {
  const size_t n = xs.size();
  const float u = 1.0f - t;
  if (n == 2) {
    *ox = u * xs[0] + t * xs[1];
    *oy = u * ys[0] + t * ys[1];
  } else if (n == 3) {
    *ox = u * u * xs[0] + 2 * u * t * xs[1] + t * t * xs[2];
    *oy = u * u * ys[0] + 2 * u * t * ys[1] + t * t * ys[2];
  } else {
    *ox = u * u * u * xs[0] + 3 * u * u * t * xs[1] + 3 * u * t * t * xs[2] +
          t * t * t * xs[3];
    *oy = u * u * u * ys[0] + 3 * u * u * t * ys[1] + 3 * u * t * t * ys[2] +
          t * t * t * ys[3];
  }
}

/**
 * @brief Commit a Bezier curve as a stroke of the current brush width.
 *
 * Only the curve itself is painted. For a lid, draw the edge and then flood
 * fill the side that should be solid: the loader wants a filled band, but
 * deciding which side that is belongs to the person drawing, not to this.
 */
void applyBezier(ImageDocument &doc, const ImageEditorState &state,
                 uint32_t rgb) {
  if (state.bezierX.size() < 2)
    return;
  // Enough steps that no pixel along the curve is skipped even at full width.
  const int steps = std::max(doc.width(), doc.height()) * 4;
  float px = 0, py = 0;
  for (int i = 0; i <= steps; ++i) {
    float cx, cy;
    bezierPoint(state.bezierX, state.bezierY, (float)i / steps, &cx, &cy);
    if (i > 0)
      drawLine(doc, (int)lroundf(px), (int)lroundf(py), (int)lroundf(cx),
               (int)lroundf(cy), state.brush, rgb);
    px = cx;
    py = cy;
  }
}

// ---------------------------------------------------------------------------
//  CANVAS TEXTURE
// ---------------------------------------------------------------------------

/** @brief Keep the canvas texture matching the document, and refresh it. */
SDL_Texture *syncTexture(ImageDocument &doc, SDL_Renderer *renderer,
                         SDL_Texture **texture) {
  int tw = 0, th = 0;
  if (*texture) {
    float fw = 0, fh = 0;
    SDL_GetTextureSize(*texture, &fw, &fh);
    tw = (int)fw;
    th = (int)fh;
  }
  if (!*texture || tw != doc.width() || th != doc.height()) {
    if (*texture)
      SDL_DestroyTexture(*texture);
    *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                 SDL_TEXTUREACCESS_STREAMING, doc.width(),
                                 doc.height());
    if (*texture)
      SDL_SetTextureScaleMode(*texture, SDL_SCALEMODE_NEAREST);
  }
  if (*texture)
    SDL_UpdateTexture(*texture, nullptr, doc.rgbaForUpload().data(),
                      doc.width() * 4);
  return *texture;
}

// ---------------------------------------------------------------------------
//  PALETTE
// ---------------------------------------------------------------------------

/** @brief ImGui colour from 0x00RRGGBB. */
ImVec4 toVec4(uint32_t rgb) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f,
                (rgb & 0xFF) / 255.0f, 1.0f);
}

/** @brief 0x00RRGGBB from an ImGui colour. */
uint32_t fromFloats(const float *c) {
  auto q = [](float v) {
    const int i = (int)(v * 255.0f + 0.5f);
    return (uint32_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
  };
  return (q(c[0]) << 16) | (q(c[1]) << 8) | q(c[2]);
}

/** @brief The colours actually present, commonest first, up to @p limit. */
std::vector<uint32_t> imageColors(const ImageDocument &doc, size_t limit) {
  std::map<uint32_t, int> counts;
  // A coarse stride is plenty for building a swatch row and keeps this cheap
  // on a 512x128 texture.
  const int step = std::max(1, (doc.width() * doc.height()) / 4096);
  int seen = 0;
  for (int y = 0; y < doc.height(); ++y)
    for (int x = 0; x < doc.width(); ++x)
      if ((seen++ % step) == 0)
        counts[doc.pixel(x, y)]++;
  std::vector<std::pair<uint32_t, int>> v(counts.begin(), counts.end());
  std::sort(v.begin(), v.end(),
            [](const std::pair<uint32_t, int> &a,
               const std::pair<uint32_t, int> &b) { return a.second > b.second; });
  std::vector<uint32_t> out;
  for (size_t i = 0; i < v.size() && i < limit; ++i)
    out.push_back(v[i].first);
  return out;
}

const char *toolName(ImageTool t) {
  switch (t) {
  case ImageTool::Pencil: return "Pencil";
  case ImageTool::Eraser: return "Eraser";
  case ImageTool::Fill: return "Fill";
  case ImageTool::Eyedropper: return "Pick";
  case ImageTool::Line: return "Line";
  case ImageTool::Rect: return "Rect";
  case ImageTool::Bezier: return "Curve";
  }
  return "?";
}

} // namespace

ImageEditorResult drawImageEditor(ImageDocument &doc, ImageEditorState &state,
                                  const std::vector<std::string> &names,
                                  size_t current, SDL_Renderer *renderer,
                                  SDL_Texture **texture, float width,
                                  float height) {
  ImageEditorResult result;

  // Fill the host window exactly. No decoration of its own -- the OS window
  // already has a title bar and a close button, and drawing a second set
  // inside them is what makes it look like a window inside a window.
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  const bool visible = ImGui::Begin(
      "##imageeditor", nullptr,
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
          ImGuiWindowFlags_NoBringToFrontOnFocus |
          ImGuiWindowFlags_NoSavedSettings);
  if (!visible) {
    ImGui::End();
    ImGui::PopStyleVar(2);
    return result;
  }

  const bool oneBit = doc.kind() == ImageDocument::Indexed1;

  // -- Which image ---------------------------------------------------------
  ImGui::SetNextItemWidth(220);
  const char *label = current < names.size() ? names[current].c_str() : "";
  if (ImGui::BeginCombo("##image", label)) {
    for (size_t i = 0; i < names.size(); ++i) {
      const bool selected = (i == current);
      if (ImGui::Selectable(names[i].c_str(), selected))
        result.selectImage = (int)i;
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::Text("%dx%d %s%s", doc.width(), doc.height(),
              oneBit ? "1-bit" : "24-bit", doc.dirty() ? " (edited)" : "");
  ImGui::SameLine();
  if (ImGui::Button("Revert"))
    result.revertRequested = true;

  if (oneBit) {
    ImGui::PushTextWrapPos(0.0f);
    // Verified against the renderer rather than inferred: a band lowered
    // further down closes the lid further, monotonically.
    ImGui::TextDisabled(
        "Silhouette. Each column's topmost and bottommost lit pixel is all "
        "that is read: for an upper lid the top edge is where it sits fully "
        "open and the bottom edge fully shut, and the other way round for a "
        "lower lid. Everything between them is ignored.");
    ImGui::PopTextWrapPos();
  }
  else {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Polar. x is the angle around the eye, y is distance "
                        "out from the pupil; this is not a picture of an eye.");
    ImGui::PopTextWrapPos();
  }

  // -- Tools ---------------------------------------------------------------
  struct ToolButton {
    ImageTool tool; ///< Which tool
    const char *help; ///< What it does, on hover
  };
  // Every tool says what it is for. The eyelid ones need it most: nothing
  // about a toolbar hints that a lid has to be a filled band rather than a
  // drawn line.
  const ToolButton tools[] = {
      {ImageTool::Pencil, "Paint freehand at the brush size."},
      {ImageTool::Eraser, "Paint freehand in the secondary colour."},
      {ImageTool::Fill, "Flood fill the region of matching colour you click."},
      {ImageTool::Eyedropper, "Take a colour off the canvas to paint with."},
      {ImageTool::Line, "Drag for a straight line."},
      {ImageTool::Rect, "Drag for a rectangle outline."},
      {ImageTool::Bezier,
       oneBit ? "Click 2 to 4 points, drag any of them to reshape, then "
                "Apply to stroke the curve at the brush width. For a lid, draw "
                "its edge and then Fill the side that should be solid: the "
                "loader reads a filled band, and a bare line puts the open and "
                "shut positions on the same row, which pins the lid still."
              : "Click 2 to 4 points, drag any of them to reshape, then Apply "
                "to stroke the curve at the brush width."}};
  for (size_t i = 0; i < IM_ARRAYSIZE(tools); ++i) {
    if (i)
      ImGui::SameLine();
    const bool active = state.tool == tools[i].tool;
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(toolName(tools[i].tool))) {
      state.tool = tools[i].tool;
      state.bezierX.clear();
      state.bezierY.clear();
      state.bezierDrag = -1;
    }
    if (active)
      ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
      ImGui::TextUnformatted(tools[i].help);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  }

  ImGui::SetNextItemWidth(110);
  ImGui::SliderInt("brush", &state.brush, 1, 32);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  int zoomPercent = (int)(state.zoom * 100.0f);
  if (ImGui::SliderInt("zoom", &zoomPercent, 0, 1600, zoomPercent ? "%d%%" : "fit"))
    state.zoom = zoomPercent / 100.0f;
  ImGui::SameLine();
  ImGui::Checkbox("grid", &state.grid);
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc.canUndo());
  if (ImGui::Button("Undo") ||
      (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyCtrl)) {
    doc.undo();
    result.imageChanged = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!doc.canRedo());
  if (ImGui::Button("Redo") ||
      (ImGui::IsKeyPressed(ImGuiKey_Y) && ImGui::GetIO().KeyCtrl)) {
    doc.redo();
    result.imageChanged = true;
  }
  ImGui::EndDisabled();

  if (state.tool == ImageTool::Bezier) {
    {
      ImGui::Text("%zu/4 points", state.bezierX.size());
      ImGui::SameLine();
      ImGui::BeginDisabled(state.bezierX.size() < 2);
      if (ImGui::Button("Apply curve")) {
        doc.beginStroke();
        applyBezier(doc, state, state.color);
        doc.endStroke();
        state.bezierX.clear();
        state.bezierY.clear();
        state.bezierDrag = -1;
        result.imageChanged = true;
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Clear points")) {
        state.bezierX.clear();
        state.bezierY.clear();
        state.bezierDrag = -1;
      }
    }
  }

  ImGui::Separator();

  // -- Palette, down the right ---------------------------------------------
  const float paletteW = 210.0f;
  ImGui::BeginChild("canvas", ImVec2(-paletteW, 0), false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  float zoom = state.zoom;
  if (zoom <= 0.0f) {
    zoom = std::min(avail.x / doc.width(), avail.y / doc.height());
    if (zoom < 1.0f && zoom > 0.0f) {
      // Never below one screen pixel per image pixel; scroll instead.
    } else {
      zoom = floorf(std::max(1.0f, zoom));
    }
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(doc.width() * zoom, doc.height() * zoom);

  SDL_Texture *tex = syncTexture(doc, renderer, texture);
  if (tex)
    ImGui::Image((ImTextureID)(intptr_t)tex, size);
  else
    ImGui::Dummy(size);

  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const int ix = (int)floorf((mouse.x - origin.x) / zoom);
  const int iy = (int)floorf((mouse.y - origin.y) / zoom);
  const bool inside =
      ix >= 0 && iy >= 0 && ix < doc.width() && iy < doc.height();

  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (state.grid && zoom >= 4.0f) {
    const ImU32 col = IM_COL32(255, 255, 255, 40);
    for (int x = 0; x <= doc.width(); ++x)
      dl->AddLine(ImVec2(origin.x + x * zoom, origin.y),
                  ImVec2(origin.x + x * zoom, origin.y + size.y), col);
    for (int y = 0; y <= doc.height(); ++y)
      dl->AddLine(ImVec2(origin.x, origin.y + y * zoom),
                  ImVec2(origin.x + size.x, origin.y + y * zoom), col);
  }
  dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
              IM_COL32(255, 255, 255, 90));

  // -- Interaction ---------------------------------------------------------
  const uint32_t paint =
      (state.tool == ImageTool::Eraser) ? state.altColor : state.color;

  if (hovered && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    switch (state.tool) {
    case ImageTool::Pencil:
    case ImageTool::Eraser:
      doc.beginStroke();
      dab(doc, ix, iy, state.brush, paint);
      state.lastX = ix;
      state.lastY = iy;
      state.dragging = true;
      break;
    case ImageTool::Fill:
      doc.beginStroke();
      floodFill(doc, ix, iy, paint);
      doc.endStroke();
      result.imageChanged = true;
      break;
    case ImageTool::Eyedropper:
      state.color = doc.pixel(ix, iy);
      break;
    case ImageTool::Bezier: {
      // Grab a point that is already there before adding another, so the
      // curve can be adjusted rather than only placed. The radius is in
      // screen pixels, so it stays usable at any zoom.
      const float grab = 8.0f;
      int hit = -1;
      float best = grab * grab;
      for (size_t i = 0; i < state.bezierX.size(); ++i) {
        const float dx = origin.x + (state.bezierX[i] + 0.5f) * zoom - mouse.x;
        const float dy = origin.y + (state.bezierY[i] + 0.5f) * zoom - mouse.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= best) {
          best = d2;
          hit = (int)i;
        }
      }
      if (hit >= 0) {
        state.bezierDrag = hit;
      } else if (state.bezierX.size() < 4) {
        state.bezierX.push_back(ix);
        state.bezierY.push_back(iy);
        state.bezierDrag = (int)state.bezierX.size() - 1;
      }
      // With four already placed and none grabbed, nothing happens: silently
      // throwing the curve away to start another would lose the work.
      break;
    }
    default: // Line and Rect both drag
      state.dragging = true;
      state.dragX0 = state.dragX1 = ix;
      state.dragY0 = state.dragY1 = iy;
      break;
    }
  }

  if (state.bezierDrag >= 0) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if ((size_t)state.bezierDrag < state.bezierX.size()) {
        // Clamped rather than dropped, so a point dragged past the edge stays
        // grabbable instead of disappearing.
        state.bezierX[state.bezierDrag] =
            std::min(std::max(ix, 0), doc.width() - 1);
        state.bezierY[state.bezierDrag] =
            std::min(std::max(iy, 0), doc.height() - 1);
      }
    } else {
      state.bezierDrag = -1;
    }
  }

  if (state.dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    state.dragX1 = ix;
    state.dragY1 = iy;
    if (state.tool == ImageTool::Pencil || state.tool == ImageTool::Eraser) {
      if (inside) {
        drawLine(doc, state.lastX, state.lastY, ix, iy, state.brush, paint);
        state.lastX = ix;
        state.lastY = iy;
      }
    }
  }

  if (state.dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    switch (state.tool) {
    case ImageTool::Pencil:
    case ImageTool::Eraser:
      doc.endStroke();
      break;
    case ImageTool::Line:
      doc.beginStroke();
      drawLine(doc, state.dragX0, state.dragY0, state.dragX1, state.dragY1,
               state.brush, paint);
      doc.endStroke();
      break;
    case ImageTool::Rect:
      doc.beginStroke();
      drawRect(doc, state.dragX0, state.dragY0, state.dragX1, state.dragY1,
               state.brush, paint);
      doc.endStroke();
      break;
    default:
      break;
    }
    state.dragging = false;
    state.lastX = state.lastY = -1;
    result.imageChanged = true;
  }

  // Previews for the tools that commit on release.
  auto toScreen = [&](int x, int y) {
    return ImVec2(origin.x + (x + 0.5f) * zoom, origin.y + (y + 0.5f) * zoom);
  };
  if (state.dragging) {
    const ImU32 col = IM_COL32(255, 255, 0, 200);
    if (state.tool == ImageTool::Line)
      dl->AddLine(toScreen(state.dragX0, state.dragY0),
                  toScreen(state.dragX1, state.dragY1), col);
    else if (state.tool == ImageTool::Rect)
      dl->AddRect(toScreen(state.dragX0, state.dragY0),
                  toScreen(state.dragX1, state.dragY1), col);
  }
  if (!state.bezierX.empty()) {
    const ImU32 hullCol = IM_COL32(255, 255, 0, 70);
    const ImU32 curveCol = IM_COL32(255, 255, 0, 230);
    const ImU32 handleCol = IM_COL32(255, 200, 0, 255);

    // The hull, faint, so it is obvious which line is the curve and which is
    // merely the points that pull it. A cubic does not pass through its middle
    // two points, and without this that reads as the preview being wrong.
    for (size_t i = 1; i < state.bezierX.size(); ++i)
      dl->AddLine(toScreen(state.bezierX[i - 1], state.bezierY[i - 1]),
                  toScreen(state.bezierX[i], state.bezierY[i]), hullCol);

    if (state.bezierX.size() >= 2) {
      // Enough segments that the curve stays smooth however far it is zoomed.
      const int steps = 128;
      ImVec2 prev;
      for (int i = 0; i <= steps; ++i) {
        float cx, cy;
        bezierPoint(state.bezierX, state.bezierY, (float)i / steps, &cx, &cy);
        const ImVec2 p(origin.x + (cx + 0.5f) * zoom,
                       origin.y + (cy + 0.5f) * zoom);
        if (i)
          dl->AddLine(prev, p, curveCol, 2.0f);
        prev = p;
      }
    }

    // Rings rather than discs, so a handle does not hide the curve under it.
    for (size_t i = 0; i < state.bezierX.size(); ++i) {
      const ImVec2 c = toScreen(state.bezierX[i], state.bezierY[i]);
      const bool held = state.bezierDrag == (int)i;
      dl->AddCircleFilled(c, held ? 5.0f : 3.0f, handleCol);
      dl->AddCircle(c, held ? 9.0f : 7.0f, handleCol, 0, held ? 2.5f : 1.5f);
    }
  }

  ImGui::EndChild();

  // -- Palette -------------------------------------------------------------
  ImGui::SameLine();
  ImGui::BeginChild("palette", ImVec2(0, 0), false);
  ImGui::PushTextWrapPos(0.0f);

  if (oneBit) {
    ImGui::TextDisabled("Two colours, and no others.");
    const uint32_t lit = doc.paletteColor(true);
    const uint32_t dark = doc.paletteColor(false);
    auto swatch = [&](const char *name, uint32_t c) {
      const bool selected = state.color == c;
      const float side = 36.0f;
      // Distinct ids: a ColorButton and a Selectable sharing the visible name
      // would share an ImGui id and fight over it.
      char swatchId[32], labelId[32];
      snprintf(swatchId, sizeof(swatchId), "##sw_%s", name);
      snprintf(labelId, sizeof(labelId), "%s##lbl_%s", name, name);
      bool picked = ImGui::ColorButton(swatchId, toVec4(c),
                                       ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(side, side));
      ImGui::SameLine();
      // The name is a target too, so the whole row picks the colour rather
      // than only the small square.
      if (ImGui::Selectable(labelId, selected, 0, ImVec2(0, side)))
        picked = true;
      if (picked)
        state.color = c;
    };
    swatch("lit", lit);
    swatch("unlit", dark);
    state.altColor = (state.color == lit) ? dark : lit;
    ImGui::Separator();
    ImGui::TextDisabled("The eraser paints the other one.");
  } else {
    float c[3] = {((state.color >> 16) & 0xFF) / 255.0f,
                  ((state.color >> 8) & 0xFF) / 255.0f,
                  (state.color & 0xFF) / 255.0f};
    if (ImGui::ColorPicker3("##pick", c,
                            ImGuiColorEditFlags_NoSidePreview |
                                ImGuiColorEditFlags_NoSmallPreview |
                                ImGuiColorEditFlags_DisplayRGB))
      state.color = fromFloats(c);
    ImGui::TextDisabled("Shown as RGB565, which is all that survives.");

    ImGui::Separator();
    ImGui::TextDisabled("In this image");
    const std::vector<uint32_t> used = imageColors(doc, 24);
    const float swatch = 24.0f;
    const float step = swatch + ImGui::GetStyle().ItemSpacing.x;
    const int perRow =
        std::max(1, (int)(ImGui::GetContentRegionAvail().x / step));
    for (size_t i = 0; i < used.size(); ++i) {
      if (i && (int)(i % perRow))
        ImGui::SameLine();
      char id[32];
      snprintf(id, sizeof(id), "##c%zu", i);
      if (ImGui::ColorButton(id, toVec4(used[i]),
                             ImGuiColorEditFlags_NoTooltip,
                             ImVec2(swatch, swatch)))
        state.color = used[i];
    }
    ImGui::Separator();
    float a[3] = {((state.altColor >> 16) & 0xFF) / 255.0f,
                  ((state.altColor >> 8) & 0xFF) / 255.0f,
                  (state.altColor & 0xFF) / 255.0f};
    if (ImGui::ColorEdit3("eraser", a, ImGuiColorEditFlags_NoInputs))
      state.altColor = fromFloats(a);
  }

  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  if (hovered && inside)
    ImGui::SetTooltip("%d, %d", ix, iy);

  ImGui::End();
  ImGui::PopStyleVar(2);
  return result;
}
