#include "wayland_display.hh"

#include "keys.hh"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// xkb keysym -> keys.hh code (the Win32 VK numeric space; see keys.hh).
int keysym_to_keycode(xkb_keysym_t sym)
{
    switch (sym) {
        case XKB_KEY_BackSpace: return key::Backspace;
        case XKB_KEY_Tab:       return key::Tab;
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:  return key::Enter;
        case XKB_KEY_Escape:    return key::Escape;
        case XKB_KEY_space:     return key::Space;
        case XKB_KEY_Prior:     return key::PageUp;
        case XKB_KEY_Next:      return key::PageDown;
        case XKB_KEY_End:       return key::End;
        case XKB_KEY_Home:      return key::Home;
        case XKB_KEY_Left:      return key::Left;
        case XKB_KEY_Up:        return key::Up;
        case XKB_KEY_Right:     return key::Right;
        case XKB_KEY_Down:      return key::Down;
        case XKB_KEY_Delete:    return key::Delete;
        case XKB_KEY_Shift_L:
        case XKB_KEY_Shift_R:   return key::Shift;
        case XKB_KEY_Control_L:
        case XKB_KEY_Control_R: return key::Control;
        case XKB_KEY_Alt_L:
        case XKB_KEY_Alt_R:     return key::Alt;
        default: break;
    }
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return key::F1 + static_cast<int>(sym - XKB_KEY_F1);
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9)
        return key::Numpad0 + static_cast<int>(sym - XKB_KEY_KP_0);
    if (sym >= '0' && sym <= '9') return static_cast<int>(sym);
    if (sym >= 'a' && sym <= 'z') return static_cast<int>(sym) - 0x20;
    if (sym >= 'A' && sym <= 'Z') return static_cast<int>(sym);
    return 0;   // unmapped: onChar still carries any text it produced
}

// Cursor: Wayland clients must provide their own pointer image on enter or
// the cursor is undefined over their surfaces. libwayland-cursor (part of
// the wayland package — a theme loader, not a toolkit) + wl_shm cover it.
struct CursorState {
    wl_shm*          shm           = nullptr;
    wl_cursor_theme* theme         = nullptr;
    wl_cursor*       arrow         = nullptr;
    wl_surface*      surface       = nullptr;
    uint32_t         enter_serial  = 0;
    bool             load_attempted = false;
};
CursorState g_cursor;   // one seat/pointer per process is all we support

void show_cursor(wl_compositor* compositor, wl_pointer* pointer)
{
    if (!g_cursor.load_attempted) {
        g_cursor.load_attempted = true;
        if (g_cursor.shm) {
            const char* size_env = std::getenv("XCURSOR_SIZE");
            int size = size_env ? std::atoi(size_env) : 0;
            if (size <= 0) size = 24;
            g_cursor.theme = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"),
                                                  size, g_cursor.shm);
            if (g_cursor.theme) {
                g_cursor.arrow = wl_cursor_theme_get_cursor(g_cursor.theme, "default");
                if (!g_cursor.arrow)
                    g_cursor.arrow = wl_cursor_theme_get_cursor(g_cursor.theme, "left_ptr");
            }
            if (g_cursor.arrow && !g_cursor.surface)
                g_cursor.surface = wl_compositor_create_surface(compositor);
        }
    }
    if (!g_cursor.arrow || !g_cursor.surface || g_cursor.arrow->image_count == 0)
        return;
    wl_cursor_image* img = g_cursor.arrow->images[0];
    wl_buffer* buf = wl_cursor_image_get_buffer(img);
    if (!buf) return;
    wl_pointer_set_cursor(pointer, g_cursor.enter_serial, g_cursor.surface,
                          static_cast<int32_t>(img->hotspot_x),
                          static_cast<int32_t>(img->hotspot_y));
    wl_surface_attach(g_cursor.surface, buf, 0, 0);
    wl_surface_damage(g_cursor.surface, 0, 0,
                      static_cast<int32_t>(img->width),
                      static_cast<int32_t>(img->height));
    wl_surface_commit(g_cursor.surface);
}

}  // namespace

// ── static listener thunks ──────────────────────────────────────────────────

