#include "canvas.hh"
#include "font.hh"
#include "glyphs.hh"
#include "msdf.hh"
#include "utf8.hh"

#include <algorithm>
#include <cmath>

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
    if (shapes_) { rect(0.0f, 0.0f, float(screenW_), float(screenH_), c, 0.0f); return; }
    float hw = float(screenW_) * 0.5f;
    float hh = float(screenH_) * 0.5f;
    emitFilledRect(out_, hw, hh, hw, hh, c.r, c.g, c.b, c.a);
}

void Canvas::emitShapeQuad_(float x0, float y0, float x1, float y1,
                            float kind, const float d[6], Color c) {
    // 1px AA margin so the smoothed edge is never cut by the quad itself.
    x0 -= 1.0f; y0 -= 1.0f; x1 += 1.0f; y1 += 1.0f;
    if (clipActive_) {
        // Clamp the quad; the SDF is evaluated in absolute pixel coords, so
        // this clips the shape pixel-exactly (better than the curve path's
        // tile-granular clip).
        x0 = std::max(x0, clipX0_); x1 = std::min(x1, clipX1_);
        y0 = std::max(y0, clipY0_); y1 = std::min(y1, clipY1_);
        if (x0 >= x1 || y0 >= y1) return;
    }
    auto vert = [&](float vx, float vy) {
        float rec[14] = { vx, vy, c.r, c.g, c.b, c.a,
                          kind, d[0], d[1], d[2], d[3], d[4], d[5], 0.0f };
        shapes_->insert(shapes_->end(), rec, rec + 14);
    };
    vert(x0, y0); vert(x1, y0); vert(x1, y1);
    vert(x0, y0); vert(x1, y1); vert(x0, y1);
}

void Canvas::rect(float x, float y, float w, float h, Color c, float radius) {
    if (shapes_ && !rotActive_) {
        float d[6] = { x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, radius, 0.0f };
        emitShapeQuad_(x, y, x + w, y + h, 0.0f, d, c);
        return;
    }
    size_t start = out_.size();
    if (rotActive_) {
        // Rounded-box SDF can't be rotated; emit a rotatable capsule instead.
        emitRotatableRect_(x, y, w, h, c);
    } else {
        emitFilledRect(out_,
                       x + w * 0.5f, y + h * 0.5f,
                       w * 0.5f,     h * 0.5f,
                       c.r, c.g, c.b, c.a, radius);
    }
    clipFrom_(start);
}

void Canvas::image(TextureHandle tex, float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1) {
  if (!images_ || tex == kInvalidTexture) return;
  ImageDraw d;
  d.tex = tex;
  d.x = x; d.y = y; d.w = w; d.h = h;
  d.u0 = u0; d.v0 = v0; d.u1 = u1; d.v1 = v1;
  if (clipActive_) {
    d.hasClip = true;
    d.clipX = clipX0_; d.clipY = clipY0_;
    d.clipW = clipX1_ - clipX0_; d.clipH = clipY1_ - clipY0_;
  }
  images_->push_back(d);
}

void Canvas::imageFg(TextureHandle tex, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1) {
  if (!imagesFg_ || tex == kInvalidTexture) return;
  ImageDraw d;
  d.tex = tex;
  d.x = x; d.y = y; d.w = w; d.h = h;
  d.u0 = u0; d.v0 = v0; d.u1 = u1; d.v1 = v1;
  if (clipActive_) {
    d.hasClip = true;
    d.clipX = clipX0_; d.clipY = clipY0_;
    d.clipW = clipX1_ - clipX0_; d.clipH = clipY1_ - clipY0_;
  }
  imagesFg_->push_back(d);
}

