#include "app.hh"
#include "renderer.hh"
#include "canvas.hh"
#include <android/log.h>

#define LOG_TAG "vk_canvas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static Renderer* g_renderer = nullptr;
static Canvas*   g_canvas   = nullptr;

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
    g_renderer->draw(*g_canvas);
}

void App::init_vulkan() {
    g_renderer = new Renderer(state_->window);
    g_canvas   = new Canvas(g_renderer->width(), g_renderer->height());
    LOGI("Vulkan canvas initialised");
}

void App::destroy_vulkan() {
    delete g_canvas;   g_canvas   = nullptr;
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