struct WaylandListeners {
    // wl_pointer
    static void pointer_enter(void* data, wl_pointer* p, uint32_t serial,
                              wl_surface* s, wl_fixed_t x, wl_fixed_t y) {
        auto* d = static_cast<WaylandDisplay*>(data);
        g_cursor.enter_serial = serial;
        show_cursor(d->compositor_, p);
        d->pointer_enter(s, wl_fixed_to_double(x), wl_fixed_to_double(y));
    }
    static void pointer_leave(void* data, wl_pointer*, uint32_t, wl_surface* s) {
        static_cast<WaylandDisplay*>(data)->pointer_leave(s);
    }
    static void pointer_motion(void* data, wl_pointer*, uint32_t,
                               wl_fixed_t x, wl_fixed_t y) {
        static_cast<WaylandDisplay*>(data)->pointer_motion(
            wl_fixed_to_double(x), wl_fixed_to_double(y));
    }
    static void pointer_button(void* data, wl_pointer*, uint32_t serial, uint32_t,
                               uint32_t button, uint32_t state) {
        auto* d = static_cast<WaylandDisplay*>(data);
        d->last_serial_ = serial;   // freshest serial for a click-driven copy
        d->pointer_button(button, state == WL_POINTER_BUTTON_STATE_PRESSED);
    }
    static void pointer_axis(void* data, wl_pointer*, uint32_t,
                             uint32_t axis, wl_fixed_t value) {
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
            static_cast<WaylandDisplay*>(data)->pointer_axis(
                wl_fixed_to_double(value));
    }
    static void pointer_frame(void*, wl_pointer*) {}
    static void pointer_axis_source(void*, wl_pointer*, uint32_t) {}
    static void pointer_axis_stop(void*, wl_pointer*, uint32_t, uint32_t) {}
    static void pointer_axis_discrete(void*, wl_pointer*, uint32_t, int32_t) {}

