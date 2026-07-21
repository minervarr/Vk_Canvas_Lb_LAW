#pragma once
// One Wayland connection: registry globals, outputs, seat input, event pump.
//
// The pump (dispatch()) is the MsgWaitForMultipleObjects equivalent: it
// sleeps on {display fd, waker eventfd, key-repeat timerfd} up to a timeout,
// then dispatches whatever arrived. Seat events are translated into the
// engine's InputSink (input.hh) — the counterpart of win32_translate_input —
// routed per focused wl_surface: hosts register a sink per surface they care
// about (set_sink); surfaces without a sink drop input (that is how a
// read-only mirror window ignores clicks). Key codes are mapped from xkb
// keysyms into the keys.hh space; key repeat is client-side on Wayland, so
// the pump re-emits the held key from a timerfd armed per wl_keyboard's
// repeat_info.

#include "input.hh"
#include "wayland_platform.hh"

#include <wayland-client.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct xdg_wm_base;   // generated xdg-shell-client-protocol.h (linked in .cc)

struct WaylandOutput {
    wl_output*  output = nullptr;
    uint32_t    name_id = 0;       // registry name, for hotplug removal
    int32_t     x = 0, y = 0;      // position in the compositor's layout
    int32_t     width = 0, height = 0;  // current mode, physical px
    int32_t     scale = 1;
    std::string name;              // connector name, e.g. "HDMI-A-1" (v4+)
};

class WaylandDisplay {
public:
    WaylandDisplay();   // connect + bind globals; check valid() after
    ~WaylandDisplay();

    WaylandDisplay(const WaylandDisplay&) = delete;
    WaylandDisplay& operator=(const WaylandDisplay&) = delete;

    bool valid() const { return display_ && compositor_ && wm_base_; }

    wl_display*    display()    const { return display_; }
    wl_compositor* compositor() const { return compositor_; }
    xdg_wm_base*   wm_base()    const { return wm_base_; }

    // Outputs in bind order (Wayland has no "primary"; index 0 by convention).
    const std::vector<WaylandOutput>& outputs() const { return outputs_; }
    const WaylandOutput* find_output(wl_output* o) const;

    // Route input events from `surface` to `sink` (null unregisters).
    void set_sink(wl_surface* surface, InputSink* sink);

    FrameWaker& waker() { return waker_; }

    // Pump: sleep up to timeout_ms for display events / waker / key repeat,
    // dispatch them. Returns false when the connection died (compositor gone
    // or protocol error) — the host should quit.
    bool dispatch(int timeout_ms);

    void roundtrip();

private:
    // registry
    static void on_global(void* data, wl_registry* reg, uint32_t name,
                          const char* iface, uint32_t version);
    static void on_global_remove(void* data, wl_registry* reg, uint32_t name);

    void bind_seat(uint32_t name, uint32_t version);
    void update_seat_devices(uint32_t capabilities);

    InputSink* sink_for(wl_surface* surface) const;

    // seat handlers (members called from static listener thunks in the .cc)
    void pointer_enter(wl_surface* s, double x, double y);
    void pointer_leave(wl_surface* s);
    void pointer_motion(double x, double y);
    void pointer_button(uint32_t button, bool down);
    void pointer_axis(double value);   // vertical, wayland sign (positive=down)
    void keyboard_keymap(int32_t fd, uint32_t size);
    void keyboard_enter(wl_surface* s);
    void keyboard_leave(wl_surface* s);
    void keyboard_key(uint32_t key, bool down);
    void keyboard_modifiers(uint32_t depressed, uint32_t latched,
                            uint32_t locked, uint32_t group);
    void keyboard_repeat_info(int32_t rate, int32_t delay);

    void arm_repeat(uint32_t keycode);
    void disarm_repeat();
    void fire_repeat(uint64_t expirations);
    void emit_key(uint32_t xkb_keycode, bool down);

    wl_display*    display_    = nullptr;
    wl_registry*   registry_   = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base*   wm_base_    = nullptr;

    wl_seat*     seat_     = nullptr;
    wl_pointer*  pointer_  = nullptr;
    wl_keyboard* keyboard_ = nullptr;

    std::vector<WaylandOutput> outputs_;

    std::unordered_map<wl_surface*, InputSink*> sinks_;
    wl_surface* pointer_focus_  = nullptr;
    wl_surface* keyboard_focus_ = nullptr;
    double      pointer_x_ = 0.0, pointer_y_ = 0.0;

    // xkbcommon state (opaque pointers; real types only in the .cc)
    struct xkb_context* xkb_context_ = nullptr;
    struct xkb_keymap*  xkb_keymap_  = nullptr;
    struct xkb_state*   xkb_state_   = nullptr;

    // client-side key repeat
    int      repeat_timer_fd_ = -1;
    int32_t  repeat_rate_     = 25;   // chars/sec; wl_keyboard.repeat_info
    int32_t  repeat_delay_    = 400;  // ms
    uint32_t repeat_keycode_  = 0;    // xkb keycode currently repeating (0 = none)

    EventFdFrameWaker waker_;

    friend struct WaylandListeners;   // the .cc's static listener thunks
};
