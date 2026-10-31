// Canvas is an emitter: it appends plain float records. That makes its
// geometry and clip rules testable without a GPU, a font atlas or a swapchain.
// This test is the CPU-side contract for the curve path and the SDF shape path.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "canvas.hh"

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

static constexpr size_t kCurve = 20;   // floats per curve record
static constexpr size_t kShapeVert = 14;
static constexpr size_t kShapeVerts = 6;

int main() {
    // ── clear(): one full-screen rounded-box record ────────────────────────
    {
        std::vector<float> out;
        Canvas c(out, 1000, 800, nullptr, 0, 0, 0, 0);
        c.clear(Color{0.1f, 0.2f, 0.3f, 0.4f});
        assert(out.size() == kCurve);
        assert(near(out[0], 2.0f));       // rounded box
        assert(near(out[1], 500.0f));     // centre x
        assert(near(out[2], 400.0f));     // centre y
        assert(near(out[3], 500.0f));     // half width
        assert(near(out[4], 400.0f));     // half height
        assert(near(out[9], 0.1f) && near(out[12], 0.4f));
        assert(near(out[14], -1.0f) && near(out[15], -1.0f));
        assert(near(out[16], 1001.0f) && near(out[17], 801.0f));
    }

    // ── rect(): exact centre, half extents, colour and 1px tiling box ──────
    {
        std::vector<float> out;
        Canvas c(out, 1000, 800, nullptr, 0, 0, 0, 0);
        c.rect(10.0f, 20.0f, 30.0f, 40.0f, Color{1.0f, 0.0f, 0.0f, 1.0f}, 6.0f);
        assert(out.size() == kCurve);
        assert(near(out[0], 2.0f));
        assert(near(out[1], 25.0f));
        assert(near(out[2], 40.0f));
        assert(near(out[3], 15.0f));
        assert(near(out[4], 20.0f));
        assert(near(out[5], 6.0f));
        assert(near(out[9], 1.0f) && near(out[10], 0.0f));
        assert(near(out[14], 9.0f) && near(out[15], 19.0f));
        assert(near(out[16], 41.0f) && near(out[17], 61.0f));
    }

    // ── rectOutline(): inner stroke, path inset by half the thickness ──────
    {
        std::vector<float> out;
        Canvas c(out, 1000, 800, nullptr, 0, 0, 0, 0);
        c.rectOutline(10.0f, 20.0f, 30.0f, 40.0f,
                      Color{0.0f, 1.0f, 0.0f, 1.0f}, 6.0f, 2.0f);
        assert(out.size() == kCurve);
        assert(near(out[0], 2.0f));
        assert(near(out[1], 25.0f));
        assert(near(out[2], 40.0f));
        assert(near(out[3], 14.0f));   // 15 - half thickness
        assert(near(out[4], 19.0f));   // 20 - half thickness
        assert(near(out[5], 5.0f));    // radius - half thickness
        assert(near(out[13], 1.0f));   // half stroke width

        const size_t before = out.size();
        c.rectOutline(0.0f, 0.0f, 0.0f, 10.0f, Color{1, 1, 1, 1});
        c.rectOutline(0.0f, 0.0f, 10.0f, 10.0f, Color{1, 1, 1, 1}, 2.0f, 0.0f);
        assert(out.size() == before);  // degenerate outlines emit nothing
    }

    // ── SDF shape path: six vertices, kind + params packed per vertex ──────
    {
        std::vector<float> shapes;
        Canvas c(shapes, 1000, 800, nullptr, 0, 0, 0, 0);
        c.useShapes(&shapes);

        c.rect(10.0f, 20.0f, 30.0f, 40.0f,
               Color{1.0f, 0.5f, 0.25f, 1.0f}, 6.0f);
        assert(shapes.size() == kShapeVert * kShapeVerts);

        // First vertex: expanded by the 1px AA margin.
        assert(near(shapes[0], 9.0f) && near(shapes[1], 19.0f));
        assert(near(shapes[2], 1.0f) && near(shapes[3], 0.5f));
        assert(near(shapes[6], 0.0f));       // kind 0 = rounded box
        assert(near(shapes[7], 25.0f));      // centre x
        assert(near(shapes[8], 40.0f));      // centre y
        assert(near(shapes[9], 15.0f));      // half width
        assert(near(shapes[10], 20.0f));     // half height
        assert(near(shapes[11], 6.0f));      // radius
        assert(near(shapes[12], 0.0f));      // reserved

        // Vertex 2 is the opposite corner; the last vertex returns to x0/y1
        // (two triangles: 0-1-2 and 3-4-5).
        const size_t opposite = 2 * kShapeVert;
        assert(near(shapes[opposite + 0], 41.0f));
        assert(near(shapes[opposite + 1], 61.0f));
        const size_t last = (kShapeVerts - 1) * kShapeVert;
        assert(near(shapes[last + 0], 9.0f));
        assert(near(shapes[last + 1], 61.0f));

        // rectOutline on the shape path: kind 3, path radius and half thickness.
        c.rectOutline(10.0f, 20.0f, 30.0f, 40.0f,
                      Color{0.0f, 1.0f, 0.0f, 1.0f}, 6.0f, 2.0f);
        assert(shapes.size() == 2 * kShapeVert * kShapeVerts);
        const size_t r = kShapeVert * kShapeVerts;
        assert(near(shapes[r + 6], 3.0f));
        assert(near(shapes[r + 7], 25.0f));
        assert(near(shapes[r + 8], 40.0f));
        assert(near(shapes[r + 9], 14.0f));
        assert(near(shapes[r + 10], 19.0f));
        assert(near(shapes[r + 11], 5.0f));
        assert(near(shapes[r + 12], 1.0f));

        // Triangle on the shape path: kind 2 carries the three points.
        const size_t tri = shapes.size();
        c.triangle(10.0f, 10.0f, 50.0f, 10.0f, 30.0f, 40.0f,
                   Color{0.0f, 0.0f, 1.0f, 1.0f});
        assert(shapes.size() == tri + kShapeVert * kShapeVerts);
        assert(near(shapes[tri + 6], 2.0f));
        assert(near(shapes[tri + 7], 10.0f));
        assert(near(shapes[tri + 8], 10.0f));
        assert(near(shapes[tri + 9], 50.0f));
        assert(near(shapes[tri + 10], 10.0f));
        assert(near(shapes[tri + 11], 30.0f));
        assert(near(shapes[tri + 12], 40.0f));
    }

    // ── Image records: texture, rect, UVs, tone defaults, clip ─────────────
    {
        std::vector<float> out;
        std::vector<ImageDraw> images;
        std::vector<ImageDraw> foreground;
        Canvas c(out, 1000, 800, nullptr, 0, 0, 0, 0);
        c.useImages(&images);
        c.useImagesFg(&foreground);

        c.image(7, 1.0f, 2.0f, 3.0f, 4.0f);
        assert(images.size() == 1);
        assert(images[0].tex == 7);
        assert(near(images[0].x, 1.0f) && near(images[0].y, 2.0f));
        assert(near(images[0].w, 3.0f) && near(images[0].h, 4.0f));
        assert(near(images[0].u0, 0.0f) && near(images[0].u1, 1.0f));
        assert(near(images[0].v0, 0.0f) && near(images[0].v1, 1.0f));
        assert(!images[0].hasClip);

        c.image(kInvalidTexture, 0, 0, 10, 10);
        assert(images.size() == 1);   // invalid handle is a no-op

        c.setClip(5.0f, 6.0f, 20.0f, 30.0f);
        c.image(8, 10.0f, 12.0f, 4.0f, 5.0f);
        assert(images.size() == 2);
        assert(images[1].hasClip);
        assert(near(images[1].clipX, 5.0f) && near(images[1].clipY, 6.0f));
        assert(near(images[1].clipW, 20.0f) && near(images[1].clipH, 30.0f));
        c.clearClip();

        c.imageFg(9, 0.0f, 0.0f, 1.0f, 1.0f);
        assert(foreground.size() == 1);
        assert(foreground[0].tex == 9);
    }

    // ── Clipping: outside records are dropped, inside bboxes are clamped ──
    {
        std::vector<float> out;
        Canvas c(out, 1000, 800, nullptr, 0, 0, 0, 0);
        c.setClip(0.0f, 0.0f, 50.0f, 50.0f);

        c.rect(60.0f, 60.0f, 10.0f, 10.0f, Color{1, 1, 1, 1});
        assert(out.empty());   // fully outside

        c.rect(45.0f, 45.0f, 10.0f, 10.0f, Color{1, 1, 1, 1});
        assert(out.size() == kCurve);
        assert(near(out[14], 44.0f) && near(out[15], 44.0f));
        assert(near(out[16], 50.0f) && near(out[17], 50.0f));  // clamped

        // Nested clips intersect, never replace.
        c.setClip(10.0f, 10.0f, 20.0f, 20.0f);
        out.clear();
        c.rect(0.0f, 0.0f, 100.0f, 100.0f, Color{1, 1, 1, 1});
        assert(out.size() == kCurve);
        assert(near(out[14], 10.0f) && near(out[15], 10.0f));
        assert(near(out[16], 30.0f) && near(out[17], 30.0f));
        c.clearClip();
    }

    std::printf("canvas_records_test: all assertions passed\n");
    return 0;
}
