#pragma once
#include <cstdint>
#include <vector>
#include <string_view>
#include "texture.hh"

struct Font;
class  MsdfFont;
enum class FontStyle : uint8_t;   // defined in msdf.hh (Roman/Bold/Math/Italic)

struct Color { float r, g, b, a; };

namespace col {
  constexpr Color bg      = {0.0f, 0.0f, 0.0f, 1.0f};  // Pure black background
  constexpr Color panel   = {0.11f, 0.11f, 0.14f, 1.0f};  // Elevated panel
  constexpr Color text    = {0.96f, 0.96f, 0.98f, 1.0f};  // Crisp white
  constexpr Color dim     = {0.45f, 0.45f, 0.50f, 1.0f};  // Dimmed text
  constexpr Color green   = {0.18f, 0.85f, 0.40f, 1.0f};  // Vibrant mint green
  constexpr Color red     = {0.95f, 0.25f, 0.35f, 1.0f};  // Punchy coral red
  constexpr Color yellow  = {1.00f, 0.72f, 0.15f, 1.0f};  // Bright amber
  constexpr Color btnIdle = {0.18f, 0.20f, 0.26f, 1.0f};  // Subtle indigo
  constexpr Color btnRec  = {0.95f, 0.20f, 0.30f, 1.0f};  // Bright recording red
  constexpr Color btnWait = {0.26f, 0.26f, 0.14f, 1.0f};
  constexpr Color accent  = {0.30f, 0.55f, 0.95f, 1.0f};  // Selection / on-state blue
  constexpr Color track   = {0.22f, 0.22f, 0.28f, 1.0f};  // Slider track / inset
  constexpr Color thumb   = {0.82f, 0.84f, 0.90f, 1.0f};  // Slider thumb / knob
  constexpr Color panel2  = {0.15f, 0.15f, 0.19f, 1.0f};  // Secondary panel / row
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

  // Route Canvas::image() draws into `out` (appended to, not cleared — caller
  // clears once per frame alongside the curve buffer). Pass nullptr (the
  // default) to make image() a no-op, e.g. for hosts that never draw art.
  void useImages(std::vector<ImageDraw>* out) { images_ = out; }

  // Same, for imageFg() — foreground images (icons, buttons) that must
  // render ON TOP of vector/text UI, the opposite of image()'s background
  // layer (album art, sitting BEHIND UI chrome). Renderer::draw() composites
  // in the order: background images -> vector overlay -> foreground images.
  void useImagesFg(std::vector<ImageDraw>* out) { imagesFg_ = out; }

  // Measure text width in pixels at the given cap-height size.
  float textWidth(std::string_view str, float size) const;

  // Fill the entire screen with color c.
  void clear(Color c);

  // Filled axis-aligned rectangle with optional rounded corners.
  void rect(float x, float y, float w, float h, Color c, float radius = 0.0f);

