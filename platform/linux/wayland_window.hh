#pragma once
// One xdg-shell toplevel. A Wayland window has no size until the compositor's
// first configure, so the constructor commits and roundtrips until configured
// — only then may a Renderer be built on it (the swapchain takes its extent
// from SurfaceProvider::extent() immediately).
//
// Kiosk-friendly by design: set_fullscreen(output) needs no decorations at
// all (wlroots draws none for clients); windowed mode simply takes whatever
// size the compositor assigns (tiling compositors will tile it).

#include "wayland_display.hh"

#include <vulkan/vulkan.h>

struct xdg_surface;
struct xdg_toplevel;

class WaylandWindow {
public:
    // Creates surface + xdg toplevel and waits for the first configure.
    WaylandWindow(WaylandDisplay& display, const char* title,
                  const char* app_id, uint32_t width, uint32_t height);
    ~WaylandWindow();

    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;

    bool valid() const { return surface_ && toplevel_ && configured_; }

    wl_surface* surface() const { return surface_; }

    // Current drawable size in buffer pixels (logical size × buffer scale).
    VkExtent2D extent() const {
        return { static_cast<uint32_t>(logical_w_ * scale_),
                 static_cast<uint32_t>(logical_h_ * scale_) };
    }

    void set_fullscreen(wl_output* output);   // null = compositor chooses
    void unset_fullscreen();

    bool closed() const { return closed_; }
    // True once after the size changed since the last call — the host calls
    // Renderer::notifyResized() on it (see renderer.hh's comment: needed when
    // a window jumps to a new size in one step, e.g. going fullscreen).
    bool take_resized() { bool r = resized_; resized_ = false; return r; }

    // Protocol listener entry points (public: named by the namespace-scope
    // listener tables in the .cc; not part of the consumer API).
    static void on_xdg_surface_configure(void* data, xdg_surface* s, uint32_t serial);
    static void on_toplevel_configure(void* data, xdg_toplevel* t,
                                      int32_t w, int32_t h, wl_array* states);
    static void on_toplevel_close(void* data, xdg_toplevel* t);
    static void on_surface_enter(void* data, wl_surface* s, wl_output* output);
    static void on_surface_leave(void* data, wl_surface* s, wl_output* output);

private:
    void apply_scale(int32_t scale);

    WaylandDisplay& display_;
    wl_surface*     surface_  = nullptr;
    xdg_surface*    xdg_surface_ = nullptr;
    xdg_toplevel*   toplevel_ = nullptr;

    int32_t logical_w_ = 0, logical_h_ = 0;   // current, from configure
    int32_t pending_w_ = 0, pending_h_ = 0;   // toplevel configure, pre-ack
    int32_t scale_     = 1;                   // integer buffer scale

    bool configured_ = false;
    bool closed_     = false;
    bool resized_    = false;
};