void Canvas::triangle(float x0, float y0, float x1, float y1,
                      float x2, float y2, Color c) {
    if (shapes_ && !rotActive_) {
        float d[6] = { x0, y0, x1, y1, x2, y2 };
        emitShapeQuad_(std::min({x0, x1, x2}), std::min({y0, y1, y2}),
                       std::max({x0, x1, x2}), std::max({y0, y1, y2}),
                       2.0f, d, c);
        return;
    }
    size_t start = out_.size();
    auto edge = [&](float ax, float ay, float bx, float by) {
        xform_(ax, ay);
        xform_(bx, by);
        float rec[kCurveFloats] = {0};
        rec[0] = 6.0f;  // line segment, winding-fill pass (same as glyph edges)
        rec[1] = ax; rec[2] = ay;
        rec[3] = bx; rec[4] = by;
        rec[9] = c.r; rec[10] = c.g; rec[11] = c.b; rec[12] = c.a;
        rec[kBBoxMinX] = std::min(ax, bx); rec[kBBoxMinY] = std::min(ay, by);
        rec[kBBoxMaxX] = std::max(ax, bx); rec[kBBoxMaxY] = std::max(ay, by);
        out_.insert(out_.end(), rec, rec + kCurveFloats);
    };
    edge(x0, y0, x1, y1);
    edge(x1, y1, x2, y2);
    edge(x2, y2, x0, y0);
    clipFrom_(start);
}

void Canvas::emitCapsule_(float ax, float ay, float bx, float by, float r, Color c) {
    xform_(ax, ay);
    xform_(bx, by);
    float minX = std::min(ax, bx) - r, maxX = std::max(ax, bx) + r;
    float minY = std::min(ay, by) - r, maxY = std::max(ay, by) + r;

    float rec[kCurveFloats] = {0};
    rec[0] = 3.0f;                       // SDF segment
    rec[1] = ax; rec[2] = ay;
    rec[3] = bx; rec[4] = by;
    rec[9] = c.r; rec[10] = c.g; rec[11] = c.b; rec[12] = c.a;
    rec[13] = r;                         // capsule radius (half thickness)
    rec[kBBoxMinX] = minX; rec[kBBoxMinY] = minY;
    rec[kBBoxMaxX] = maxX; rec[kBBoxMaxY] = maxY;
    out_.insert(out_.end(), rec, rec + kCurveFloats);
}

void Canvas::segment(float x0, float y0, float x1, float y1, float thickness, Color c) {
    if (shapes_ && !rotActive_) {
        float r = thickness * 0.5f;
        float d[6] = { x0, y0, x1, y1, r, 0.0f };
        emitShapeQuad_(std::min(x0, x1) - r, std::min(y0, y1) - r,
                       std::max(x0, x1) + r, std::max(y0, y1) + r,
                       1.0f, d, c);
        return;
    }
    size_t start = out_.size();
    emitCapsule_(x0, y0, x1, y1, thickness * 0.5f, c);
    clipFrom_(start);
}

void Canvas::polyline(const float* xy, int count, float thickness, Color c) {
    if (count < 2) return;
    size_t start = out_.size();
    float r = thickness * 0.5f;
    for (int i = 1; i < count; ++i) {
        float ax = xy[2 * (i - 1)], ay = xy[2 * (i - 1) + 1];
        float bx = xy[2 * i],       by = xy[2 * i + 1];
        // Skip degenerate/non-finite segments: a zero-length capsule divides by
        // zero in the coverage shader's SDF, and NaN sneaks past the equality
        // check (NaN == NaN is false) then corrupts tile bboxes.
        if (ax == bx && ay == by) continue;
        if (!std::isfinite(ax) || !std::isfinite(ay) ||
            !std::isfinite(bx) || !std::isfinite(by)) continue;
        if (shapes_ && !rotActive_) {
            float d[6] = { ax, ay, bx, by, r, 0.0f };
            emitShapeQuad_(std::min(ax, bx) - r, std::min(ay, by) - r,
                           std::max(ax, bx) + r, std::max(ay, by) + r,
                           1.0f, d, c);
            continue;
        }
        emitCapsule_(ax, ay, bx, by, r, c);
    }
    clipFrom_(start);
}