  // Draw a textured quad (album art, icons) at screen rect (x,y,w,h) sampling
  // the UV sub-rect [u0,v0]-[u1,v1] (default: whole texture). Requires
  // useImages() to have been called with a non-null output vector, and `tex`
  // to have come from Renderer::create_texture(); a no-op otherwise. Honors
  // the active clip rect (setClip); does not honor setRotation (art/icons in
  // this engine are never drawn rotated) — screen-space only.
  void image(TextureHandle tex, float x, float y, float w, float h,
             float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

  // Same as image(), but drawn in the foreground layer (see useImagesFg()).
  void imageFg(TextureHandle tex, float x, float y, float w, float h,
               float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f);

  // Draw text. x,y is the top-left corner of the text box.
  void text(std::string_view str, float x, float y, float size, Color c);

  // Draw text in a named style (Roman/Bold/Italic/Math) via the MSDF style map.
  // x,y is the top-left corner. Codepoints the style lacks fall back to the
  // default face. Roman delegates to text(). Requires an MSDF font.
  void textStyled(std::string_view str, float x, float y, float size, Color c, FontStyle style);
  // Width of `str` in `style` at `size` (matches textStyled's advance).
  float textWidthStyled(std::string_view str, float size, FontStyle style) const;

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
  // Draw a fast solid rectangular quad using MSDF (bypassing curve generation)
  void quadMsdfRect(float x, float y, float w, float h, Color c);

  // Antialiased line segment of the given pixel thickness, drawn as an SDF
  // capsule (curve record type 3). Reusable for plot curves, grids, axes, trace
  // cursors and underlines. Respects the active clip and rotation.
  void segment(float x0, float y0, float x1, float y1, float thickness, Color c);

  // Antialiased polyline through `count` screen points (xy = x0,y0,x1,y1,...).
  // Zero-length segments are skipped; callers break a curve by issuing separate
  // polyline() calls around invalid samples (asymptotes / NaN).
  void polyline(const float* xy, int count, float thickness, Color c);

  // The active MSDF font (for 2D math layout that needs OpenType MATH metrics,
  // constants and stretchy constructions). nullptr if text isn't MSDF-routed.
  const MsdfFont* msdfFont() const { return msdf_; }

  // Draw one math glyph addressed by key = (fontId<<24)|gid at pen (penX,
  // baselineY), honoring the active clip + rotation. For math-italic atoms and
  // stretched radical/delimiter assembly parts emitted by mathlayout.
  void mathGlyph(uint32_t key, float penX, float baselineY, float size, Color c);

  void setClip(float x, float y, float w, float h);
  void clearClip();

  // Cull every text glyph already emitted whose center lies inside `rect`, an
  // opaque occluder. All MSDF text composites in a single pass *after* all SDF
  // geometry, so draw order alone can't make a later panel hide earlier text —
  // call occlude(panelRect) right before drawing a modal/overlay and the text
  // beneath it can never bleed through. Generic; any host modal stays a clean
  // layer with one call (no per-screen visibility hacks).
  void occlude(float x, float y, float w, float h);

  // Rotate all subsequently-emitted overlay geometry by `radians` (clockwise in
  // screen space, y-down) about the pivot (pivotX, pivotY), until clearRotation().
  // Intended for rotating a whole UI overlay group (e.g. to follow device
  // orientation) while the underlying preview stays fixed. Applies to the MSDF
  // text/quad path (text/button labels via useMsdf, and quadMsdfRect).
  void setRotation(float radians, float pivotX, float pivotY);
  void clearRotation();

private:
  std::vector<float>& out_;
  uint32_t screenW_, screenH_;
  const Font* font_;
  const MsdfFont* msdf_ = nullptr;
  std::vector<float>* quads_ = nullptr;
  std::vector<ImageDraw>* images_ = nullptr;
  std::vector<ImageDraw>* imagesFg_ = nullptr;
  float insetTop_, insetBottom_, insetLeft_, insetRight_;
  float contentW_, contentH_;

  bool  clipActive_ = false;
  float clipX0_ = 0.0f, clipY0_ = 0.0f, clipX1_ = 0.0f, clipY1_ = 0.0f;

  bool  rotActive_ = false;
  float rotCos_ = 1.0f, rotSin_ = 0.0f, rotPx_ = 0.0f, rotPy_ = 0.0f;
  // Rotate (x,y) about the pivot in place when a rotation is active.
  void  xform_(float& x, float& y) const;
  // Rotate the point-based curve records (types 3/4/5/6) appended at/after
  // startIdx and recompute their bounding boxes. Used for the curve text path.
  void  rotateRecordsFrom_(size_t startIdx);
  // Emit a filled rectangle as a type-3 capsule (SDF segment + radius): a
  // rotatable, isotropic substitute for the axis-aligned rounded-box SDF box.
  // Lives in the SDF pass (not the winding pass), so winding text composites
  // cleanly on top without colour interference.
  void  emitRotatableRect_(float x, float y, float w, float h, Color c);

  // Emit one SDF capsule (type-3) between two points with the given radius,
  // applying the active rotation and computing its bounding box. Shared by
  // segment()/polyline().
  void  emitCapsule_(float ax, float ay, float bx, float by, float radius, Color c);

  // Clip records appended at/after startIdx against the active clip rect.
  void clipFrom_(size_t startIdx);

  // Internal: emit str at (x, baseline_y)
  void emitText_(std::string_view str, float x, float baselineY, float size, Color c);
  void emitTextMsdf_(std::string_view str, float x, float baselineY, float size, Color c);
};
