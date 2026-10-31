// plotview.cc — reusable 2D graphing widget for the canvas engine.
//
// Copyright (C) 2026 nava. AGPLv3 or later; see the header in plotview.hh.
#include "plotview.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace plot {
namespace {

// Smallest "nice" step (1/2/5 × 10ⁿ) giving at most `maxTicks` ticks over `range`.
double niceTickStep(double range, int maxTicks) {
    if (range <= 0.0 || maxTicks < 1) return 1.0;
    double raw  = range / maxTicks;
    double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    double norm = raw / mag;  // [1,10)
    double nice = (norm <= 1.0) ? 1.0 : (norm <= 2.0) ? 2.0 : (norm <= 5.0) ? 5.0 : 10.0;
    return nice * mag;
}

void formatTick(char* buf, size_t n, double v, double step) {
    if (std::fabs(v) < step * 0.25) { std::snprintf(buf, n, "0"); return; }
    double astep = std::fabs(step);
    int decimals = (astep > 0.0 && astep < 1.0) ? (int)std::ceil(-std::log10(astep)) : 0;
    if (decimals > 6) decimals = 6;
    if (std::fabs(v) >= 1e5 || (std::fabs(v) > 0 && std::fabs(v) < 1e-3))
        std::snprintf(buf, n, "%.2g", v);
    else
        std::snprintf(buf, n, "%.*f", decimals, v);
}

}  // namespace

void PlotView::drawGrid(Canvas& c) const {
    const Viewport& v = vp_;
    c.rect(v.sx, v.sy, v.sw, v.sh, bg, 0.0f);
    double rangeX = v.xmax - v.xmin, rangeY = v.ymax - v.ymin;
    if (rangeX <= 0.0 || rangeY <= 0.0) return;

    int targetX = std::max(2, (int)(v.sw / 110.0f));  // ~110px between major lines
    int targetY = std::max(2, (int)(v.sh / 110.0f));
    double majX = niceTickStep(rangeX, targetX), majY = niceTickStep(rangeY, targetY);
    double minX = majX / 5.0, minY = majY / 5.0;      // 5 subdivisions
    float thMin = std::max(1.0f, v.sh * 0.0010f);
    float thMaj = std::max(1.0f, v.sh * 0.0016f);
    float lbl   = std::max(11.0f, v.sh * 0.030f);

    // Fine subgrid first (only when the subdivisions aren't too dense), majors
    // overdraw it, axes overdraw those — cheap and avoids fp skip logic.
    if ((float)(minX / rangeX) * v.sw >= 7.0f) {
        for (double g = std::ceil(v.xmin / minX) * minX; g <= v.xmax; g += minX)
            c.segment(v.toScreenX(g), v.sy, v.toScreenX(g), v.sy + v.sh, thMin, subgrid);
    }
    if ((float)(minY / rangeY) * v.sh >= 7.0f) {
        for (double g = std::ceil(v.ymin / minY) * minY; g <= v.ymax; g += minY)
            c.segment(v.sx, v.toScreenY(g), v.sx + v.sw, v.toScreenY(g), thMin, subgrid);
    }
    double gx0 = std::ceil(v.xmin / majX) * majX;
    for (double g = gx0; g <= v.xmax + majX * 0.001; g += majX)
        c.segment(v.toScreenX(g), v.sy, v.toScreenX(g), v.sy + v.sh, thMaj, grid);
    double gy0 = std::ceil(v.ymin / majY) * majY;
    for (double g = gy0; g <= v.ymax + majY * 0.001; g += majY)
        c.segment(v.sx, v.toScreenY(g), v.sx + v.sw, v.toScreenY(g), thMaj, grid);

    bool yAxisVis = (v.xmin < 0 && v.xmax > 0);
    bool xAxisVis = (v.ymin < 0 && v.ymax > 0);
    float axth = std::max(1.6f, v.sh * 0.0024f);
    if (yAxisVis) c.segment(v.toScreenX(0), v.sy, v.toScreenX(0), v.sy + v.sh, axth, axis);
    if (xAxisVis) c.segment(v.sx, v.toScreenY(0), v.sx + v.sw, v.toScreenY(0), axth, axis);

    if (!drawLabels) return;
    char buf[32];

    // X labels: thin to a uniform stride so wide labels at high zoom (e.g. many
    // decimals) never overlap. Gridlines stay dense; only the text thins, kept
    // symmetric about the origin (draw at tick indices that are multiples of it).
    float majPxX = (float)(majX / rangeX) * v.sw;
    float maxWX = 0.0f;
    for (double g = gx0; g <= v.xmax + majX * 0.001; g += majX) {
        if (std::fabs(g) < majX * 0.25) continue;
        formatTick(buf, sizeof buf, g, majX);
        maxWX = std::max(maxWX, c.textWidth(buf, lbl));
    }
    long strideX = std::max(1L, (long)std::ceil((maxWX + lbl * 0.6f) /
                                                std::max(1.0f, majPxX)));
    float xAxisY = xAxisVis ? v.toScreenY(0) : v.sy + v.sh;
    for (double g = gx0; g <= v.xmax + majX * 0.001; g += majX) {
        if (std::fabs(g) < majX * 0.25) continue;            // origin
        if (std::llround(g / majX) % strideX != 0) continue;  // thinned out
        formatTick(buf, sizeof buf, g, majX);
        float ly = std::min(xAxisY + lbl * 0.20f, v.sy + v.sh - lbl * 1.1f);
        c.textCentered(buf, v.toScreenX(g), ly, lbl, label);
    }

    // Y labels: thin by line height the same way.
    float majPxY  = (float)(majY / rangeY) * v.sh;
    long  strideY = std::max(1L, (long)std::ceil((lbl * 1.4f) /
                                                 std::max(1.0f, majPxY)));
    float yAxisX = yAxisVis ? v.toScreenX(0) : v.sx;
    for (double g = gy0; g <= v.ymax + majY * 0.001; g += majY) {
        if (std::fabs(g) < majY * 0.25) continue;
        if (std::llround(g / majY) % strideY != 0) continue;
        formatTick(buf, sizeof buf, g, majY);
        float lx = std::min(std::max(yAxisX + lbl * 0.25f, v.sx + 2.0f),
                            v.sx + v.sw - lbl * 2.5f);
        c.text(buf, lx, v.toScreenY(g) - lbl * 0.6f, lbl, label);
    }
}

