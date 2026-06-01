#pragma once
#include <cstdint>
#include <vector>
#include <string_view>

struct Font;
class  MsdfFont;

struct Color { float r, g, b, a; };

namespace col {
  constexpr Color bg      = {0.05f, 0.05f, 0.07f, 1.0f};  // Deep dark background
  constexpr Color panel   = {0.11f, 0.11f, 0.14f, 1.0f};  // Elevated panel
  constexpr Color text    = {0.96f, 0.96f, 0.98f, 1.0f};  // Crisp white
  constexpr Color dim     = {0.45f, 0.45f, 0.50f, 1.0f};  // Dimmed text
  constexpr Color green   = {0.18f, 0.85f, 0.40f, 1.0f};  // Vibrant mint green
  constexpr Color red     = {0.95f, 0.25f, 0.35f, 1.0f};  // Punchy coral red
  constexpr Color yellow  = {1.00f, 0.72f, 0.15f, 1.0f};  // Bright amber
  constexpr Color btnIdle = {0.18f, 0.20f, 0.26f, 1.0f};  // Subtle indigo
  constexpr Color btnRec  = {0.95f, 0.20f, 0.30f, 1.0f};  // Bright recording red
  constexpr Color btnWait = {0.26f, 0.26f, 0.14f, 1.0f};
}

struct Rect {
  float x, y, w, h;
  bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
  }
};

class Canvas {
public:
  // out    : float curve buffer to append draw calls into
  // font   : OTF font for filled glyphs, or nullptr to use stroke fallback
  // insets : system-bar insets in pixels (top/bottom/left/right)
  Canvas(std::vector<float>& out, uint32_t screenW, uint32_t screenH,
         const Font* font,
         float insetTop, float insetBottom, float insetLeft, float insetRight);

  // Content-area geometry (screen minus insets)
  float w()      const { return contentW_; }
  float h()      const { return contentH_; }
  float left()   const { return insetLeft_; }
  float right()  const { return insetLeft_ + contentW_; }
  float top()    const { return insetTop_; }
  float bottom() const { return insetTop_ + contentH_; }
  float pad()    const { return contentW_ * 0.025f; }

  // Route text through an MSDF atlas instead of Bézier curves. Glyph quads are
  // appended to quadOut (8 floats/vert) for the MSDF pipeline to draw. When set,
  // text()/button labels and textWidth() use MSDF metrics. Pass nullptr to fall
  // back to the curve path.
  void useMsdf(const MsdfFont* font, std::vector<float>* quadOut) {
    msdf_ = font; quads_ = quadOut;
  }

  // Measure text width in pixels at the given cap-height size.
  float textWidth(std::string_view str, float size) const;

  // Fill the entire screen with color c.
  void clear(Color c);

  // Filled axis-aligned rectangle with optional rounded corners.
  void rect(float x, float y, float w, float h, Color c, float radius = 0.0f);

  // Draw text. x,y is the top-left corner of the text box.
  void text(std::string_view str, float x, float y, float size, Color c);

  // Draw text right-aligned: right edge of the text lands at rightX.
  void textRight(std::string_view str, float rightX, float y, float size, Color c);

  // Draw text horizontally centered at cx.
  void textCentered(std::string_view str, float cx, float y, float size, Color c);

  // Filled rounded button with a centered label.
  void button(float x, float y, float w, float h,
              std::string_view label, Color bg, Color fg, float radius = 0.0f);

  // Constrain subsequent draws to a rectangle: records fully outside are
  // dropped, and the bounding box the rasteriser tiles by is clamped to the
  // clip. Granularity is tile-level (~16px), so this is a bleed safety net,
  // not a pixel-perfect mask. clearClip() restores unclipped drawing.
  void setClip(float x, float y, float w, float h);
  void clearClip();

private:
  std::vector<float>& out_;
  uint32_t screenW_, screenH_;
  const Font* font_;
  const MsdfFont* msdf_ = nullptr;
  std::vector<float>* quads_ = nullptr;
  float insetTop_, insetBottom_, insetLeft_, insetRight_;
  float contentW_, contentH_;

  bool  clipActive_ = false;
  float clipX0_ = 0.0f, clipY0_ = 0.0f, clipX1_ = 0.0f, clipY1_ = 0.0f;

  // Clip records appended at/after startIdx against the active clip rect.
  void clipFrom_(size_t startIdx);

  // Internal: emit str at (x, baseline_y)
  void emitText_(std::string_view str, float x, float baselineY, float size, Color c);
  void emitTextMsdf_(std::string_view str, float x, float baselineY, float size, Color c);
};
