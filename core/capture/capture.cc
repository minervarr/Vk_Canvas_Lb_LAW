#include "capture.hh"

#include "headless.hh"
#include "renderer.hh"

#ifndef _WIN32
#include "wayland_display.hh"   // primary-monitor mode query (no window)
#endif

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace vkc {

namespace {

struct Args {
    std::string out;
    uint32_t    frame_w = 0, frame_h = 0;   // 0 => query monitor
    int         scale = 1;
    std::string only;
    bool        list = false;
};

bool parse_wxh(const char* s, uint32_t& w, uint32_t& h) {
    return std::sscanf(s, "%ux%u", &w, &h) == 2 && w > 0 && h > 0;
}

Args parse_args(int argc, char** argv, const CaptureConfig& cfg) {
    Args a;
    a.out = cfg.default_out;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : ""; };
        if      (k == "--out")   a.out = next();
        else if (k == "--frame") parse_wxh(next(), a.frame_w, a.frame_h);
        else if (k == "--scale") a.scale = std::atoi(next());
        else if (k == "--only")  a.only = next();
        else if (k == "--list")  a.list = true;
    }
    if (a.scale < 1) a.scale = 1;
    if (a.scale > 8) a.scale = 8;
    return a;
}

// Resolve the 1x logical frame: --frame wins, else the primary monitor's mode.
bool resolve_frame(Args& a) {
    if (a.frame_w && a.frame_h) return true;
#ifndef _WIN32
    WaylandDisplay display;
    if (display.valid() && !display.outputs().empty()) {
        const auto& o = display.outputs()[0];
        if (o.width > 0 && o.height > 0) {
            a.frame_w = (uint32_t)o.width;
            a.frame_h = (uint32_t)o.height;
            return true;
        }
    }
#endif
    std::fprintf(stderr,
        "error: could not query a monitor; pass --frame WxH "
        "(e.g. --frame 960x700)\n");
    return false;
}

} // namespace

int capture_main(int argc, char** argv,
                 const std::vector<Scenario>& scenarios,
                 const CaptureConfig& cfg) {
    Args a = parse_args(argc, argv, cfg);

    if (a.list) {
        for (const auto& s : scenarios) std::printf("%s\n", s.path.c_str());
        return 0;
    }

    if (!resolve_frame(a)) return 2;

    if (!headless_surface_supported()) {
        std::fprintf(stderr,
            "error: VK_EXT_headless_surface not available on this Vulkan ICD.\n"
            "       Install a driver that supports it (Mesa lavapipe works).\n");
        return 3;
    }

    const uint32_t out_w = a.frame_w * (uint32_t)a.scale;
    const uint32_t out_h = a.frame_h * (uint32_t)a.scale;
    std::printf("capture: frame %ux%u x%d = %ux%u -> %s\n",
                a.frame_w, a.frame_h, a.scale, out_w, out_h, a.out.c_str());

    FileAssetReader assets;
    HeadlessSurfaceProvider surface(out_w, out_h);
    Renderer renderer(surface, assets, 3);
    if (cfg.init) cfg.init(renderer);

    int failures = 0, written = 0;
    std::vector<uint8_t> rgba;
    for (const auto& sc : scenarios) {
        if (!a.only.empty() && sc.path.find(a.only) == std::string::npos) continue;

        sc.render(renderer);

        uint32_t w = 0, h = 0;
        if (!renderer.readbackLastFrame(rgba, w, h)) {
            std::fprintf(stderr, "  FAIL (readback) %s\n", sc.path.c_str());
            ++failures;
            continue;
        }

        std::filesystem::path png = std::filesystem::path(a.out) / (sc.path + ".png");
        std::error_code ec;
        std::filesystem::create_directories(png.parent_path(), ec);

        if (!stbi_write_png(png.string().c_str(), (int)w, (int)h, 4,
                            rgba.data(), (int)(w * 4))) {
            std::fprintf(stderr, "  FAIL (write) %s\n", png.string().c_str());
            ++failures;
            continue;
        }
        std::printf("  %s (%ux%u)\n", png.string().c_str(), w, h);
        ++written;
    }

    std::printf("capture: %d written, %d failed\n", written, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace vkc