void PlotView::emitRuns(Canvas& c, const Curve& cv, float th, Color col) const {
    scratch_.clear();
    bool havePrev = false;
    float prevY = 0.0f, keptX = 0.0f, keptY = 0.0f;
    bool pending = false;
    float pendX = 0.0f, pendY = 0.0f;
    auto flush = [&]() {
        if (pending) {                      // run always ends on its true endpoint
            scratch_.push_back(pendX);
            scratch_.push_back(pendY);
            pending = false;
        }
        if (scratch_.size() >= 4)
            c.polyline(scratch_.data(), (int)(scratch_.size() / 2), th, col);
        scratch_.clear();
    };
    for (int i = 0; i < cv.count; ++i) {
        const CurvePoint& p = cv.pts[i];
        if (!p.valid) { flush(); havePrev = false; continue; }
        float px = vp_.toScreenX(p.wx), py = vp_.toScreenY(p.wy);
        // NaN/Inf samples poison the GPU tile rasterizer (NaN bboxes corrupt
        // tile binning) — treat them as pen-lifts like `valid=false`.
        if (!std::isfinite(px) || !std::isfinite(py)) { flush(); havePrev = false; continue; }
        if (havePrev && std::fabs(py - prevY) > vp_.sh * 1.5f) { flush(); havePrev = false; }  // asymptote
        prevY = py;
        // Screen-space decimation: at far zoom-out a whole curve collapses into
        // a few pixels; emitting every sample floods the rasterizer's per-tile
        // curve lists (fixed capacity — overflow makes LATER records vanish in
        // that tile: the "glitch square"). Points that moved less than ~a pixel
        // since the last KEPT point are deferred; the newest one is emitted at
        // run end so endpoints stay exact.
        if (havePrev && std::fabs(px - keptX) < 1.2f && std::fabs(py - keptY) < 1.2f) {
            pending = true; pendX = px; pendY = py;
            continue;
        }
        scratch_.push_back(px);
        scratch_.push_back(py);
        keptX = px; keptY = py;
        havePrev = true;
        pending  = false;
    }
    flush();
}

