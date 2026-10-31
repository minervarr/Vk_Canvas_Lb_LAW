#include "text_editor.hh"
#include "vulkan_state.hh"
#include <android/keycodes.h>
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <string>

#define TAG "TextEditor"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)

static constexpr float kFontSize    = 28.f;
static constexpr Color kTextColor   = {0.90f, 0.90f, 0.85f, 1.f};
static constexpr Color kCursorColor = {0.90f, 0.70f, 0.20f, 1.f};

// ── Lifecycle ───────────────────────────────────────────────────────────────

Font* TextEditor::activeFont() const {
    Font* f = font_weights_[(int)active_weight_];
    return f ? f : font_weights_[(int)FontWeight::Regular];
}

MsdfFont* TextEditor::activeMsdf() const {
    MsdfFont* m = msdf_weights_[(int)active_weight_];
    return m ? m : msdf_weights_[(int)FontWeight::Regular];
}

void TextEditor::initWeight(FontWeight weight, Font* font, MsdfFont* msdf) {
    font_weights_[(int)weight] = font;
    msdf_weights_[(int)weight] = msdf;
}

void TextEditor::setFontWeight(FontWeight w) {
    if (active_weight_ == w) return;
    active_weight_ = w;
    last_gen_ = ~size_t(0);  // force rebuild
}

static float computeAvgCharW(MsdfFont* msdf, Font* font) {
    float sum = 0.f;
    for (uint32_t cp = 32; cp < 127; cp++)
        sum += msdf ? msdf->advance(cp, kFontSize)
                    : (font ? font->glyphAdvance(cp, kFontSize) : kFontSize * 0.55f);
    return sum / 95.f;  // 127 - 32 = 95 printable ASCII chars
}

void TextEditor::init(VulkanState* vk, Font* font, MsdfFont* msdf,
                      uint32_t w, uint32_t h) {
    vk_ = vk;
    initWeight(FontWeight::Regular, font, msdf);
    w_ = w; h_ = h;
    MsdfFont* m = activeMsdf();
    Font*     f = activeFont();
    line_h_      = (m && m->valid()) ? m->lineHeight(kFontSize) : kFontSize * 1.4f;
    avg_char_w_  = computeAvgCharW(m && m->valid() ? m : nullptr, f);
    scroll_band_ = line_h_ * 15.f;
    input_.init((float)h, line_h_);
    LOGI("TextEditor init %ux%u line_h=%.1f avg_char=%.1f msdf=%s",
         w, h, line_h_, avg_char_w_, m && m->valid() ? "yes" : "no");
}

void TextEditor::resize(uint32_t w, uint32_t h) {
    w_ = w; h_ = h;
    scroll_band_ = line_h_ * 15.f;
    input_.init((float)h, line_h_);
    last_gen_ = ~size_t(0);     // force full rebuild
}

void TextEditor::load(const std::string& content) {
    buf_ = TextBuffer();
    buf_.insert(content);
    buf_.move_cursor_to(0);
    buf_.clear_dirty();
    undo_.clear();
    scroll_y_ = scroll_x_ = 0.f;
    emit_scroll_y_ = emit_scroll_x_ = -1e9f;
    last_gen_ = ~size_t(0);
}

// ── Input ────────────────────────────────────────────────────────────────────

void TextEditor::insert_char(unsigned char cp) {
    if (cp < 0x80) {
        char c = (char)cp;
        size_t pos = buf_.cursor_pos();
        if (!undo_.try_coalesce_insert(pos, c))
            undo_.record_insert(pos, std::string(1, c));
        buf_.insert(c);
    } else {
        // Latin-1 supplement → 2-byte UTF-8
        char u[2] = { (char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F)) };
        std::string s(u, 2);
        size_t pos = buf_.cursor_pos();
        undo_.record_insert(pos, s);
        buf_.insert(s);
    }
}

