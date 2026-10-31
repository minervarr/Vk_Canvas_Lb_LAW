// Renderer lifecycle: resize, minimize/restore, surface loss/regain and
// repeated create/destroy under load. These are the recovery paths Phase 3 is
// about; the draw-path correctness lives in gpu_smoke_test.
//
// Exit 77 when the machine cannot run it (no headless surface or shaders).
// Exit 1 on a real failure.
#undef NDEBUG
#include "asset_reader.hh"
#include "canvas.hh"
#include "headless.hh"
#include "renderer.hh"

#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VCE_LIFECYCLE_ASSET_ROOT
#define VCE_LIFECYCLE_ASSET_ROOT ""
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

bool shaders_present(RootReader& assets) {
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
    return true;
}

bool centre_is(const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
               uint8_t r, uint8_t g, uint8_t b, int tol = 6) {
    if (rgba.size() < static_cast<size_t>(w) * h * 4) return false;
    const uint8_t* p = rgba.data() + (static_cast<size_t>(h / 2) * w + w / 2) * 4;
    auto near = [tol](int got, int want) { return got >= want - tol && got <= want + tol; };
    // RGBA or BGRA, whichever the driver hands back.
    return (near(p[0], r) && near(p[1], g) && near(p[2], b)) ||
           (near(p[0], b) && near(p[1], g) && near(p[2], r));
}

// One frame of a known colour, readback-checked. Also proves the swapchain
// extent followed the provider.
void draw_and_check(Renderer& renderer, const Color& colour,
                    uint32_t expect_w, uint32_t expect_h) {
    assert(renderer.width() == expect_w);
    assert(renderer.height() == expect_h);

    std::vector<float> curves;
    Canvas canvas(curves, expect_w, expect_h, nullptr, 0, 0, 0, 0);
    canvas.rect(0, 0, static_cast<float>(expect_w), static_cast<float>(expect_h), colour);
    renderer.draw(curves, 0);

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    assert(renderer.readbackLastFrame(rgba, w, h));
    assert(w == expect_w && h == expect_h);
    const int r = static_cast<int>(colour.r * 255.0f + 0.5f);
    const int g = static_cast<int>(colour.g * 255.0f + 0.5f);
    const int b = static_cast<int>(colour.b * 255.0f + 0.5f);
    assert(centre_is(rgba, w, h, static_cast<uint8_t>(r),
                     static_cast<uint8_t>(g), static_cast<uint8_t>(b)));
}

}  // namespace

int main() {
    if (!headless_surface_supported()) {
        std::fprintf(stderr, "renderer_lifecycle_test: no headless surface\n");
        return 77;
    }

    RootReader assets;
    assets.root = VCE_LIFECYCLE_ASSET_ROOT;
    if (!shaders_present(assets)) {
        std::fprintf(stderr, "renderer_lifecycle_test: shaders missing under %s\n",
                     assets.root.c_str());
        return 77;
    }

    const Color colour{ 0.15f, 0.55f, 0.85f, 1.0f };

    try {
        HeadlessSurfaceProvider surface(192, 128);
        Renderer renderer(surface, assets, 3);
        renderer.setPresentEnabled(false);
        assert(!renderer.deviceLost());
        assert(!renderer.surfaceLost());
        draw_and_check(renderer, colour, 192, 128);

        // Resize through the explicit path a host uses when it jumps straight
        // to a new size (fullscreen toggle), not the lazy acquire discovery.
        surface.set_extent(320, 200);
        renderer.notifyResized();
        draw_and_check(renderer, colour, 320, 200);

        // Minimize: 0x0. draw()/notifyResized() must be no-ops, not a crash or
        // a zero-extent swapchain create.
        surface.set_extent(0, 0);
        renderer.notifyResized();
        std::vector<float> empty;
        renderer.draw(empty, 0);
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        assert(!renderer.readbackLastFrame(rgba, w, h) || w == 320);

        // Restore: the next swapchain rebuild picks the new extent up.
        surface.set_extent(256, 160);
        renderer.notifyResized();
        draw_and_check(renderer, colour, 256, 160);

        // Surface loss/regain: the window goes away, then comes back at a new
        // size. recreate_surface() keeps device and pipelines.
        surface.set_extent(0, 0);
        assert(renderer.recreate_surface());
        surface.set_extent(288, 176);
        assert(renderer.recreate_surface());
        assert(!renderer.surfaceLost());
        draw_and_check(renderer, colour, 288, 176);

        // Soak: repeated create/draw/destroy cycles exercise teardown under
        // the sanitizer stage of tools/audit.sh.
        for (int cycle = 0; cycle < 8; ++cycle) {
            HeadlessSurfaceProvider cycle_surface(160, 96);
            Renderer cycle_renderer(cycle_surface, assets, 3);
            cycle_renderer.setPresentEnabled(false);
            for (int frame = 0; frame < 4; ++frame) {
                std::vector<float> curves;
                Canvas canvas(curves, 160, 96, nullptr, 0, 0, 0, 0);
                canvas.rect(0, 0, 160, 96, colour);
                cycle_renderer.draw(curves, frame * 90);
            }
            std::vector<uint8_t> out;
            uint32_t out_w = 0, out_h = 0;
            assert(cycle_renderer.readbackLastFrame(out, out_w, out_h));
        }

        std::printf("renderer_lifecycle_test: ok\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "renderer_lifecycle_test: %s\n", e.what());
        return 1;
    }
    return 0;
}