    // wl_keyboard
    static void keyboard_keymap(void* data, wl_keyboard*, uint32_t format,
                                int32_t fd, uint32_t size) {
        if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
            static_cast<WaylandDisplay*>(data)->keyboard_keymap(fd, size);
        else
            close(fd);
    }
    static void keyboard_enter(void* data, wl_keyboard*, uint32_t serial,
                               wl_surface* s, wl_array*) {
        auto* d = static_cast<WaylandDisplay*>(data);
        d->last_serial_ = serial;
        d->keyboard_enter(s);
    }
    static void keyboard_leave(void* data, wl_keyboard*, uint32_t, wl_surface* s) {
        static_cast<WaylandDisplay*>(data)->keyboard_leave(s);
    }
    static void keyboard_key(void* data, wl_keyboard*, uint32_t serial, uint32_t,
                             uint32_t key, uint32_t state) {
        auto* d = static_cast<WaylandDisplay*>(data);
        d->last_serial_ = serial;
        d->keyboard_key(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
    }
    static void keyboard_modifiers(void* data, wl_keyboard*, uint32_t,
                                   uint32_t depressed, uint32_t latched,
                                   uint32_t locked, uint32_t group) {
        static_cast<WaylandDisplay*>(data)->keyboard_modifiers(
            depressed, latched, locked, group);
    }
    static void keyboard_repeat_info(void* data, wl_keyboard*,
                                     int32_t rate, int32_t delay) {
        static_cast<WaylandDisplay*>(data)->keyboard_repeat_info(rate, delay);
    }

    // wl_seat
    static void seat_capabilities(void* data, wl_seat*, uint32_t caps) {
        static_cast<WaylandDisplay*>(data)->update_seat_devices(caps);
    }
    static void seat_name(void*, wl_seat*, const char*) {}

    // wl_output
    static void output_geometry(void* data, wl_output* o, int32_t x, int32_t y,
                                int32_t, int32_t, int32_t, const char*,
                                const char*, int32_t) {
        auto* d = static_cast<WaylandDisplay*>(data);
        for (auto& out : d->outputs_)
            if (out.output == o) { out.x = x; out.y = y; }
    }
    static void output_mode(void* data, wl_output* o, uint32_t flags,
                            int32_t w, int32_t h, int32_t) {
        if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
        auto* d = static_cast<WaylandDisplay*>(data);
        for (auto& out : d->outputs_)
            if (out.output == o) { out.width = w; out.height = h; }
    }
    static void output_done(void*, wl_output*) {}
    static void output_scale(void* data, wl_output* o, int32_t factor) {
        auto* d = static_cast<WaylandDisplay*>(data);
        for (auto& out : d->outputs_)
            if (out.output == o) out.scale = factor;
    }
    static void output_name(void* data, wl_output* o, const char* name) {
        auto* d = static_cast<WaylandDisplay*>(data);
        for (auto& out : d->outputs_)
            if (out.output == o) out.name = name;
    }
    static void output_description(void*, wl_output*, const char*) {}

    // xdg_wm_base
    static void wm_base_ping(void*, xdg_wm_base* wm, uint32_t serial) {
        xdg_wm_base_pong(wm, serial);
    }

    // wl_registry (thunks so the private statics stay private)
    static void registry_global(void* data, wl_registry* reg, uint32_t name,
                                const char* iface, uint32_t version) {
        WaylandDisplay::on_global(data, reg, name, iface, version);
    }
    static void registry_global_remove(void* data, wl_registry* reg, uint32_t name) {
        WaylandDisplay::on_global_remove(data, reg, name);
    }

    // wl_data_source (clipboard selection we own)
    static void data_source_target(void*, wl_data_source*, const char*) {}
    static void data_source_send(void* data, wl_data_source*,
                                 const char* /*mime*/, int32_t fd) {
        // Another client is pasting: stream the selection text to its fd.
        // SIGPIPE is ignored (see set_clipboard_text), so a reader that
        // closes early surfaces as a write error, not a signal.
        const std::string& s = static_cast<WaylandDisplay*>(data)->selection_text_;
        size_t off = 0;
        while (off < s.size()) {
            ssize_t n = write(fd, s.data() + off, s.size() - off);
            if (n <= 0) break;
            off += static_cast<size_t>(n);
        }
        close(fd);
    }
    static void data_source_cancelled(void* data, wl_data_source* src) {
        // Selection replaced by another client (or by us): drop our source.
        auto* d = static_cast<WaylandDisplay*>(data);
        if (d->selection_source_ == src) d->selection_source_ = nullptr;
        wl_data_source_destroy(src);
    }
    static void data_source_dnd_drop_performed(void*, wl_data_source*) {}
    static void data_source_dnd_finished(void*, wl_data_source*) {}
    static void data_source_action(void*, wl_data_source*, uint32_t) {}
};

namespace {

const wl_pointer_listener kPointerListener = {
    WaylandListeners::pointer_enter,
    WaylandListeners::pointer_leave,
    WaylandListeners::pointer_motion,
    WaylandListeners::pointer_button,
    WaylandListeners::pointer_axis,
    WaylandListeners::pointer_frame,
    WaylandListeners::pointer_axis_source,
    WaylandListeners::pointer_axis_stop,
    WaylandListeners::pointer_axis_discrete,
};

const wl_keyboard_listener kKeyboardListener = {
    WaylandListeners::keyboard_keymap,
    WaylandListeners::keyboard_enter,
    WaylandListeners::keyboard_leave,
    WaylandListeners::keyboard_key,
    WaylandListeners::keyboard_modifiers,
    WaylandListeners::keyboard_repeat_info,
};

const wl_seat_listener kSeatListener = {
    WaylandListeners::seat_capabilities,
    WaylandListeners::seat_name,
};

const wl_output_listener kOutputListener = {
    WaylandListeners::output_geometry,
    WaylandListeners::output_mode,
    WaylandListeners::output_done,
    WaylandListeners::output_scale,
    WaylandListeners::output_name,
    WaylandListeners::output_description,
};

const xdg_wm_base_listener kWmBaseListener = {
    WaylandListeners::wm_base_ping,
};

const wl_data_source_listener kDataSourceListener = {
    WaylandListeners::data_source_target,
    WaylandListeners::data_source_send,
    WaylandListeners::data_source_cancelled,
    WaylandListeners::data_source_dnd_drop_performed,
    WaylandListeners::data_source_dnd_finished,
    WaylandListeners::data_source_action,
};

const wl_registry_listener kRegistryListener = {
    WaylandListeners::registry_global,
    WaylandListeners::registry_global_remove,
};

template <typename T>
T* bind(wl_registry* reg, uint32_t name, const wl_interface* iface,
        uint32_t version) {
    return static_cast<T*>(wl_registry_bind(reg, name, iface, version));
}

uint32_t min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }

}  // namespace

// ── registry ────────────────────────────────────────────────────────────────

