// Headless pixel check for the text-edge blend. text_cover_test pins the
// arithmetic; this pins that the shader and the sRGB view actually store it.
// Exit 77 when the machine cannot run it (no headless surface, no shaders,
// no font). Exit 1 when a pink edge clamps toward white, or a rose edge on
// navy loses its hue.
#undef NDEBUG
#include "asset_reader.hh"
#include "canvas.hh"
#include "headless.hh"
#include "raster_font.hh"
#include "renderer.hh"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef TEXT_EDGE_ASSET_ROOT
#define TEXT_EDGE_ASSET_ROOT ""
#endif
#ifndef TEXT_EDGE_FONT
#define TEXT_EDGE_FONT ""
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

void layoutText(Renderer& renderer, RasterFont& font, Color page, Color ink,
                std::vector<float>& curves, std::vector<float>& quads,
                std::vector<float>& shapes) {
    Canvas canvas(curves, renderer.width(), renderer.height(), nullptr,
                  0, 0, 0, 0);
    canvas.useShapes(&shapes);
    canvas.useMsdf(&font, &quads);
    canvas.rect(0, 0, (float)renderer.width(), (float)renderer.height(), page);
    canvas.text("Ag", 24.0f, 28.0f, 64.0f, ink);
}

bool drawText(Renderer& renderer, RasterFont& font, Color page, Color ink,
              Frame& frame) {
    // Layout once so a missing phase is baked before the frame that is read
    // back. Drawing the incomplete frame would be the image readback returns.
    {
        std::vector<float> curves, quads, shapes;
        layoutText(renderer, font, page, ink, curves, quads, shapes);
    }
    if (font.hasMisses() && font.bakeMisses() > 0)
        renderer.initMsdf(font);
    std::vector<float> curves, quads, shapes;
    layoutText(renderer, font, page, ink, curves, quads, shapes);
    renderer.draw(curves, 0, {}, {}, quads, shapes);
    if (!renderer.readbackLastFrame(frame.rgba, frame.w, frame.h)) return false;
    // The page is not grey. Whichever channel sits nearer the page's red
    // than its blue is R, which tells RGBA from BGRA without asking the
    // swapchain format.
    Rgb raw0;
    const uint8_t* p = frame.rgba.data() + (size_t)(8 * frame.w + 8) * 4;
    raw0 = { p[0], p[1], p[2] };
    const int pageR = (int)std::lround(page.r * 255.0f);
    const int pageB = (int)std::lround(page.b * 255.0f);
    frame.bgra = std::abs(raw0.r - pageB) + std::abs(raw0.b - pageR)
               < std::abs(raw0.r - pageR) + std::abs(raw0.b - pageB);
    return true;
}

}  // namespace

