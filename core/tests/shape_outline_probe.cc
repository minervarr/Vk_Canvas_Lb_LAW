// Headless pixel check for Canvas::rectOutline. A border drawn as four
// straight rects leaves the pixel at the old sharp corner border-coloured;
// the SDF outline must leave it background and only light the rounded band.
// Exit 77 when the machine cannot run it (no headless surface, no shaders).
#undef NDEBUG
#include "asset_reader.hh"
#include "canvas.hh"
#include "headless.hh"
#include "renderer.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SHAPE_OUTLINE_ASSET_ROOT
#define SHAPE_OUTLINE_ASSET_ROOT ""
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
        return got == out.size();
    }
};

struct Rgb { int r, g, b; };

int near(Rgb a, Rgb b, int tol) {
    return std::abs(a.r - b.r) <= tol && std::abs(a.g - b.g) <= tol
        && std::abs(a.b - b.b) <= tol;
}

struct Frame {
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    bool bgra = false;
    Rgb at(uint32_t x, uint32_t y) const {
        const uint8_t* p = rgba.data() + (size_t)(y * w + x) * 4;
        if (bgra) return { p[2], p[1], p[0] };
        return { p[0], p[1], p[2] };
    }
};

Rgb to8(const Color& c) {
    return { (int)std::lround(c.r * 255.0f),
             (int)std::lround(c.g * 255.0f),
             (int)std::lround(c.b * 255.0f) };
}

int check(const Frame& f, uint32_t x, uint32_t y, Rgb want,
          const char* what) {
    const Rgb got = f.at(x, y);
    if (!near(got, want, 8)) {
        std::fprintf(stderr,
                     "shape_outline_probe: %s at (%u,%u) = %d,%d,%d, want %d,%d,%d\n",
                     what, x, y, got.r, got.g, got.b, want.r, want.g, want.b);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (!headless_surface_supported()) {
        std::fprintf(stderr, "shape_outline_probe: no headless surface\n");
        return 77;
    }
    RootReader assets;
    assets.root = SHAPE_OUTLINE_ASSET_ROOT;
    {
        std::vector<uint8_t> spv;
        if (!assets.read("shaders/shape_frag.spv", spv) || spv.empty()) {
            std::fprintf(stderr, "shape_outline_probe: shape shader missing under %s\n",
                         assets.root.c_str());
            return 77;
        }
    }

    const Color bg     { 10 / 255.0f, 13 / 255.0f, 18 / 255.0f, 1.0f };
    const Color fill   { 41 / 255.0f, 43 / 255.0f, 54 / 255.0f, 1.0f };
    const Color border { 240 / 255.0f, 161 / 255.0f, 61 / 255.0f, 1.0f };
    const Rgb bg8 = to8(bg);
    const Rgb fill8 = to8(fill);
    const Rgb border8 = to8(border);

    try {
        HeadlessSurfaceProvider surface(160, 96);
        Renderer renderer(surface, assets, 3);
        renderer.setPresentEnabled(false);

        std::vector<float> curves, shapes;
        Canvas canvas(curves, renderer.width(), renderer.height(), nullptr,
                      0, 0, 0, 0);
        canvas.useShapes(&shapes);
        canvas.rect(0, 0, (float)renderer.width(), (float)renderer.height(), bg);
        canvas.rect(20, 16, 120, 64, fill, 16);
        canvas.rectOutline(20, 16, 120, 64, border, 16, 3);
        renderer.draw(curves, 0, {}, {}, {}, shapes);

        Frame frame;
        if (!renderer.readbackLastFrame(frame.rgba, frame.w, frame.h)) {
            std::fprintf(stderr, "shape_outline_probe: readback failed\n");
            return 1;
        }
        {
            const Rgb raw = frame.at(5, 5);
            frame.bgra = std::abs(raw.r - bg8.b) + std::abs(raw.b - bg8.r)
                       < std::abs(raw.r - bg8.r) + std::abs(raw.b - bg8.b);
        }

        int failures = 0;
        failures += check(frame, 80, 48, fill8, "centre must be fill");
        failures += check(frame, 80, 17, border8, "top edge must be border");
        failures += check(frame, 21, 48, border8, "left edge must be border");
        failures += check(frame, 25, 21, border8, "rounded corner must be border");
        failures += check(frame, 21, 21, bg8, "outside the corner arc must be background");
        // This is the regression: the old four straight lines painted it.
        failures += check(frame, 20, 16, bg8, "sharp corner must not be border");
        failures += check(frame, 80, 22, fill8, "inside the stroke must be fill");
        failures += check(frame, 18, 18, bg8, "outside the box must be background");

        if (failures != 0) {
            std::fprintf(stderr, "shape_outline_probe: %d check(s) failed (bgra=%d)\n",
                         failures, frame.bgra ? 1 : 0);
            return 1;
        }
        std::printf("shape_outline_probe: ok (bgra=%d)\n", frame.bgra ? 1 : 0);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "shape_outline_probe: %s\n", e.what());
        return 1;
    }
}