void TextEditor::dispatch_key(int keycode, int meta) {
    switch (keycode) {
        case AKEYCODE_DEL: {
            size_t pos = buf_.cursor_pos();
            if (pos == 0) break;
            // Walk back over a full UTF-8 sequence
            size_t del_start = pos - 1;
            while (del_start > 0 && ((unsigned char)buf_.at(del_start) & 0xC0) == 0x80)
                --del_start;
            std::string erased;
            for (size_t i = del_start; i < pos; ++i) erased += buf_.at(i);
            undo_.record_erase(del_start, erased);
            for (size_t i = del_start; i < pos; ++i) buf_.erase_before();
        } break;
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER: {
            size_t pos = buf_.cursor_pos();
            if (!undo_.try_coalesce_insert(pos, '\n'))
                undo_.record_insert(pos, "\n");
            buf_.insert('\n');
        } break;
        case AKEYCODE_DPAD_LEFT:  buf_.move_cursor(-1); break;
        case AKEYCODE_DPAD_RIGHT: buf_.move_cursor(1);  break;
        case AKEYCODE_DPAD_UP: {
            int l = buf_.cursor_line() - 1;
            if (l >= 0) buf_.move_to_line_col(l, buf_.cursor_col());
        } break;
        case AKEYCODE_DPAD_DOWN: {
            int l = buf_.cursor_line() + 1;
            if (l < buf_.line_count()) buf_.move_to_line_col(l, buf_.cursor_col());
        } break;
        default: break;
    }
}

bool TextEditor::handle_key_event(AInputEvent* ev) {
    return input_.handle_key(ev, buf_, undo_);
}

// Place the cursor at the document position closest to screen point (sx, sy).
void TextEditor::tapToCursor(float sx, float sy) {
    // Convert screen Y → document line (round to nearest, accounting for inset)
    float doc_y = sy + scroll_y_ - ((float)last_inset_top_ + padding_);
    int line = static_cast<int>(doc_y / line_h_ + 0.5f);
    line = std::max(0, std::min(line, buf_.line_count() - 1));

    // Convert screen X → byte column by scanning glyph advances
    float target_x = sx - padding_ + scroll_x_;   // screen → document X
    MsdfFont* msdf = activeMsdf();
    Font*     font = activeFont();

    size_t ls = buf_.line_start(line);
    size_t le = buf_.line_end(line);
    float  x  = 0.f;
    int    col = 0;
    for (size_t i = ls; i < le; ) {
        unsigned char b = (unsigned char)buf_.at(i);
        uint32_t cp;
        int n = 1;
        if      (b < 0x80)                                     { cp = b; n = 1; }
        else if ((b & 0xE0) == 0xC0 && i + 1 < le)            { cp = ((b & 0x1F) << 6) | ((unsigned char)buf_.at(i + 1) & 0x3F); n = 2; }
        else                                                   { cp = '?'; n = 1; }
        if (cp == '\n') break;
        float gw = msdf ? msdf->advance(cp, kFontSize)
                        : (font ? font->glyphAdvance(cp, kFontSize) : kFontSize * 0.6f);
        if (x + gw * 0.5f > target_x) break;  // tap is in the left half of this glyph
        x   += gw;
        col += n;
        i   += n;
    }
    buf_.move_to_line_col(line, col);
    last_cursor_abs_ = ~size_t(0);  // invalidate cursor X cache
}

bool TextEditor::handle_touch_event(AInputEvent* ev, bool& was_tap,
                                     bool& want_open, bool& want_save) {
    bool consumed = input_.handle_touch(ev, buf_, scroll_y_, want_open, want_save, was_tap);
    if (was_tap)
        tapToCursor(input_.last_tap_x(), input_.last_tap_y());
    return consumed;
}

// ── Cursor measurement ────────────────────────────────────────────────────────

float TextEditor::cursorDocX() const {
    size_t cursor_abs = buf_.cursor_pos();
    if (cursor_abs == last_cursor_abs_) return last_cursor_x_;

    int cl     = const_cast<TextBuffer&>(buf_).cursor_line();
    size_t ls  = const_cast<TextBuffer&>(buf_).line_start(cl);
    float  x   = 0.f;
    size_t i   = ls;
    MsdfFont* msdf = activeMsdf();
    Font*     font = activeFont();
    while (i < cursor_abs && i < buf_.length()) {
        unsigned char b = (unsigned char)buf_.at(i);
        uint32_t cp;
        if (b < 0x80)                                          { cp = b; ++i; }
        else if ((b & 0xE0) == 0xC0 && i+1 < buf_.length())   { cp = ((b&0x1F)<<6)|((unsigned char)buf_.at(i+1)&0x3F); i+=2; }
        else                                                   { cp = '?'; ++i; }
        if (cp == '\n') break;
        x += msdf ? msdf->advance(cp, kFontSize)
                  : font->glyphAdvance(cp, kFontSize);
    }
    // Cache — cast away const because cursor position is logically const here
    const_cast<TextEditor*>(this)->last_cursor_abs_ = cursor_abs;
    const_cast<TextEditor*>(this)->last_cursor_x_   = x;
    return x;
}

