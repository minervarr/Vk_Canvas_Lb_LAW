#include "app.hh"
#include "renderer.hh"
#include "canvas.hh"
#include "android_platform.hh"
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <vector>

#define LOG_TAG "vk_canvas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Platform seam objects must outlive the Renderer that references them.
static AndroidSurfaceProvider* g_surface  = nullptr;
static AndroidAssetReader*     g_assets   = nullptr;
static Renderer*               g_renderer = nullptr;
static std::vector<float>      g_curves;

// Pin the window to 30fps with FIXED_SOURCE compatibility so SurfaceFlinger
// knows we are the rate source and stops hunting the LTPO refresh mode.
// ONLY_IF_SEAMLESS (not ALWAYS) lets the display driver pick the right moment —
// ALWAYS forced mode switches on Samsung's LTPO driver were themselves causing
// brief refresh-rate transitions.
static void pin_frame_rate(ANativeWindow* window) {
    using WithStrategyFn = int32_t (*)(ANativeWindow*, float, int8_t, int8_t);
    auto fn_ex = reinterpret_cast<WithStrategyFn>(
        dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRateWithChangeStrategy"));
    if (fn_ex) {
        fn_ex(window, 30.0f, /*FIXED_SOURCE*/ 1, /*ONLY_IF_SEAMLESS*/ 0);
        LOGI("Pinned window frame rate to 30fps FIXED_SOURCE ONLY_IF_SEAMLESS");
    } else {
        using SetFrameRateFn = int32_t (*)(ANativeWindow*, float, int8_t);
        auto fn = reinterpret_cast<SetFrameRateFn>(
            dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate"));
        if (fn) {
            fn(window, 30.0f, /*FIXED_SOURCE*/ 1);
            LOGI("Pinned window frame rate to 30fps FIXED_SOURCE");
        }
    }
}

App::App(android_app* state) : state_(state) {
    state_->userData = this;
    state_->onAppCmd = handle_cmd;
}

App::~App() {
    destroy_vulkan();
}

void App::run() {
    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(g_renderer ? 0 : -1, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(state_, source);
            if (state_->destroyRequested) return;
        }
        if (g_renderer) draw_frame();
    }
}

void App::draw_frame() {
    g_curves.clear();
    // Test scene exercising the tiling -> coverage -> composite path: a panel
    // rect plus stroke-fallback text (no Font loaded yet, so glyphs.cc curves).
    Canvas canvas(g_curves, g_renderer->width(), g_renderer->height(),
                  /*font=*/nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
    canvas.clear(col::bg);
    float w = canvas.w(), h = canvas.h();
    canvas.rect(w * 0.1f, h * 0.4f, w * 0.8f, h * 0.2f, col::panel, w * 0.02f);
    canvas.text("VK CANVAS", w * 0.15f, h * 0.47f, h * 0.05f, col::text);
    g_renderer->draw(g_curves, /*overlay_rotation_deg=*/0);
}

void App::init_vulkan() {
    g_surface  = new AndroidSurfaceProvider(state_->window);
    g_assets   = new AndroidAssetReader(state_->activity->assetManager);
    g_renderer = new Renderer(*g_surface, *g_assets);
    pin_frame_rate(state_->window);
    LOGI("Vulkan canvas initialised (%ux%u)", g_renderer->width(), g_renderer->height());
}

void App::destroy_vulkan() {
    delete g_renderer; g_renderer = nullptr;
    delete g_surface;  g_surface  = nullptr;
    delete g_assets;   g_assets   = nullptr;
}

void App::handle_cmd(android_app* app, int32_t cmd) {
    auto* self = reinterpret_cast<App*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) self->init_vulkan();
            break;
        case APP_CMD_TERM_WINDOW:
            self->destroy_vulkan();
            break;
        default:
            break;
    }
}