void Canvas::quadMsdfRect(float x, float y, float w, float h, Color c) {
    if (!msdf_ || !quads_) { rect(x, y, w, h, c, 0.0f); return; }
    GlyphQuad q;
    msdf_->layout('I', 0.0f, 0.0f, 100.0f, q);
    float uc = (q.u0 + q.u1) * 0.5f;
    float vc = (q.v0 + q.v1) * 0.5f;

    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (clipActive_) {
        if (x1 <= clipX0_ || x0 >= clipX1_ || y1 <= clipY0_ || y0 >= clipY1_) return;
        x0 = std::max(x0, clipX0_); x1 = std::min(x1, clipX1_);
        y0 = std::max(y0, clipY0_); y1 = std::min(y1, clipY1_);
    }
    auto vert = [&](float vx, float vy) {
        xform_(vx, vy);
        quads_->push_back(vx); quads_->push_back(vy);
        quads_->push_back(uc); quads_->push_back(vc);
        quads_->push_back(c.r); quads_->push_back(c.g);
        quads_->push_back(c.b); quads_->push_back(c.a);
    };
    vert(x0, y0); vert(x1, y0); vert(x1, y1);
    vert(x0, y0); vert(x1, y1); vert(x0, y1);
}

void Canvas::setRotation(float radians, float pivotX, float pivotY) {
    rotActive_ = true;
    rotCos_ = std::cos(radians);
    rotSin_ = std::sin(radians);
    rotPx_  = pivotX;
    rotPy_  = pivotY;
}

void Canvas::clearRotation() { rotActive_ = false; }

void Canvas::xform_(float& x, float& y) const {
    if (!rotActive_) return;
    float dx = x - rotPx_, dy = y - rotPy_;
    x = rotPx_ + dx * rotCos_ - dy * rotSin_;
    y = rotPy_ + dx * rotSin_ + dy * rotCos_;
}

void Canvas::rotateRecordsFrom_(size_t startIdx) {
    if (!rotActive_) return;
    for (size_t r = startIdx; r + kCurveFloats <= out_.size(); r += kCurveFloats) {
        int type = static_cast<int>(out_[r]);
        // Number of (x,y) points stored at slots [1..]: cubic=4, quad=3,
        // line/segment=2. Other (SDF box/parabola/halfplane) types aren't
        // closed under rotation and are not emitted while a rotation is active.
        int npts = (type == 4) ? 4 : (type == 5) ? 3 : (type == 6 || type == 3) ? 2 : 0;
        if (npts == 0) continue;
        float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
        for (int k = 0; k < npts; ++k) {
            float px = out_[r + 1 + 2 * k], py = out_[r + 2 + 2 * k];
            xform_(px, py);
            out_[r + 1 + 2 * k] = px; out_[r + 2 + 2 * k] = py;
            minX = std::min(minX, px); minY = std::min(minY, py);
            maxX = std::max(maxX, px); maxY = std::max(maxY, py);
        }
        out_[r + kBBoxMinX] = minX; out_[r + kBBoxMinY] = minY;
        out_[r + kBBoxMaxX] = maxX; out_[r + kBBoxMaxY] = maxY;
    }
}

void Canvas::emitRotatableRect_(float x, float y, float w, float h, Color c) {
    // A horizontal capsule of half-thickness h/2 spanning [x, x+w] at mid-height:
    // segment endpoints inset by the radius so the rounded ends land at x and x+w.
    float hh = h * 0.5f;
    float ax = x + hh, ay = y + hh;
    float bx = x + w - hh, by = y + hh;
    if (bx < ax) { ax = bx = x + w * 0.5f; }  // degenerate (very short) button
    xform_(ax, ay); xform_(bx, by);

    // Tiling bbox: the rotated rectangle's 4 corners (the capsule fits inside).
    float cxs[4] = {x, x + w, x + w, x};
    float cys[4] = {y, y, y + h, y + h};
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (int i = 0; i < 4; ++i) {
        float px = cxs[i], py = cys[i];
        xform_(px, py);
        minX = std::min(minX, px); minY = std::min(minY, py);
        maxX = std::max(maxX, px); maxY = std::max(maxY, py);
    }

    float rec[kCurveFloats] = {0};
    rec[0] = 3.0f;                 // SDF segment
    rec[1] = ax; rec[2] = ay;
    rec[3] = bx; rec[4] = by;
    rec[9] = c.r; rec[10] = c.g; rec[11] = c.b; rec[12] = c.a;
    rec[13] = hh;                  // lineWidth = capsule radius → filled pill
    rec[kBBoxMinX] = minX; rec[kBBoxMinY] = minY;
    rec[kBBoxMaxX] = maxX; rec[kBBoxMaxY] = maxY;
    out_.insert(out_.end(), rec, rec + kCurveFloats);
}