// ── Quad emission ─────────────────────────────────────────────────────────────

void TextEditor::rebuildQuads(int inset_top, int inset_bottom, bool blink_now) {
    int wi = (int)active_weight_;
    quads_per_weight_[wi].clear();
    std::vector<float>& quads_ = quads_per_weight_[wi];

    MsdfFont* msdf_ = activeMsdf();
    Font*     font_ = activeFont();
    bool use_msdf = msdf_ && msdf_->valid() && vk_->msdfReady(wi);

    // Document-space base: top = inset_top + padding_, x = padding_ (NO scroll)
    float x_base    = padding_;
    float y_base    = (float)inset_top + padding_;
    float viewport_h = (float)((int)h_ - inset_top - inset_bottom);

    // Cull band: emit lines within [scroll_y_ - band, scroll_y_ + viewport_h + band]
    float cull_top = emit_scroll_y_ - scroll_band_;
    float cull_bot = emit_scroll_y_ + viewport_h + scroll_band_;

    int lines = buf_.line_count();

    if (use_msdf) {
        // MSDF path: emit textured quads directly into quads_ (no Canvas overhead)
        for (int li = 0; li < lines; ++li) {
            float y_top = y_base + li * line_h_;
            if (y_top + line_h_ < cull_top) continue;
            if (y_top > cull_bot)            break;

            // Use zero-copy string_view when the line is contiguous in the gap buffer
            std::string_view sv = buf_.line_view(li);
            std::string line_copy;
            if (sv.empty()) {
                // Line spans the gap (cursor line) — build a copy
                size_t start = buf_.line_start(li);
                size_t end   = buf_.line_end(li);
                line_copy.reserve(end - start);
                for (size_t ci = start; ci < end; ++ci) {
                    char c = buf_.at(ci);
                    if (c == '\n') break;
                    line_copy += c;
                }
                sv = line_copy;
            }

            if (!sv.empty()) {
                float pen = x_base;
                float baseline = y_top + kFontSize;   // baseline = top + cap-height
                for (size_t si = 0; si < sv.size(); ) {
                    unsigned char b = (unsigned char)sv[si];
                    uint32_t cp;
                    if (b < 0x80)                                          { cp = b; ++si; }
                    else if ((b & 0xE0) == 0xC0 && si+1 < sv.size())      { cp = ((b&0x1F)<<6)|((unsigned char)sv[si+1]&0x3F); si+=2; }
                    else                                                   { cp = '?'; ++si; }
                    pen = msdf_->emitGlyph(quads_, cp, pen, baseline, kFontSize,
                                           kTextColor.r, kTextColor.g, kTextColor.b, kTextColor.a);
                }
            }
        }

        // Cursor rect via quadMsdfRect approximation: emit 2 triangles with solid color
        if (blink_now) {
            int   cl  = buf_.cursor_line();
            float cx  = x_base + cursorDocX();
            float cy  = y_base + cl * line_h_;
            float cw  = 2.f;
            float ch  = line_h_;
            // Use a point UV from the center of a known solid glyph ('|' works well)
            GlyphQuad q;
            msdf_->layout('|', 0.f, 0.f, kFontSize, q);
            float uc = (q.u0 + q.u1) * 0.5f;
            float vc = (q.v0 + q.v1) * 0.5f;
            auto emit = [&](float vx, float vy) {
                quads_.push_back(vx); quads_.push_back(vy);
                quads_.push_back(uc); quads_.push_back(vc);
                quads_.push_back(kCursorColor.r); quads_.push_back(kCursorColor.g);
                quads_.push_back(kCursorColor.b); quads_.push_back(kCursorColor.a);
            };
            emit(cx,    cy);    emit(cx+cw, cy);    emit(cx,    cy+ch);
            emit(cx+cw, cy);    emit(cx+cw, cy+ch); emit(cx,    cy+ch);
        }
    } else {
        // Curve/compute fallback (no MSDF atlas available)
        Canvas canvas(quads_, w_, h_, font_,
                      (float)inset_top, (float)inset_bottom, 0.f, 0.f);
        for (int li = 0; li < lines; ++li) {
            float y = y_base + li * line_h_;
            if (y + line_h_ < cull_top) continue;
            if (y > cull_bot)           break;

            std::string_view sv = buf_.line_view(li);
            std::string line_copy;
            if (sv.empty()) {
                size_t start = buf_.line_start(li); size_t end = buf_.line_end(li);
                line_copy.reserve(end - start);
                for (size_t ci = start; ci < end; ++ci) {
                    char c = buf_.at(ci); if (c == '\n') break; line_copy += c;
                }
                sv = line_copy;
            }
            if (!sv.empty())
                canvas.text(sv, x_base, y, kFontSize, kTextColor);
        }
        if (blink_now) {
            float cx = x_base + cursorDocX();
            float cy = y_base + buf_.cursor_line() * line_h_;
            canvas.rect(cx, cy, 2.f, line_h_, kCursorColor);
        }
    }
}

