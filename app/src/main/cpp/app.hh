#pragma once
#include <android_native_app_glue.h>

class App {
public:
    explicit App(android_app* state);
    ~App();
    void run();

private:
    android_app* state_;

    void init_vulkan();
    void destroy_vulkan();
    void draw_frame();

    static void handle_cmd(android_app* app, int32_t cmd);
};