void Canvas::setClip(float x, float y, float w, float h) {
    clipActive_ = true;
    clipX0_ = x; clipY0_ = y; clipX1_ = x + w; clipY1_ = y + h;
}

void Canvas::clearClip() { clipActive_ = false; }

void Canvas::occlude(float x, float y, float w, float h) {
    if (!quads_ || quads_->empty()) return;
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    std::vector<float>& q = *quads_;
    constexpr int kFloatsPerGlyph = 48;  // 6 verts × 8 floats (pos.xy, uv, rgba)
    size_t write = 0;
    for (size_t g = 0; g + kFloatsPerGlyph <= q.size(); g += kFloatsPerGlyph) {
        float cx = 0.0f, cy = 0.0f;       // glyph centre = mean of its 6 verts
        for (int v = 0; v < 6; v++) { cx += q[g + v * 8]; cy += q[g + v * 8 + 1]; }
        cx /= 6.0f; cy /= 6.0f;
        if (cx >= x0 && cx < x1 && cy >= y0 && cy < y1) continue;  // occluded → drop
        if (write != g) std::copy(q.begin() + g, q.begin() + g + kFloatsPerGlyph,
                                  q.begin() + write);
        write += kFloatsPerGlyph;
    }
    q.resize(write);
}

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
    rotateRecordsFrom_(start);  // rotate glyph cubics when a rotation is active
    clipFrom_(start);
}

// MSDF text: lay out each glyph, clip its quad to the active clip rect (adjusting
// UVs proportionally so partly-visible glyphs at a scroll boundary don't bleed),
// and append two triangles to the quad buffer.
void Canvas::emitTextMsdf_(std::string_view str, float x, float baselineY, float size, Color c) {
    float pen = x;
    for (size_t i = 0; i < str.size(); ) {
        uint32_t cp = utf8::nextCodepoint(str, i);
        GlyphQuad q;
        pen = msdf_->layout(cp, pen, baselineY, size, q);
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
            xform_(vx, vy);
            quads_->push_back(vx); quads_->push_back(vy);
            quads_->push_back(vu); quads_->push_back(vv);
            quads_->push_back(c.r); quads_->push_back(c.g);
            quads_->push_back(c.b); quads_->push_back(c.a);
        };
        vert(x0, y0, u0, v0); vert(x1, y0, u1, v0); vert(x1, y1, u1, v1);
        vert(x0, y0, u0, v0); vert(x1, y1, u1, v1); vert(x0, y1, u0, v1);
    }
}

