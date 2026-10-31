// CPU-only geometry contract for the toolkit widgets. Every widget draws and
// hit-tests from the same sub-rect; these assertions pin that source of truth
// without a GPU, a window or a Canvas. The UI target links draw+font, so the
// test still proves the layer compiles without the Vulkan renderer.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "widgets.hh"

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
    // Toggle switch: square-ish, right-aligned, vertically centred in the row.
    {
        Rect row{10, 20, 200, 40};
        Rect sw = widgets::toggleSwitchRect(row);
        assert(near(sw.h, 40 * 0.66f));
        assert(near(sw.w, sw.h * 1.8f));
        assert(near(sw.x + sw.w, row.x + row.w));
        assert(near(sw.y + sw.h * 0.5f, row.y + row.h * 0.5f));
    }

    // Stepper: minus / value / plus are contiguous, ordered and right-aligned.
    {
        Rect row{0, 0, 300, 50};
        auto g = widgets::stepperGeom(row);
        assert(g.minus.x + g.minus.w < g.value.x);
        assert(g.value.x + g.value.w < g.plus.x);
        assert(near(g.plus.x + g.plus.w, row.x + row.w));
        assert(near(g.minus.y, g.value.y));
        assert(near(g.minus.h, g.value.h));
        assert(g.minus.w > 0 && g.value.w > 0 && g.plus.w > 0);
    }

    // Slider: bar excludes the label/value zones; thumb centre follows t and
    // clamps at both ends.
    {
        Rect row{0, 0, 300, 50};
        const float x0 = row.w * 0.34f;
        const float x1 = row.w * 0.84f;
        auto left = widgets::sliderGeom(row, 0.0f);
        auto mid = widgets::sliderGeom(row, 0.5f);
        auto right = widgets::sliderGeom(row, 1.0f);
        assert(near(left.bar.x, x0));
        assert(near(left.bar.x + left.bar.w, x1));
        assert(near(left.thumb.x + left.thumb.w * 0.5f, x0));
        assert(near(mid.thumb.x + mid.thumb.w * 0.5f, (x0 + x1) * 0.5f));
        assert(near(right.thumb.x + right.thumb.w * 0.5f, x1));
        // Out-of-range t is clamped, not extrapolated.
        auto under = widgets::sliderGeom(row, -1.0f);
        auto over = widgets::sliderGeom(row, 2.0f);
        assert(near(under.thumb.x, left.thumb.x));
        assert(near(over.thumb.x, right.thumb.x));

        assert(near(widgets::sliderValueAt(row, x0), 0.0f));
        assert(near(widgets::sliderValueAt(row, x1), 1.0f));
        assert(near(widgets::sliderValueAt(row, (x0 + x1) * 0.5f), 0.5f));
        assert(near(widgets::sliderValueAt(row, -100.0f), 0.0f));
        assert(near(widgets::sliderValueAt(row, 500.0f), 1.0f));
    }

    // Segmented control: equal widths, gaps accounted for, full row covered.
    {
        Rect row{5, 7, 300, 30};
        auto rects = widgets::segmentRects(row, 3);
        assert(rects.size() == 3);
        assert(near(rects[0].x, row.x));
        assert(near(rects[2].x + rects[2].w, row.x + row.w));
        assert(near(rects[0].w, rects[1].w));
        assert(near(rects[1].w, rects[2].w));
        assert(rects[0].x + rects[0].w < rects[1].x);
        // i == 0 is the formula's reference; invalid count returns the row.
        assert(widgets::segmentRectAt(row, 0, 0).w == row.w);
    }

    // Table header: weight-based columns divide the row exactly; explicit
    // widths are honoured verbatim and stay contiguous.
    {
        Rect area{10, 20, 400, 200};
        Rect header = widgets::tableHeaderRow(area, 32);
        assert(near(header.x, area.x));
        assert(near(header.y, area.y));
        assert(near(header.w, area.w));
        assert(near(header.h, 32));

        std::vector<widgets::TableColumn> cols = {{"a", 1}, {"b", 2}, {"c", 1}};
        auto rects = widgets::tableHeaderColumnRects(header, cols);
        assert(rects.size() == 3);
        assert(near(rects[0].w, 100));
        assert(near(rects[1].w, 200));
        assert(near(rects[2].w, 100));
        assert(near(rects[2].x + rects[2].w, header.x + header.w));

        std::vector<float> widths = {50, 150, 200};
        auto explicitRects = widgets::tableHeaderColumnRects(header, cols, &widths);
        assert(near(explicitRects[0].w, 50));
        assert(near(explicitRects[1].x, header.x + 50));
        assert(near(explicitRects[2].x, header.x + 200));
        assert(near(explicitRects[2].x + explicitRects[2].w, header.x + header.w));

        // Out-of-range col falls back to the whole header, never out of bounds.
        assert(widgets::tableHeaderColumnRect(header, cols, -1).w == header.w);
        assert(widgets::tableHeaderColumnRect(header, cols, 9).w == header.w);
    }

    assert(widgets::listContentHeight(0, 20) == 0);
    assert(widgets::listContentHeight(7, 20) == 140);

    std::printf("widget_geometry_test: OK\n");
    return 0;
}
