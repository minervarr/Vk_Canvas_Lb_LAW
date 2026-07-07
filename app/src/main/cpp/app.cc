#include "app.hh"
#include "renderer.hh"
#include "canvas.hh"
#include <android/log.h>
#include <vector>

#define LOG_TAG "vk_canvas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static Renderer*          g_renderer = nullptr;
static std::vector<float> g_curves;

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
    g_renderer = new Renderer(state_->window, state_->activity->assetManager);
    LOGI("Vulkan canvas initialised (%ux%u)", g_renderer->width(), g_renderer->height());
}

void App::destroy_vulkan() {
    delete g_renderer; g_renderer = nullptr;
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