void Canvas::mathGlyph(uint32_t key, float penX, float baselineY, float size, Color c) {
    if (!msdf_ || !quads_) return;
    GlyphQuad q;
    msdf_->layoutByKey(key, penX, baselineY, size, q);
    if (!q.draw) return;

    float x0 = q.x0, y0 = q.y0, x1 = q.x1, y1 = q.y1;
    float u0 = q.u0, v0 = q.v0, u1 = q.u1, v1 = q.v1;
    if (clipActive_) {
        if (x1 <= clipX0_ || x0 >= clipX1_ || y1 <= clipY0_ || y0 >= clipY1_) return;
        float nx0 = std::max(x0, clipX0_), nx1 = std::min(x1, clipX1_);
        float ny0 = std::max(y0, clipY0_), ny1 = std::min(y1, clipY1_);
        float fu0 = (nx0 - x0) / (x1 - x0), fu1 = (nx1 - x0) / (x1 - x0);
        float fv0 = (ny0 - y0) / (y1 - y0), fv1 = (ny1 - y0) / (y1 - y0);
        float ru0 = u0 + fu0 * (u1 - u0), ru1 = u0 + fu1 * (u1 - u0);
        float rv0 = v0 + fv0 * (v1 - v0), rv1 = v0 + fv1 * (v1 - v0);
        x0 = nx0; x1 = nx1; y0 = ny0; y1 = ny1;
        u0 = ru0; u1 = ru1; v0 = rv0; v1 = rv1;
    }
    auto vert = [&](float vx, float vy, float vu, float vv) {
        xform_(vx, vy);
        quads_->push_back(vx); quads_->push_back(vy);
        quads_->push_back(vu); quads_->push_back(vv);
        quads_->push_back(c.r); quads_->push_back(c.g);
        quads_->push_back(c.b); quads_->push_back(c.a);
    };
    vert(x0, y0, u0, v0); vert(x1, y0, u1, v0); vert(x1, y1, u1, v1);
    vert(x0, y0, u0, v0); vert(x1, y1, u1, v1); vert(x0, y1, u0, v1);
}

float Canvas::textWidthStyled(std::string_view str, float size, FontStyle style) const {
    if (style == FontStyle::Roman || !msdf_) return textWidth(str, size);
    float w = 0.0f;
    for (size_t i = 0; i < str.size(); ) {
        uint32_t cp = utf8::nextCodepoint(str, i);
        uint32_t key = msdf_->keyForStyle(style, cp);
        w += key ? msdf_->advanceKey(key, size) : msdf_->advance(cp, size);
    }
    return w;
}

void Canvas::textStyled(std::string_view str, float x, float y, float size, Color c, FontStyle style) {
    if (style == FontStyle::Roman || !msdf_ || !quads_) { text(str, x, y, size, c); return; }
    float baselineY = y + size, pen = x;
    for (size_t i = 0; i < str.size(); ) {
        uint32_t cp = utf8::nextCodepoint(str, i);
        uint32_t key = msdf_->keyForStyle(style, cp);
        GlyphQuad q;
        pen = key ? msdf_->layoutByKey(key, pen, baselineY, size, q)
                  : msdf_->layout(cp, pen, baselineY, size, q);   // fall back to default face
        if (!q.draw) continue;
        float x0 = q.x0, y0 = q.y0, x1 = q.x1, y1 = q.y1;
        float u0 = q.u0, v0 = q.v0, u1 = q.u1, v1 = q.v1;
        if (clipActive_) {
            if (x1 <= clipX0_ || x0 >= clipX1_ || y1 <= clipY0_ || y0 >= clipY1_) continue;
            float nx0 = std::max(x0, clipX0_), nx1 = std::min(x1, clipX1_);
            float ny0 = std::max(y0, clipY0_), ny1 = std::min(y1, clipY1_);
            float fu0 = (nx0 - x0) / (x1 - x0), fu1 = (nx1 - x0) / (x1 - x0);
            float fv0 = (ny0 - y0) / (y1 - y0), fv1 = (ny1 - y0) / (y1 - y0);
            float ru0 = u0 + fu0 * (u1 - u0), ru1 = u0 + fu1 * (u1 - u0);
            float rv0 = v0 + fv0 * (v1 - v0), rv1 = v0 + fv1 * (v1 - v0);
            x0 = nx0; x1 = nx1; y0 = ny0; y1 = ny1; u0 = ru0; u1 = ru1; v0 = rv0; v1 = rv1;
        }
        auto vert = [&](float vx, float vy, float vu, float vv) {
            xform_(vx, vy);
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
