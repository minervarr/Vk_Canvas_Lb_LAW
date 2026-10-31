// Headless smoke test for every Renderer draw path: overlay curves, SDF
// shapes, textured images, clipping, rotation, MSDF text (when a font is
// available) and the FrameStats timings. It asserts structural facts (the
// centre is the colour just drawn, a clipped pixel stays background), not
// exact hashes across drivers.
//
// Exit 77 when the machine cannot run it (no headless surface, shaders or
// font). Exit 1 when a path did not reach the frame.
#undef NDEBUG
#include "asset_reader.hh"
#include "canvas.hh"
#include "headless.hh"
#include "raster_font.hh"
#include "renderer.hh"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef GPU_SMOKE_ASSET_ROOT
#define GPU_SMOKE_ASSET_ROOT ""
#endif
#ifndef GPU_SMOKE_FONT
#define GPU_SMOKE_FONT ""
#endif

namespace {

struct RootReader : AssetReader {
    std::string root;
    bool read(const char* path, std::vector<uint8_t>& out) override {
        std::string full = path;
        if (path[0] != '/') full = root + path;
        FILE* f = std::fopen(full.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        if (n < 0) { std::fclose(f); return false; }
        std::fseek(f, 0, SEEK_SET);
        out.resize(static_cast<size_t>(n));
        size_t got = std::fread(out.data(), 1, out.size(), f);
        std::fclose(f);
        return got == out.size() && !out.empty();
    }
};

struct Rgb { int r, g, b; };

struct Frame {
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    Rgb at(uint32_t x, uint32_t y) const {
        const uint8_t* p = rgba.data() + (size_t)(y * w + x) * 4;
        return { p[0], p[1], p[2] };
    }
};

Rgb to8(const Color& c) {
    return { (int)std::lround(c.r * 255.0f),
             (int)std::lround(c.g * 255.0f),
             (int)std::lround(c.b * 255.0f) };
}

// The readback is RGBA on every target we create, but a driver is free to
// hand back BGRA; accept either channel order.
bool near_color(Rgb got, const Color& want, int tol = 6) {
    const Rgb w = to8(want);
    const bool rgba = std::abs(got.r - w.r) <= tol && std::abs(got.g - w.g) <= tol
                   && std::abs(got.b - w.b) <= tol;
    const bool bgra = std::abs(got.r - w.b) <= tol && std::abs(got.g - w.g) <= tol
                   && std::abs(got.b - w.r) <= tol;
    return rgba || bgra;
}

bool near_black(Rgb got, int tol = 6) {
    return got.r <= tol && got.g <= tol && got.b <= tol;
}

bool read_frame(Renderer& renderer, Frame& frame) {
    return renderer.readbackLastFrame(frame.rgba, frame.w, frame.h)
        && frame.w > 0 && frame.h > 0;
}

bool shaders_present(RootReader& assets, bool need_msdf) {
    const char* required[] = {
        "shaders/composite_vert.spv", "shaders/composite_frag.spv",
        "shaders/tiling.spv", "shaders/coverage.spv",
        "shaders/overlay_vert.spv", "shaders/overlay_frag.spv",
        "shaders/image_vert.spv", "shaders/image_frag.spv",
        "shaders/shape_vert.spv", "shaders/shape_frag.spv",
    };
    std::vector<uint8_t> bytes;
    for (const char* path : required)
        if (!assets.read(path, bytes)) return false;
    if (need_msdf &&
        (!assets.read("shaders/msdf_vert.spv", bytes) ||
         !assets.read("shaders/msdf_frag.spv", bytes)))
        return false;
    return true;
}

}  // namespace

int main() {
    if (!headless_surface_supported()) {
        std::fprintf(stderr, "gpu_smoke_test: no headless surface\n");
        return 77;
    }

    RootReader assets;
    assets.root = GPU_SMOKE_ASSET_ROOT;

    std::vector<uint8_t> font_bytes;
    const bool have_font = assets.read(GPU_SMOKE_FONT, font_bytes);
    if (!shaders_present(assets, have_font)) {
        std::fprintf(stderr, "gpu_smoke_test: shaders missing under %s\n",
                     assets.root.c_str());
        return 77;
    }

    const Color overlay_col{ 0.20f, 0.40f, 0.80f, 1.0f };
    const Color shape_col  { 0.20f, 0.80f, 0.40f, 1.0f };
    const Color image_col  { 0.90f, 0.30f, 0.20f, 1.0f };

    try {
        HeadlessSurfaceProvider surface(192, 128);
        Renderer renderer(surface, assets, 3);
        renderer.setPresentEnabled(false);
        const uint32_t w = renderer.width();
        const uint32_t h = renderer.height();

        // 1. Overlay curve path (compute rasterizer).
        {
            std::vector<float> curves;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.rect(0, 0, (float)w, (float)h, overlay_col);
            renderer.draw(curves, 0);
            Frame frame;
            if (!read_frame(renderer, frame)) {
                std::fprintf(stderr, "gpu_smoke_test: overlay readback failed\n");
                return 1;
            }
            if (!near_color(frame.at(w / 2, h / 2), overlay_col)) {
                std::fprintf(stderr, "gpu_smoke_test: overlay curve did not draw\n");
                return 1;
            }
            assert(renderer.lastFrameStats().draw_count == 1);
            assert(renderer.lastFrameStats().cpu_total_ms >= 0.0);
        }

        // 2. SDF shape path.
        {
            std::vector<float> curves, shapes;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.useShapes(&shapes);
            canvas.rect(0, 0, (float)w, (float)h, shape_col);
            renderer.draw(curves, 0, {}, {}, {}, shapes);
            Frame frame;
            if (!read_frame(renderer, frame)
                || !near_color(frame.at(w / 2, h / 2), shape_col)) {
                std::fprintf(stderr, "gpu_smoke_test: shape path did not draw\n");
                return 1;
            }
        }

        // 3. Textured image path, then the same draw with a clip.
        const uint8_t rgba[2 * 2 * 4] = {
            230, 77, 51, 255, 230, 77, 51, 255,
            230, 77, 51, 255, 230, 77, 51, 255,
        };
        const TextureHandle tex = renderer.create_texture(rgba, 2, 2, false);
        if (tex == kInvalidTexture) {
            std::fprintf(stderr, "gpu_smoke_test: create_texture failed\n");
            return 1;
        }
        {
            std::vector<float> curves;
            std::vector<ImageDraw> images;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.useImages(&images);
            canvas.image(tex, 0, 0, (float)w, (float)h);
            renderer.draw(curves, 0, images);
            Frame frame;
            if (!read_frame(renderer, frame)
                || !near_color(frame.at(w / 2, h / 2), image_col)) {
                std::fprintf(stderr, "gpu_smoke_test: image path did not draw\n");
                return 1;
            }
        }
        {
            std::vector<float> curves;
            std::vector<ImageDraw> images;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.useImages(&images);
            canvas.setClip(8, 8, 40, 40);
            canvas.image(tex, 0, 0, (float)w, (float)h);
            canvas.clearClip();
            renderer.draw(curves, 0, images);
            Frame frame;
            if (!read_frame(renderer, frame)) {
                std::fprintf(stderr, "gpu_smoke_test: clip readback failed\n");
                return 1;
            }
            if (!near_color(frame.at(24, 24), image_col)) {
                std::fprintf(stderr, "gpu_smoke_test: clipped image missing\n");
                return 1;
            }
            if (!near_black(frame.at(w - 24, h - 24))) {
                std::fprintf(stderr, "gpu_smoke_test: clip leaked outside\n");
                return 1;
            }
        }

        // 4. Rotation: a left-half curve rect becomes a top or bottom half.
        {
            std::vector<float> curves;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.rect(0, 0, (float)(w / 2), (float)h, overlay_col);
            renderer.draw(curves, 90);
            Frame frame;
            if (!read_frame(renderer, frame)) {
                std::fprintf(stderr, "gpu_smoke_test: rotation readback failed\n");
                return 1;
            }
            const Rgb top = frame.at(w / 2, h / 4);
            const Rgb bottom = frame.at(w / 2, 3 * h / 4);
            const bool top_hit = near_color(top, overlay_col);
            const bool bottom_hit = near_color(bottom, overlay_col);
            if (top_hit == bottom_hit) {
                std::fprintf(stderr,
                             "gpu_smoke_test: rotation did not swap the halves\n");
                return 1;
            }
        }

        // 5. MSDF text, only when a face is available.
        if (have_font) {
            RasterFont font;
            if (!font.open(assets, GPU_SMOKE_FONT)) {
                std::fprintf(stderr, "gpu_smoke_test: font open failed\n");
                return 1;
            }
            const std::vector<uint32_t> cps = { 'A' };
            if (font.ensureGlyphs(cps, { 64 }) < 0) {
                std::fprintf(stderr, "gpu_smoke_test: glyph bake failed\n");
                return 1;
            }
            renderer.initMsdf(font);
            if (!renderer.msdfReady()) {
                std::fprintf(stderr, "gpu_smoke_test: text pipeline missing\n");
                return 1;
            }
            std::vector<float> curves, quads, shapes;
            Canvas canvas(curves, w, h, nullptr, 0, 0, 0, 0);
            canvas.useMsdf(&font, &quads);
            canvas.text("A", 24, 24, 64, image_col);
            renderer.draw(curves, 0, {}, {}, quads, shapes);
            Frame frame;
            if (!read_frame(renderer, frame)) {
                std::fprintf(stderr, "gpu_smoke_test: text readback failed\n");
                return 1;
            }
            int ink = 0;
            for (uint32_t y = 0; y < frame.h; ++y)
                for (uint32_t x = 0; x < frame.w; ++x)
                    if (!near_black(frame.at(x, y))) ++ink;
            if (ink < 30) {
                std::fprintf(stderr, "gpu_smoke_test: MSDF text did not draw\n");
                return 1;
            }
        } else {
            std::printf("gpu_smoke_test: no font at %s, MSDF path skipped\n",
                        GPU_SMOKE_FONT);
        }

        renderer.destroy_texture(tex);
        const FrameStats& stats = renderer.lastFrameStats();
        std::printf("gpu_smoke_test: ok (frames=%llu, last cpu %.3f ms, wait %.3f ms, record %.3f ms)\n",
                    (unsigned long long)stats.draw_count, stats.cpu_total_ms,
                    stats.cpu_wait_ms, stats.cpu_record_ms);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "gpu_smoke_test: %s\n", e.what());
        return 1;
    }
    return 0;
}