int main() {
    if (!headless_surface_supported()) {
        std::fprintf(stderr, "text_edge_probe: no headless surface\n");
        return 77;
    }
    RootReader assets;
    assets.root = TEXT_EDGE_ASSET_ROOT;
    {
        std::vector<uint8_t> spv;
        if (!assets.read("shaders/msdf_frag.spv", spv) || spv.empty()
            || !assets.read("shaders/shape_frag.spv", spv)) {
            std::fprintf(stderr, "text_edge_probe: shaders missing under %s\n",
                         assets.root.c_str());
            return 77;
        }
        std::vector<uint8_t> fontBytes;
        if (!assets.read(TEXT_EDGE_FONT, fontBytes) || fontBytes.empty()) {
            std::fprintf(stderr, "text_edge_probe: font missing %s\n",
                         TEXT_EDGE_FONT);
            return 77;
        }
    }

    const Color paper{ 245 / 255.0f, 245 / 255.0f, 240 / 255.0f, 1.0f };
    const Color pink { 217 / 255.0f,  64 / 255.0f, 115 / 255.0f, 1.0f };
    const Color navy {  32 / 255.0f,  35 / 255.0f,  48 / 255.0f, 1.0f };
    const Color rose { 255 / 255.0f, 240 / 255.0f, 245 / 255.0f, 1.0f };
    const Rgb paper8{245, 245, 240};
    const Rgb pink8{217, 64, 115};
    const Rgb navy8{32, 35, 48};
    const Rgb rose8{255, 240, 245};

    try {
        HeadlessSurfaceProvider surface(320, 160);
        Renderer renderer(surface, assets, 3);
        renderer.setPresentEnabled(false);

        RasterFont font;
        if (!font.open(assets, TEXT_EDGE_FONT)) {
            std::fprintf(stderr, "text_edge_probe: font open failed\n");
            return 77;
        }
        const std::vector<uint32_t> cps = { 'A', 'g' };
        if (font.ensureGlyphs(cps, { 64 }) < 0) {
            std::fprintf(stderr, "text_edge_probe: glyph bake failed\n");
            return 1;
        }
        renderer.initMsdf(font);
        if (!renderer.msdfReady()) {
            std::fprintf(stderr, "text_edge_probe: text pipeline missing\n");
            return 1;
        }

        Frame pinkFrame, roseFrame;
        if (!drawText(renderer, font, paper, pink, pinkFrame)
            || !drawText(renderer, font, navy, rose, roseFrame)) {
            std::fprintf(stderr, "text_edge_probe: readback failed\n");
            return 1;
        }

        int ink = 0, edge = 0, hot = 0;
        for (uint32_t y = 0; y < pinkFrame.h; ++y) {
            for (uint32_t x = 0; x < pinkFrame.w; ++x) {
                Rgb p = pinkFrame.at(x, y);
                if (p.r > 250) ++hot;
                if (near(p, pink8, 2)) ++ink;
                else if (!near(p, paper8, 2)) ++edge;
            }
        }
        std::printf("pink-on-paper: ink=%d edge=%d r>250=%d bgra=%d\n",
                    ink, edge, hot, pinkFrame.bgra ? 1 : 0);
        if (ink < 30 || edge < 30) {
            std::fprintf(stderr, "text_edge_probe: pink text did not draw\n");
            return 1;
        }
        if (hot != 0) {
            std::fprintf(stderr, "text_edge_probe: pink edge clamped toward white\n");
            return 1;
        }

        int mid = 0, neutral = 0;
        for (uint32_t y = 0; y < roseFrame.h; ++y) {
            for (uint32_t x = 0; x < roseFrame.w; ++x) {
                Rgb p = roseFrame.at(x, y);
                if (p.g < 165 || p.g > 205) continue;
                if (near(p, navy8, 2) || near(p, rose8, 2)) continue;
                ++mid;
                if (p.r < p.b + 2) ++neutral;
            }
        }
        std::printf("rose-on-navy: mid=%d neutral=%d\n", mid, neutral);
        if (mid < 15) {
            std::fprintf(stderr, "text_edge_probe: rose fringe not found\n");
            return 1;
        }
        if (neutral != 0) {
            std::fprintf(stderr, "text_edge_probe: rose fringe lost its hue\n");
            return 1;
        }

        // A frame with no glyphs still has to reach PRESENT_SRC. The UI pass
        // stops in COLOR_ATTACHMENT_OPTIMAL when the sRGB text pass exists,
        // and readback refuses to copy from any other layout.
        {
            std::vector<float> curves, shapes;
            Canvas canvas(curves, renderer.width(), renderer.height(), nullptr,
                          0, 0, 0, 0);
            canvas.useShapes(&shapes);
            canvas.rect(0, 0, (float)renderer.width(), (float)renderer.height(),
                        navy);
            renderer.draw(curves, 0, {}, {}, {}, shapes);
            Frame bare;
            if (!renderer.readbackLastFrame(bare.rgba, bare.w, bare.h)) {
                std::fprintf(stderr, "text_edge_probe: empty-text readback failed\n");
                return 1;
            }
            bare.bgra = roseFrame.bgra;
            const Rgb px = bare.at(bare.w / 2, bare.h / 2);
            std::printf("no-text: center=%d,%d,%d\n", px.r, px.g, px.b);
            if (!near(px, navy8, 2)) {
                std::fprintf(stderr, "text_edge_probe: empty-text frame is not the page\n");
                return 1;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "text_edge_probe: %s\n", e.what());
        return 1;
    }
    std::printf("text_edge_probe: ok\n");
    return 0;
}
