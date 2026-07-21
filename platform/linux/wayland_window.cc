#include "wayland_window.hh"

#include "xdg-shell-client-protocol.h"

#include <cstdio>

// ── listeners ───────────────────────────────────────────────────────────────

void WaylandWindow::on_xdg_surface_configure(void* data, xdg_surface* s,
                                             uint32_t serial)
{
    auto* w = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(s, serial);
    // 0x0 from the toplevel means "client decides" — keep the current size.
    if (w->pending_w_ > 0 && w->pending_h_ > 0 &&
        (w->pending_w_ != w->logical_w_ || w->pending_h_ != w->logical_h_)) {
        w->logical_w_ = w->pending_w_;
        w->logical_h_ = w->pending_h_;
        w->resized_   = true;
    }
    w->configured_ = true;
}

void WaylandWindow::on_toplevel_configure(void* data, xdg_toplevel*,
                                          int32_t width, int32_t height,
                                          wl_array*)
{
    auto* w = static_cast<WaylandWindow*>(data);
    w->pending_w_ = width;
    w->pending_h_ = height;
}

void WaylandWindow::on_toplevel_close(void* data, xdg_toplevel*)
{
    static_cast<WaylandWindow*>(data)->closed_ = true;
}

void WaylandWindow::on_surface_enter(void* data, wl_surface*, wl_output* output)
{
    auto* w = static_cast<WaylandWindow*>(data);
    if (const WaylandOutput* out = w->display_.find_output(output))
        w->apply_scale(out->scale);
}

void WaylandWindow::on_surface_leave(void*, wl_surface*, wl_output*) {}

namespace {

const xdg_surface_listener kXdgSurfaceListener = {
    WaylandWindow::on_xdg_surface_configure,
};

const xdg_toplevel_listener kToplevelListener = {
    WaylandWindow::on_toplevel_configure,
    WaylandWindow::on_toplevel_close,
};

const wl_surface_listener kSurfaceListener = {
    WaylandWindow::on_surface_enter,
    WaylandWindow::on_surface_leave,
};

}  // namespace

// ── lifecycle ───────────────────────────────────────────────────────────────

WaylandWindow::WaylandWindow(WaylandDisplay& display, const char* title,
                             const char* app_id, uint32_t width, uint32_t height)
    : display_(display), logical_w_((int32_t)width), logical_h_((int32_t)height)
{
    if (!display_.valid()) return;

    surface_ = wl_compositor_create_surface(display_.compositor());
    wl_surface_add_listener(surface_, &kSurfaceListener, this);

    xdg_surface_ = xdg_wm_base_get_xdg_surface(display_.wm_base(), surface_);
    xdg_surface_add_listener(xdg_surface_, &kXdgSurfaceListener, this);

    toplevel_ = xdg_surface_get_toplevel(xdg_surface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, title);
    xdg_toplevel_set_app_id(toplevel_, app_id);

    // The window exists only after the first configure/ack exchange — no
    // size, no mapping before that. The Renderer must be constructed after
    // this returns (its swapchain reads extent() immediately).
    wl_surface_commit(surface_);
    while (!configured_ && !closed_)
        wl_display_roundtrip(display_.display());
    resized_ = false;   // the initial size is not a "resize"
}

WaylandWindow::~WaylandWindow()
{
    if (toplevel_)    xdg_toplevel_destroy(toplevel_);
    if (xdg_surface_) xdg_surface_destroy(xdg_surface_);
    if (surface_)     wl_surface_destroy(surface_);
    // Let the compositor see the teardown before any next window reuses
    // the spot (also flushes the destroy requests).
    display_.roundtrip();
}

void WaylandWindow::set_fullscreen(wl_output* output)
{
    if (!toplevel_) return;
    xdg_toplevel_set_fullscreen(toplevel_, output);
    wl_surface_commit(surface_);
    // The new size arrives via configure; pump until it lands so the caller
    // can rebuild the swapchain at the real extent right away.
    display_.roundtrip();
}

void WaylandWindow::unset_fullscreen()
{
    if (!toplevel_) return;
    xdg_toplevel_unset_fullscreen(toplevel_);
    wl_surface_commit(surface_);
    display_.roundtrip();
}

void WaylandWindow::apply_scale(int32_t scale)
{
    if (scale == scale_ || scale < 1) return;
    scale_ = scale;
    // Buffers are logical×scale from now on; the compositor is told via
    // buffer_scale (takes effect with the swapchain's next present/commit).
    wl_surface_set_buffer_scale(surface_, scale);
    resized_ = true;
}
