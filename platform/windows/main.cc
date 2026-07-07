// Win32 entry point: raw windows.h window + message pump driving the shared
// engine core. Renders the same test scene as the Android backend.

#include "win32_platform.hh"
#include "renderer.hh"
#include "canvas.hh"
#include "log.hh"

#include <vector>

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

    // Fixed-size window for now: the renderer doesn't recreate the swapchain
    // on resize yet, so no thick frame / maximize box.
    DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT rc{ 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
    AdjustWindowRect(&rc, style, FALSE);

    return CreateWindowExW(0, wc.lpszClassName, L"VK Canvas", style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           nullptr, nullptr, hinst, nullptr);
}

void draw_frame(Renderer& renderer, std::vector<float>& curves) {
    curves.clear();
    // Same test scene as platform/android: panel rect + stroke-fallback text
    // through the tiling -> coverage -> composite path.
    Canvas canvas(curves, renderer.width(), renderer.height(),
                  /*font=*/nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.clear(col::bg);
    float w = canvas.w(), h = canvas.h();
    canvas.rect(w * 0.1f, h * 0.4f, w * 0.8f, h * 0.2f, col::panel, w * 0.02f);
    canvas.text("VK CANVAS", w * 0.15f, h * 0.47f, h * 0.05f, col::text);
    renderer.draw(curves, /*overlay_rotation_deg=*/0);
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

        std::vector<float> curves;
        while (g_running) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!g_running) break;
            draw_frame(renderer, curves);
        }
    } catch (const std::exception& e) {
        LOGE("Fatal: %s", e.what());
        MessageBoxA(hwnd, e.what(), "vk_canvas fatal error", MB_ICONERROR);
        return 1;
    }
    return 0;
}
