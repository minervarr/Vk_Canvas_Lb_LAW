#include "canvas.hh"
#include "font.hh"
#include "glyphs.hh"
#include "msdf.hh"

#include <algorithm>

namespace {
// Layout of a curve record (20 floats); bounding box lives at [14..17].
constexpr size_t kCurveFloats = 20;
constexpr size_t kBBoxMinX = 14, kBBoxMinY = 15, kBBoxMaxX = 16, kBBoxMaxY = 17;
}  // namespace

Canvas::Canvas(std::vector<float>& out, uint32_t screenW, uint32_t screenH,
               const Font* font,
               float insetTop, float insetBottom, float insetLeft, float insetRight)
    : out_(out), screenW_(screenW), screenH_(screenH), font_(font),
      insetTop_(insetTop), insetBottom_(insetBottom),
      insetLeft_(insetLeft), insetRight_(insetRight),
      contentW_(float(screenW) - insetLeft - insetRight),
      contentH_(float(screenH) - insetTop - insetBottom) {}

float Canvas::textWidth(std::string_view str, float size) const {
    if (msdf_) return msdf_->textWidth(str, size);
    return font_ ? font_->stringWidth(str, size) : stringWidth(str, size);
}

void Canvas::clear(Color c) {
    float hw = float(screenW_) * 0.5f;
    float hh = float(screenH_) * 0.5f;
    emitFilledRect(out_, hw, hh, hw, hh, c.r, c.g, c.b, c.a);
}

void Canvas::rect(float x, float y, float w, float h, Color c, float radius) {
    size_t start = out_.size();
    emitFilledRect(out_,
                   x + w * 0.5f, y + h * 0.5f,
                   w * 0.5f,     h * 0.5f,
                   c.r, c.g, c.b, c.a, radius);
    clipFrom_(start);
}

void Canvas::setClip(float x, float y, float w, float h) {
    clipActive_ = true;
    clipX0_ = x; clipY0_ = y; clipX1_ = x + w; clipY1_ = y + h;
}

void Canvas::clearClip() { clipActive_ = false; }

void Canvas::clipFrom_(size_t startIdx) {
    if (!clipActive_) return;
    size_t write = startIdx;
    for (size_t r = startIdx; r + kCurveFloats <= out_.size(); r += kCurveFloats) {
        float minX = out_[r + kBBoxMinX], minY = out_[r + kBBoxMinY];
        float maxX = out_[r + kBBoxMaxX], maxY = out_[r + kBBoxMaxY];
        // Fully outside the clip rect — drop the record.
        if (maxX <= clipX0_ || minX >= clipX1_ ||
            maxY <= clipY0_ || minY >= clipY1_)
            continue;
        if (write != r)
            std::copy(out_.begin() + r, out_.begin() + r + kCurveFloats,
                      out_.begin() + write);
        // Clamp the tiling bounding box to the clip rect.
        out_[write + kBBoxMinX] = std::max(minX, clipX0_);
        out_[write + kBBoxMinY] = std::max(minY, clipY0_);
        out_[write + kBBoxMaxX] = std::min(maxX, clipX1_);
        out_[write + kBBoxMaxY] = std::min(maxY, clipY1_);
        write += kCurveFloats;
    }
    out_.resize(write);
}

void Canvas::emitText_(std::string_view str, float x, float baselineY, float size, Color c) {
    if (msdf_ && quads_) { emitTextMsdf_(str, x, baselineY, size, c); return; }
    size_t start = out_.size();
    if (font_)
        font_->emitString(out_, str, x, baselineY, size, c.r, c.g, c.b, c.a);
    else
        emitString(out_, str, x, baselineY, size, size * 0.07f, c.r, c.g, c.b, c.a);
    clipFrom_(start);
}

// MSDF text: lay out each glyph, clip its quad to the active clip rect (adjusting
// UVs proportionally so partly-visible glyphs at a scroll boundary don't bleed),
// and append two triangles to the quad buffer.
void Canvas::emitTextMsdf_(std::string_view str, float x, float baselineY, float size, Color c) {
    float pen = x;
    for (char s : str) {
        GlyphQuad q;
        pen = msdf_->layout((uint32_t)(unsigned char)s, pen, baselineY, size, q);
        if (!q.draw) continue;

        float x0 = q.x0, y0 = q.y0, x1 = q.x1, y1 = q.y1;
        float u0 = q.u0, v0 = q.v0, u1 = q.u1, v1 = q.v1;
        if (clipActive_) {
            if (x1 <= clipX0_ || x0 >= clipX1_ || y1 <= clipY0_ || y0 >= clipY1_)
                continue;  // fully outside
            float nx0 = std::max(x0, clipX0_), nx1 = std::min(x1, clipX1_);
            float ny0 = std::max(y0, clipY0_), ny1 = std::min(y1, clipY1_);
            // Re-map UVs to the clipped rectangle (linear across the quad).
            float fu0 = (nx0 - x0) / (x1 - x0), fu1 = (nx1 - x0) / (x1 - x0);
            float fv0 = (ny0 - y0) / (y1 - y0), fv1 = (ny1 - y0) / (y1 - y0);
            float ru0 = u0 + fu0 * (u1 - u0), ru1 = u0 + fu1 * (u1 - u0);
            float rv0 = v0 + fv0 * (v1 - v0), rv1 = v0 + fv1 * (v1 - v0);
            x0 = nx0; x1 = nx1; y0 = ny0; y1 = ny1;
            u0 = ru0; u1 = ru1; v0 = rv0; v1 = rv1;
        }
        auto vert = [&](float vx, float vy, float vu, float vv) {
            quads_->push_back(vx); quads_->push_back(vy);
            quads_->push_back(vu); quads_->push_back(vv);
            quads_->push_back(c.r); quads_->push_back(c.g);
            quads_->push_back(c.b); quads_->push_back(c.a);
        };
        vert(x0, y0, u0, v0); vert(x1, y0, u1, v0); vert(x1, y1, u1, v1);
        vert(x0, y0, u0, v0); vert(x1, y1, u1, v1); vert(x0, y1, u0, v1);
    }
}

void Canvas::text(std::string_view str, float x, float y, float size, Color c) {
    // y is top of text; baseline ≈ top + cap-height
    emitText_(str, x, y + size, size, c);
}

void Canvas::textRight(std::string_view str, float rightX, float y, float size, Color c) {
    float w = textWidth(str, size);
    emitText_(str, rightX - w, y + size, size, c);
}

void Canvas::textCentered(std::string_view str, float cx, float y, float size, Color c) {
    float w = textWidth(str, size);
    emitText_(str, cx - w * 0.5f, y + size, size, c);
}

void Canvas::button(float x, float y, float w, float h,
                    std::string_view label, Color bg, Color fg, float radius) {
    rect(x, y, w, h, bg, radius);
    float size = h * 0.32f;
    float tw   = textWidth(label, size);
    // Center label: horizontal center + vertical center (baseline offset = size)
    emitText_(label,
              x + (w - tw) * 0.5f,
              y + (h - size) * 0.5f + size,
              size, fg);
}