void WaylandDisplay::on_global(void* data, wl_registry* reg, uint32_t name,
                               const char* iface, uint32_t version)
{
    auto* d = static_cast<WaylandDisplay*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        d->compositor_ = bind<wl_compositor>(reg, name, &wl_compositor_interface,
                                             min_u32(version, 4));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        d->wm_base_ = bind<xdg_wm_base>(reg, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(d->wm_base_, &kWmBaseListener, d);
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
        if (!d->seat_) d->bind_seat(name, version);
    } else if (std::strcmp(iface, wl_data_device_manager_interface.name) == 0) {
        d->data_device_manager_ = bind<wl_data_device_manager>(
            reg, name, &wl_data_device_manager_interface, min_u32(version, 3));
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        g_cursor.shm = bind<wl_shm>(reg, name, &wl_shm_interface, 1);
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
        WaylandOutput out;
        out.name_id = name;
        out.output  = bind<wl_output>(reg, name, &wl_output_interface,
                                      min_u32(version, 4));
        d->outputs_.push_back(out);
        wl_output_add_listener(d->outputs_.back().output, &kOutputListener, d);
    }
}

void WaylandDisplay::on_global_remove(void* data, wl_registry*, uint32_t name)
{
    auto* d = static_cast<WaylandDisplay*>(data);
    for (size_t i = 0; i < d->outputs_.size(); ++i) {
        if (d->outputs_[i].name_id == name) {
            wl_output_destroy(d->outputs_[i].output);
            d->outputs_.erase(d->outputs_.begin() + i);
            return;
        }
    }
}

void WaylandDisplay::bind_seat(uint32_t name, uint32_t version)
{
    // v5: complete pointer event set (frame/axis_*); v4 minimum gives
    // keyboard repeat_info. Older compositors still work — the listener
    // entries for events they never send just sit unused.
    seat_ = bind<wl_seat>(registry_, name, &wl_seat_interface,
                          min_u32(version, 5));
    wl_seat_add_listener(seat_, &kSeatListener, this);
}

void WaylandDisplay::update_seat_devices(uint32_t capabilities)
{
    bool want_pointer  = capabilities & WL_SEAT_CAPABILITY_POINTER;
    bool want_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

    if (want_pointer && !pointer_) {
        pointer_ = wl_seat_get_pointer(seat_);
        wl_pointer_add_listener(pointer_, &kPointerListener, this);
    } else if (!want_pointer && pointer_) {
        wl_pointer_destroy(pointer_);
        pointer_ = nullptr;
    }

    if (want_keyboard && !keyboard_) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        wl_keyboard_add_listener(keyboard_, &kKeyboardListener, this);
    } else if (!want_keyboard && keyboard_) {
        disarm_repeat();
        wl_keyboard_destroy(keyboard_);
        keyboard_ = nullptr;
    }
}

// ── lifecycle ───────────────────────────────────────────────────────────────

WaylandDisplay::WaylandDisplay()
{
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        std::fprintf(stderr, "[wayland] no compositor (WAYLAND_DISPLAY unset?)\n");
        return;
    }
    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);
    // Two roundtrips: one to learn the globals, one so their own initial
    // events (output modes, seat capabilities, keymap) arrive too.
    wl_display_roundtrip(display_);
    wl_display_roundtrip(display_);

    if (!compositor_ || !wm_base_)
        std::fprintf(stderr, "[wayland] compositor lacks wl_compositor/xdg_wm_base\n");

    repeat_timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
}

