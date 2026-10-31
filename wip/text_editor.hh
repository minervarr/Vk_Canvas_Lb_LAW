#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <cstddef>
#include "canvas.hh"
#include "font.hh"
#include "msdf.hh"
#include "text_buffer.hh"
#include "undo_redo.hh"
#include "input_handler.hh"
#include "font_weight.hh"

class VulkanState;

class TextEditor {
public:
    // Register one weight's font + msdf. Call for each available weight before draw().
    // msdf may be nullptr — that weight falls back to Regular or curve rendering.
    void initWeight(FontWeight weight, Font* font, MsdfFont* msdf);

    // Convenience: init Regular weight and set up VulkanState + dimensions.
    void init(VulkanState* vk, Font* font, MsdfFont* msdf, uint32_t w, uint32_t h);
    void resize(uint32_t w, uint32_t h);

    // Switch the active rendering weight; triggers a quad rebuild on next draw().
    void setFontWeight(FontWeight w);
    FontWeight fontWeight() const { return active_weight_; }

    float  line_height() const { return line_h_; }
    float  scroll_y()    const { return scroll_y_; }

    // Buffer access for host-app file I/O
    void        load(const std::string& content);
    std::string text()        const { return buf_.to_string(); }
    bool        is_modified() const { return buf_.dirty(); }
    void        clear_modified()    { buf_.clear_dirty(); }

    // Input — cp is a Latin-1 codepoint (0–255); values ≥ 128 are UTF-8 encoded.
    void insert_char (unsigned char cp);
    void dispatch_key(int keycode, int meta);

    bool handle_key_event  (AInputEvent* ev);
    bool handle_touch_event(AInputEvent* ev, bool& was_tap,
                            bool& want_open, bool& want_save);

    void draw(float time_sec, int inset_top, int inset_bottom);

private:
    VulkanState* vk_    = nullptr;
    uint32_t     w_     = 0, h_ = 0;
    float        line_h_  = 0.f;
    float        padding_ = 12.f;
    float        scroll_y_ = 0.f;
    float        scroll_x_ = 0.f;

    // Per-weight resources
    Font*     font_weights_[kFontWeightCount] = {};
    MsdfFont* msdf_weights_[kFontWeightCount] = {};
    FontWeight active_weight_ = FontWeight::Regular;

    // Convenience accessors for active weight (fall back to Regular if null)
    Font*     activeFont() const;
    MsdfFont* activeMsdf() const;

    TextBuffer   buf_;
    UndoRedo     undo_;
    InputHandler input_;

    // Per-weight MSDF glyph quads emitted in DOCUMENT space.
    std::vector<float> quads_per_weight_[kFontWeightCount];

    // Dirty tracking
    size_t last_gen_         = ~size_t(0);
    int    last_inset_top_   = -1;
    int    last_inset_bottom_= -1;
    bool   last_blink_       = false;

    // Scroll tracking: quads cover a band around emit_scroll_y_. When the user
    // scrolls outside the band, re-emit with the new centre.
    float  emit_scroll_y_  = -1e9f;
    float  emit_scroll_x_  = -1e9f;
    float  scroll_band_    = 0.f;   // set to 3× viewport_h in init()
    float  avg_char_w_     = 0.f;   // average printable-ASCII advance at kFontSize
    static constexpr int kScrollStepChars = 5;

    // Cached cursor X to avoid re-scanning on every frame
    size_t last_cursor_abs_ = ~size_t(0);
    float  last_cursor_x_   = 0.f;

    // Advance in document-space from the start of the cursor line to cursor_pos.
    float cursorDocX() const;

    // Move cursor to the document position closest to screen tap (sx, sy).
    void tapToCursor(float sx, float sy);

    // Emit quads for visible lines around the current viewport into quads_.
    void rebuildQuads(int inset_top, int inset_bottom, bool blink_now);
};
