#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "canvas.hh"

struct Font;

// Reusable scrollable, word-wrapped text view for the Vulkan canvas engine.
//
// Immediate-mode friendly: set the rect + text whenever they change, then call
// render() every frame. Scrolling is driven by the host platform feeding drag
// deltas to scrollBy() (the engine has no input system of its own). render()
// re-wraps against the current width and clamps the scroll offset to the
// content, so callers never have to track content height themselves.
//
// Editing (caret / selection) is intentionally not part of this first cut; the
// layout + scroll core here is meant to be the foundation it builds on.
class TextArea {
 public:
  void setRect(float x, float y, float w, float h);
  void setText(std::string s);                 // replaces content (keeps scroll)
  const std::string& text() const { return text_; }

  void setTextSize(float px) { textSize_ = px; }   // 0 => auto from rect height
  void setColors(Color fg, Color bg) { fg_ = fg; bg_ = bg; }
  void setPadding(float p) { pad_ = p; }           // <0 => derive from Canvas

  bool contains(float px, float py) const { return rect_.contains(px, py); }
  bool overflows() const { return lastMaxScroll_ > 0.5f; }

  void scrollBy(float dyPixels);   // +dy reveals later text (content moves up)
  void scrollToBottom();           // pin to end (call when appending live text)
  void scrollToTop();

  // Lays out + draws (background, text, and a scrollbar thumb when overflowing).
  void render(Canvas& c, const Font* font);

  // Static emission for GPU-scrolled text: draws the background and emits ALL
  // wrapped lines at their natural positions (no clip, no scroll). The host
  // scrolls by offsetting the vertices on the GPU and clips with a scissor.
  // Use rect()/contentHeight()/viewHeight() to drive the scroll offset.
  void emitStatic(Canvas& c, const Font* font);
  Rect  rect() const { return rect_; }
  float contentHeight() const { return contentH_; }
  float viewHeight() const { return rect_.h - 2.0f * lastPad_; }

 private:
  void relayout_(const Canvas& c, float size, float maxW);

  Rect        rect_{0, 0, 0, 0};
  std::string text_;
  float scroll_         = 0.0f;    // px of content scrolled above the top edge
  float textSize_       = 0.0f;    // 0 => auto
  float pad_            = -1.0f;   // <0 => Canvas::pad()
  Color fg_             = col::text;
  Color bg_             = col::panel;
  bool  pinBottom_      = false;
  float lastMaxScroll_  = 0.0f;
  float contentH_       = 0.0f;
  float lastPad_        = 0.0f;

  // Cached word-wrap layout. Re-wrapping is O(words), not O(chars), and only
  // happens when the text, wrap width, or font size actually changes — so
  // scrolling (which only moves the offset) never re-wraps.
  std::vector<std::string_view> lines_;
  bool  layoutDirty_ = true;
  float layoutW_     = -1.0f;
  float layoutSize_  = -1.0f;
};