// ── Main draw ────────────────────────────────────────────────────────────────

void TextEditor::draw(float time_sec, int inset_top, int inset_bottom) {
    float viewport_h = (float)((int)h_ - inset_top - inset_bottom);

    // ── Vertical auto-scroll ─────────────────────────────────────────────────
    if (line_h_ > 0.f) {
        float cursor_top = padding_ + buf_.cursor_line() * line_h_;
        float margin     = line_h_ * 2.f;
        if (cursor_top + line_h_ - scroll_y_ > viewport_h - margin)
            scroll_y_ = cursor_top + line_h_ - viewport_h + margin;
        if (cursor_top - scroll_y_ < margin)
            scroll_y_ = cursor_top - margin;
        if (scroll_y_ < 0.f) scroll_y_ = 0.f;
    }

    // ── Horizontal step-scroll ───────────────────────────────────────────────
    if (avg_char_w_ > 0.f) {
        float content_w = (float)(int)w_ - padding_ * 2.f;
        float step      = avg_char_w_ * (float)kScrollStepChars;
        float cx        = cursorDocX();
        if (cx - scroll_x_ >= content_w - avg_char_w_)
            scroll_x_ = cx - content_w + step + avg_char_w_;
        if (cx - scroll_x_ < 0.f)
            scroll_x_ = std::max(0.f, cx - step);
        if (scroll_x_ < 0.f) scroll_x_ = 0.f;
    }

    // ── Blink ─────────────────────────────────────────────────────────────────
    bool blink_now = sinf(time_sec * 3.14159f * 2.f) > -0.2f;

    // ── Dirty check ──────────────────────────────────────────────────────────
    bool scroll_out_of_band = fabsf(scroll_y_ - emit_scroll_y_) > scroll_band_
                           || fabsf(scroll_x_ - emit_scroll_x_) > scroll_band_;
    bool content_changed = (buf_.generation() != last_gen_)    ||
                           (blink_now         != last_blink_)   ||
                           (inset_top         != last_inset_top_) ||
                           (inset_bottom      != last_inset_bottom_) ||
                           scroll_out_of_band;

    if (content_changed) {
        emit_scroll_y_    = scroll_y_;
        emit_scroll_x_    = scroll_x_;
        last_gen_         = buf_.generation();
        last_blink_       = blink_now;
        last_inset_top_   = inset_top;
        last_inset_bottom_= inset_bottom;
        rebuildQuads(inset_top, inset_bottom, blink_now);
    }

    uint32_t quadVerts[kFontWeightCount] = {};
    for (int w = 0; w < kFontWeightCount; w++)
        quadVerts[w] = (uint32_t)(quads_per_weight_[w].size() / 8u);
    // The MSDF vertex shader ADDS the scroll offset to each vertex. A positive
    // scroll_x_ means "we have scrolled right into the document", so the content
    // must shift LEFT on screen to reveal the text near the cursor — hence the
    // GPU X offset is negated. (tapToCursor inverts this with +scroll_x_.)
    vk_->draw(quads_per_weight_, quadVerts, content_changed, -scroll_x_, scroll_y_);
}