WaylandDisplay::~WaylandDisplay()
{
    disarm_repeat();
    if (repeat_timer_fd_ >= 0) close(repeat_timer_fd_);
    if (xkb_state_)   xkb_state_unref(xkb_state_);
    if (xkb_keymap_)  xkb_keymap_unref(xkb_keymap_);
    if (xkb_context_) xkb_context_unref(xkb_context_);
    if (g_cursor.surface) { wl_surface_destroy(g_cursor.surface); g_cursor.surface = nullptr; }
    if (g_cursor.theme)   { wl_cursor_theme_destroy(g_cursor.theme); g_cursor.theme = nullptr; }
    g_cursor = CursorState{};
    if (selection_source_)     wl_data_source_destroy(selection_source_);
    if (data_device_)          wl_data_device_destroy(data_device_);
    if (data_device_manager_)  wl_data_device_manager_destroy(data_device_manager_);
    if (pointer_)  wl_pointer_destroy(pointer_);
    if (keyboard_) wl_keyboard_destroy(keyboard_);
    if (seat_)     wl_seat_destroy(seat_);
    for (auto& out : outputs_) wl_output_destroy(out.output);
    if (wm_base_)    xdg_wm_base_destroy(wm_base_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (registry_)   wl_registry_destroy(registry_);
    if (display_)    wl_display_disconnect(display_);
}

const WaylandOutput* WaylandDisplay::find_output(wl_output* o) const
{
    for (const auto& out : outputs_)
        if (out.output == o) return &out;
    return nullptr;
}

void WaylandDisplay::set_sink(wl_surface* surface, InputSink* sink)
{
    if (sink) sinks_[surface] = sink;
    else      sinks_.erase(surface);
}

InputSink* WaylandDisplay::sink_for(wl_surface* surface) const
{
    auto it = sinks_.find(surface);
    return it == sinks_.end() ? nullptr : it->second;
}

// ── pointer ─────────────────────────────────────────────────────────────────

void WaylandDisplay::pointer_enter(wl_surface* s, double x, double y)
{
    pointer_focus_ = s;
    pointer_x_ = x;
    pointer_y_ = y;
    if (InputSink* sink = sink_for(s))
        sink->onPointer({PointerAction::Enter, (float)x, (float)y, 0});
}

void WaylandDisplay::pointer_leave(wl_surface* s)
{
    if (InputSink* sink = sink_for(s))
        sink->onPointer({PointerAction::Leave, 0, 0, 0});
    if (pointer_focus_ == s) pointer_focus_ = nullptr;
}

void WaylandDisplay::pointer_motion(double x, double y)
{
    pointer_x_ = x;
    pointer_y_ = y;
    if (InputSink* sink = sink_for(pointer_focus_))
        sink->onPointer({PointerAction::Move, (float)x, (float)y, 0});
}

void WaylandDisplay::pointer_button(uint32_t button, bool down)
{
    int b = 0;
    if      (button == BTN_LEFT)   b = 0;
    else if (button == BTN_RIGHT)  b = 1;
    else if (button == BTN_MIDDLE) b = 2;
    else return;
    if (InputSink* sink = sink_for(pointer_focus_))
        sink->onPointer({down ? PointerAction::Down : PointerAction::Up,
                         (float)pointer_x_, (float)pointer_y_, b});
}

void WaylandDisplay::pointer_axis(double value)
{
    // Wayland: positive = scroll down; one wheel detent ≈ 15. The engine
    // (like Win32) wants positive = up, ±1 per detent.
    if (InputSink* sink = sink_for(pointer_focus_))
        sink->onWheel({(float)pointer_x_, (float)pointer_y_,
                       (float)(-value / 15.0)});
}

// ── keyboard ────────────────────────────────────────────────────────────────

void WaylandDisplay::keyboard_keymap(int32_t fd, uint32_t size)
{
    disarm_repeat();
    if (xkb_state_)  { xkb_state_unref(xkb_state_);   xkb_state_  = nullptr; }
    if (xkb_keymap_) { xkb_keymap_unref(xkb_keymap_); xkb_keymap_ = nullptr; }

    char* mapped = static_cast<char*>(mmap(nullptr, size, PROT_READ,
                                           MAP_PRIVATE, fd, 0));
    if (mapped != MAP_FAILED) {
        xkb_keymap_ = xkb_keymap_new_from_string(xkb_context_, mapped,
                                                 XKB_KEYMAP_FORMAT_TEXT_V1,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapped, size);
    }
    close(fd);
    if (xkb_keymap_)
        xkb_state_ = xkb_state_new(xkb_keymap_);
}

void WaylandDisplay::keyboard_enter(wl_surface* s) { keyboard_focus_ = s; }

void WaylandDisplay::keyboard_leave(wl_surface* s)
{
    disarm_repeat();
    if (keyboard_focus_ == s) keyboard_focus_ = nullptr;
}

void WaylandDisplay::emit_key(uint32_t xkb_keycode, bool down)
{
    InputSink* sink = sink_for(keyboard_focus_);
    if (!sink || !xkb_state_) return;

    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state_, xkb_keycode);
    int code = keysym_to_keycode(sym);
    if (code)
        sink->onKey({code, down});
    if (down) {
        uint32_t cp = xkb_state_key_get_utf32(xkb_state_, xkb_keycode);
        if (cp)
            sink->onChar({cp});
    }
}

void WaylandDisplay::keyboard_key(uint32_t key, bool down)
{
    uint32_t xkb_keycode = key + 8;   // evdev -> xkb offset
    emit_key(xkb_keycode, down);

    if (down) {
        if (xkb_keymap_ && xkb_keymap_key_repeats(xkb_keymap_, xkb_keycode))
            arm_repeat(xkb_keycode);
    } else if (xkb_keycode == repeat_keycode_) {
        disarm_repeat();
    }
}

