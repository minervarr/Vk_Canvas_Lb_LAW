// plotview.hh — reusable 2D graphing widget for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
//
// ---------------------------------------------------------------------------
// A Viewport maps a world window (xmin..xmax, ymin..ymax, y-up) to a screen
// pixel rect (y-down) and supports pan / zoom-about-a-point. PlotView draws the
// adaptive grid, axes, tick labels, caller-supplied world-space polylines, and
// an optional trace marker. It is app-agnostic: the host samples equations into
// world points (any source) and feeds them here. No Android/whisper deps.
// ---------------------------------------------------------------------------
#pragma once
#include "canvas.hh"

namespace plot {

struct Viewport {
    double xmin = -10, xmax = 10, ymin = -6, ymax = 6;  // world window
    float  sx = 0, sy = 0, sw = 1, sh = 1;              // screen pixel rect

    void setScreenRect(float x, float y, float w, float h) { sx = x; sy = y; sw = w; sh = h; }

    float  toScreenX(double wx) const { return sx + float((wx - xmin) / (xmax - xmin)) * sw; }
    float  toScreenY(double wy) const { return sy + float((ymax - wy) / (ymax - ymin)) * sh; }
    double toWorldX(float px)   const { return xmin + (px - sx) / (double)sw * (xmax - xmin); }
    double toWorldY(float py)   const { return ymax - (py - sy) / (double)sh * (ymax - ymin); }
    double worldPerPxX()        const { return (xmax - xmin) / (double)sw; }
    double worldPerPxY()        const { return (ymax - ymin) / (double)sh; }

    // Drag: world window moves opposite the pixel delta so content follows the finger.
    void panPixels(float dxPx, float dyPx) {
        double wdx = dxPx * worldPerPxX(), wdy = dyPx * worldPerPxY();
        xmin -= wdx; xmax -= wdx;
        ymin += wdy; ymax += wdy;   // screen y-down vs world y-up
    }
    // Zoom by `factor` (<1 = zoom in) keeping the world point under (px,py) fixed.
    void zoomAbout(float px, float py, double factor) {
        double wx = toWorldX(px), wy = toWorldY(py);
        xmin = wx + (xmin - wx) * factor; xmax = wx + (xmax - wx) * factor;
        ymin = wy + (ymin - wy) * factor; ymax = wy + (ymax - wy) * factor;
    }
    // Reset to a centred window of half-width `halfX`, y scaled to keep unit aspect.
    void reset(double halfX) {
        xmin = -halfX; xmax = halfX;
        double halfY = (sw > 0) ? halfX * (double)sh / (double)sw : halfX * 0.6;
        ymin = -halfY; ymax = halfY;
    }
};

// One curve in WORLD coords; valid=false marks a pen-lift (asymptote / NaN).
struct CurvePoint { double wx, wy; bool valid; };
struct Curve { const CurvePoint* pts; int count; Color color; };

// Optional trace readout. `valid` false → draw the cursor line but no dot/label.
// `color` tints the cursor/dot/readout (e.g. the traced curve's colour); the
// default is the classic functional-amber.
struct TraceMarker {
    bool on = false;
    bool valid = false;
    double wx = 0, wy = 0;
    Color color = {0.900f, 0.720f, 0.300f, 1.0f};
};

class PlotView {
public:
    Viewport&       viewport()       { return vp_; }
    const Viewport& viewport() const { return vp_; }

    // Palette — clean "academic / pgfplots on black" theme (override per app).
    Color bg      = {0.000f, 0.000f, 0.000f, 1.0f};  // #000000 pure black field
    Color subgrid = {0.086f, 0.086f, 0.086f, 1.0f};  // #161616 ghost-faint subdivisions
    Color grid    = {0.165f, 0.165f, 0.165f, 1.0f};  // #2A2A2A dim major gridlines
    Color axis    = {0.333f, 0.333f, 0.333f, 1.0f};  // #555555 neutral x=0 / y=0
    Color label   = {0.800f, 0.800f, 0.800f, 1.0f};  // #CCCCCC off-white tick labels
    Color trace   = {0.900f, 0.720f, 0.300f, 1.0f};  // functional amber cursor

    // Curve stroke. Slim, LaTeX-like; glow is an opt-in "neon mode" (off here).
    // Width is screen-fraction-based so a "crisp ~1.5-2px line" reads the same on
    // a low-DPI emulator and a 500ppi phone (a literal 2px would be a hairline
    // there). emitCapsule_ halves this to the SDF radius rec[13].
    bool  glow          = false;
    float curveWidthMin  = 1.75f;    // px floor (low-DPI)
    float curveWidthFrac = 0.0016f;  // × plot height

    // Suppress tick labels + the trace readout — the only later-pass *text* a host
    // panel can't occlude via draw order. Lets a centered modal own its panel as a
    // clean layer while the dimmed grid/curves still show as context around it.
    bool  drawLabels = true;

    // Optional emission clip in screen px, decoupled from the world-mapping rect.
    // When set (w>0 && h>0), draw() clips records + text to this instead of the
    // full viewport — lets a host hide the slice of plot a slide-up panel covers
    // WITHOUT remapping the transform, so the graph doesn't shift as it animates.
    // (Text composites in a later pass than panels, so plain draw-order can't
    // occlude it; clipping emission is the structural fix.) clearClipRect()=full.
    void setClipRect(float x, float y, float w, float h) { clipX_=x; clipY_=y; clipW_=w; clipH_=h; }
    void clearClipRect() { clipW_ = 0; clipH_ = 0; }

    // Draw bg + grid + axes + tick labels + curves + trace, clipped to the rect.
    void draw(Canvas& c, const Curve* curves, int nCurves, const TraceMarker& tr) const;
    // Multi-marker variant: all markers share one cursor x (one vertical line),
    // each valid marker gets its own dot + readout in its own colour.
    void draw(Canvas& c, const Curve* curves, int nCurves,
              const TraceMarker* trs, int nTr) const;

private:
    Viewport vp_;
    mutable std::vector<float> scratch_;  // reused per-frame polyline buffer
    float clipX_ = 0, clipY_ = 0, clipW_ = 0, clipH_ = 0;  // emission clip (w/h<=0 → full)

    void drawGrid(Canvas& c) const;
    void emitRuns(Canvas& c, const Curve& cv, float thickness, Color col) const;
    void drawCurve(Canvas& c, const Curve& cv) const;
    void drawTrace(Canvas& c, const TraceMarker* trs, int nTr) const;
};

}  // namespace plot
