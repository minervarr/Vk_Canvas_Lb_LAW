// Win32 entry point: raw windows.h window + message pump driving the shared
// engine core. Renders the same test scene as the Android backend.

#include "win32_platform.hh"
#include "renderer.hh"
#include "canvas.hh"
#include "texture.hh"
#include "log.hh"

#include <vector>
#include <cstdint>

#define LOG_TAG "vk_canvas"
#define LOGI(...) VCE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGE(...) VCE_LOGE(LOG_TAG, __VA_ARGS__)

namespace {

bool g_running = true;

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { g_running = false; PostQuitMessage(0); }
            return 0;
        case WM_SIZE:
            // No explicit action needed: the next draw() call's
            // vkAcquireNextImageKHR/vkQueuePresentKHR will report
            // OUT_OF_DATE/SUBOPTIMAL once the swapchain extent no longer
            // matches the surface, and Renderer::recreate_swapchain() picks
            // up the new size from Win32SurfaceProvider::extent() (live
            // GetClientRect). Minimizing (wp == SIZE_MINIMIZED, 0x0 client
            // rect) is handled by recreate_swapchain()'s own 0x0 guard.
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

HWND create_window(uint32_t w, uint32_t h) {
    HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512) /* IDC_ARROW */);
    wc.lpszClassName = L"vk_canvas_window";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;  // resizable: renderer recreates the swapchain on resize
    RECT rc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    AdjustWindowRect(&rc, style, FALSE);

    return CreateWindowExW(0, wc.lpszClassName, L"VK Canvas", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           nullptr, nullptr, hinst, nullptr);
}

// Generates a 64x64 RGBA checkerboard (opaque red/blue squares, one corner
// quadrant semi-transparent) to exercise Canvas::image()/ImageLayer without
// needing an external asset — proves upload, sampling and premultiplied-alpha
// blending against the panel/text drawn on top of it.
TextureHandle make_test_texture(Renderer& renderer) {
    constexpr uint32_t kSize = 64, kCell = 8;
    std::vector<uint8_t> rgba(kSize * kSize * 4);
    for (uint32_t y = 0; y < kSize; y++) {
        for (uint32_t x = 0; x < kSize; x++) {
            bool checker = ((x / kCell) + (y / kCell)) % 2 == 0;
            uint8_t* px = &rgba[(y * kSize + x) * 4];
            px[0] = checker ? 220 : 40;
            px[1] = checker ? 60  : 60;
            px[2] = checker ? 60  : 220;
            px[3] = (x < kSize / 2 && y < kSize / 2) ? 128 : 255;  // TL quadrant translucent
        }
    }
    return renderer.create_texture(rgba.data(), kSize, kSize);
}

void draw_frame(Renderer& renderer, std::vector<float>& curves,
                std::vector<ImageDraw>& images, TextureHandle testTex) {
    curves.clear();
    images.clear();
    // Same test scene as platform/android: panel rect + stroke-fallback text
    // through the tiling -> coverage -> composite path, plus a textured quad
    // (background layer) exercising Canvas::image()/ImageLayer.
    Canvas canvas(curves, renderer.width(), renderer.height(),
                  /*font=*/nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.useImages(&images);
    // No canvas.clear() here: clear() draws an OPAQUE full-screen rect into
    // the vector/overlay curve buffer (it's not a real framebuffer clear), so
    // it would composite on top of the image layer below and hide it. The
    // render pass already clears the swapchain image to black; the vector
    // layer only needs to draw what's actually opaque UI (panel/text).
    float w = canvas.w(), h = canvas.h();
    canvas.image(testTex, w * 0.35f, h * 0.15f, w * 0.3f, w * 0.3f);
    canvas.rect(w * 0.1f, h * 0.4f, w * 0.8f, h * 0.2f, col::panel, w * 0.02f);
    canvas.text("VK CANVAS", w * 0.15f, h * 0.47f, h * 0.05f, col::text);
    renderer.draw(curves, /*overlay_rotation_deg=*/0, images);
}

}  // namespace

int main() {
    HWND hwnd = create_window(1280, 720);
    if (!hwnd) { LOGE("CreateWindowEx failed"); return 1; }
    ShowWindow(hwnd, SW_SHOW);

    Win32SurfaceProvider surface(hwnd);
    FileAssetReader      assets;

    try {
        Renderer renderer(surface, assets);
        LOGI("Vulkan canvas initialised (%ux%u)", renderer.width(), renderer.height());
        TextureHandle testTex = make_test_texture(renderer);

        std::vector<float> curves;
        std::vector<ImageDraw> images;
        while (g_running) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!g_running) break;
            draw_frame(renderer, curves, images, testTex);
        }
    } catch (const std::exception& e) {
        LOGE("Fatal: %s", e.what());
        MessageBoxA(hwnd, e.what(), "vk_canvas fatal error", MB_ICONERROR);
        return 1;
    }
    return 0;
}