void WaylandDisplay::keyboard_modifiers(uint32_t depressed, uint32_t latched,
                                        uint32_t locked, uint32_t group)
{
    if (xkb_state_)
        xkb_state_update_mask(xkb_state_, depressed, latched, locked,
                              0, 0, group);
}

void WaylandDisplay::set_clipboard_text(const std::string& utf8)
{
    if (!data_device_manager_ || !seat_) return;   // compositor lacks the protocol

    // A paste request streams the text to a pipe; a reader that closes early
    // would otherwise SIGPIPE us. Ignore it once (benign for a GUI app).
    static bool sigpipe_ignored = false;
    if (!sigpipe_ignored) { std::signal(SIGPIPE, SIG_IGN); sigpipe_ignored = true; }

    if (!data_device_)
        data_device_ = wl_data_device_manager_get_data_device(
            data_device_manager_, seat_);

    // Replace any source we already own.
    if (selection_source_) {
        wl_data_source_destroy(selection_source_);
        selection_source_ = nullptr;
    }

    selection_text_   = utf8;
    selection_source_ = wl_data_device_manager_create_data_source(data_device_manager_);
    wl_data_source_add_listener(selection_source_, &kDataSourceListener, this);
    wl_data_source_offer(selection_source_, "text/plain;charset=utf-8");
    wl_data_source_offer(selection_source_, "text/plain");
    wl_data_device_set_selection(data_device_, selection_source_, last_serial_);
    if (display_) wl_display_flush(display_);
}

void WaylandDisplay::keyboard_repeat_info(int32_t rate, int32_t delay)
{
    repeat_rate_  = rate;    // keys/sec; 0 disables repeat (protocol)
    repeat_delay_ = delay;   // ms
}

// ── key repeat (client-side on Wayland) ─────────────────────────────────────

void WaylandDisplay::arm_repeat(uint32_t keycode)
{
    repeat_keycode_ = keycode;
    if (repeat_timer_fd_ < 0 || repeat_rate_ <= 0) return;
    itimerspec spec{};
    spec.it_value.tv_sec     = repeat_delay_ / 1000;
    spec.it_value.tv_nsec    = (repeat_delay_ % 1000) * 1000000L;
    long interval_ns         = 1000000000L / repeat_rate_;
    spec.it_interval.tv_sec  = interval_ns / 1000000000L;
    spec.it_interval.tv_nsec = interval_ns % 1000000000L;
    timerfd_settime(repeat_timer_fd_, 0, &spec, nullptr);
}

void WaylandDisplay::disarm_repeat()
{
    repeat_keycode_ = 0;
    if (repeat_timer_fd_ < 0) return;
    itimerspec spec{};   // zero = disarm
    timerfd_settime(repeat_timer_fd_, 0, &spec, nullptr);
}

void WaylandDisplay::fire_repeat(uint64_t expirations)
{
    if (!repeat_keycode_) return;
    // Collapse a backlog to a handful — a stalled frame shouldn't dump
    // hundreds of repeats into the buffer at once.
    if (expirations > 4) expirations = 4;
    for (uint64_t i = 0; i < expirations; ++i)
        emit_key(repeat_keycode_, true);
}

// ── the pump ────────────────────────────────────────────────────────────────

bool WaylandDisplay::dispatch(int timeout_ms)
{
    if (!display_) return false;

    while (wl_display_prepare_read(display_) != 0)
        wl_display_dispatch_pending(display_);
    wl_display_flush(display_);

    pollfd fds[3];
    fds[0] = { wl_display_get_fd(display_), POLLIN, 0 };
    fds[1] = { waker_.fd(),                 POLLIN, 0 };
    fds[2] = { repeat_timer_fd_,            POLLIN, 0 };
    int n = poll(fds, 3, timeout_ms);

    if (n > 0 && (fds[0].revents & POLLIN))
        wl_display_read_events(display_);
    else
        wl_display_cancel_read(display_);

    wl_display_dispatch_pending(display_);

    if (n > 0 && (fds[1].revents & POLLIN))
        waker_.drain();

    if (n > 0 && (fds[2].revents & POLLIN)) {
        uint64_t expirations = 0;
        if (read(repeat_timer_fd_, &expirations, sizeof expirations) == sizeof expirations)
            fire_repeat(expirations);
    }

    return wl_display_get_error(display_) == 0;
}

void WaylandDisplay::roundtrip()
{
    if (display_) wl_display_roundtrip(display_);
}