void PlotView::drawCurve(Canvas& c, const Curve& cv) const {
    if (!cv.pts || cv.count < 2) return;
    float th = std::max(curveWidthMin, vp_.sh * curveWidthFrac);
    if (glow) {                                   // soft, wide, low-alpha underlay
        Color g = cv.color; g.a *= 0.16f;
        emitRuns(c, cv, th * 3.0f, g);
    }
    emitRuns(c, cv, th, cv.color);                // crisp stroke
}

void PlotView::drawTrace(Canvas& c, const TraceMarker* trs, int nTr) const {
    int first = -1;
    for (int i = 0; i < nTr; ++i)
        if (trs[i].on) { first = i; break; }
    if (first < 0) return;

    // All markers share one cursor x → one vertical line.
    float px = vp_.toScreenX(trs[first].wx);
    c.segment(px, vp_.sy, px, vp_.sy + vp_.sh, std::max(1.2f, vp_.sh * 0.0022f),
              trs[first].color);

    for (int i = 0; i < nTr; ++i) {
        const TraceMarker& tr = trs[i];
        if (!tr.on || !tr.valid) continue;
        float py = vp_.toScreenY(tr.wy);
        Color faint = tr.color; faint.a *= 0.45f;
        c.segment(vp_.sx, py, vp_.sx + vp_.sw, py, std::max(1.0f, vp_.sh * 0.0016f), faint);

        // Ringed dot (halo → fill → bg hole).
        float r = std::max(4.5f, vp_.sh * 0.012f);
        Color halo = tr.color; halo.a *= 0.30f;
        c.rect(px - r - 3, py - r - 3, 2 * r + 6, 2 * r + 6, halo, r + 3);
        c.rect(px - r, py - r, 2 * r, 2 * r, tr.color, r);
        c.rect(px - r * 0.42f, py - r * 0.42f, r * 0.84f, r * 0.84f, bg, r * 0.42f);

        // Readout chip (later-pass text — skip it so it never lands on a host panel).
        if (!drawLabels) continue;
        char buf[64];
        std::snprintf(buf, sizeof buf, "(%.4g, %.4g)", tr.wx, tr.wy);
        float ts = std::max(12.0f, vp_.sh * 0.032f);
        float tw = c.textWidth(buf, ts);
        float padx = ts * 0.45f, pady = ts * 0.30f;
        float bw = tw + 2 * padx, bh = ts + 2 * pady;
        float bx = px + r + 8.0f;
        if (bx + bw > vp_.sx + vp_.sw) bx = px - r - 8.0f - bw;
        float by = py - r - bh - 4.0f;
        if (by < vp_.sy + 2.0f) by = py + r + 4.0f;
        c.rect(bx, by, bw, bh, Color{0.0f, 0.0f, 0.0f, 0.58f}, bh * 0.28f);
        c.text(buf, bx + padx, by + pady, ts, tr.color);
    }
}

void PlotView::draw(Canvas& c, const Curve* curves, int nCurves, const TraceMarker& tr) const {
    draw(c, curves, nCurves, &tr, 1);
}

void PlotView::draw(Canvas& c, const Curve* curves, int nCurves,
                    const TraceMarker* trs, int nTr) const {
    if (clipW_ > 0.0f && clipH_ > 0.0f) c.setClip(clipX_, clipY_, clipW_, clipH_);
    else                                c.setClip(vp_.sx, vp_.sy, vp_.sw, vp_.sh);
    drawGrid(c);
    for (int i = 0; i < nCurves; ++i) drawCurve(c, curves[i]);
    drawTrace(c, trs, nTr);
    c.clearClip();
}

}  // namespace plot
