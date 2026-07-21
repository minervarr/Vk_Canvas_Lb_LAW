# platform/linux — raw-Wayland backend

Native Wayland, no X11/Qt/GTK/GLFW/libdecor. Only the protocol-level system
libraries: `wayland-client` (wire marshalling — the `user32.dll` of Wayland),
`wayland-cursor` (cursor-theme loader shipped with wayland itself), and
`xkbcommon` (keymap interpretation — what `TranslateMessage` did on Win32).
Built as the `vk_canvas_wayland` static library (see CMakeLists.txt); the
xdg-shell C stubs are generated at build time by `wayland-scanner` from the
system `wayland-protocols` XML.

What each file provides:

- `wayland_platform.hh/cc` — the `core/platform.hh` seams:
  `FileAssetReader` (assets/ next to the executable, via `/proc/self/exe`),
  `WaylandSurfaceProvider` (`VK_KHR_wayland_surface` +
  `vkCreateWaylandSurfaceKHR`; extent = the window's configured size × buffer
  scale), `EventFdFrameWaker` (eventfd written from any thread, polled by the
  pump).
- `wayland_display.hh/cc` — one connection: registry globals (compositor,
  `xdg_wm_base` ping/pong, seat, shm, outputs), output list (position/mode/
  scale/name; Wayland has no "primary" — index 0 by convention), and
  `dispatch(timeout_ms)`, the `MsgWaitForMultipleObjects` equivalent: poll on
  {display fd, waker eventfd, key-repeat timerfd}. Seat events are translated
  into the engine's `InputSink` (`core/input.hh`) — the counterpart of
  `win32_translate_input` — routed by focused surface via `set_sink()`;
  surfaces without a sink drop input (how a read-only mirror window ignores
  clicks). Key codes are xkb keysyms mapped into `core/keys.hh`'s space; key
  repeat is client-side on Wayland, re-emitted from a timerfd armed per the
  compositor's `repeat_info`.
- `wayland_window.hh/cc` — one xdg toplevel. A Wayland window has no size
  until the first configure, so the constructor commits + roundtrips until
  configured; only then may a `Renderer` be built on it. `set_fullscreen()`
  (no decorations needed — ideal for kiosks; wlroots draws none anyway),
  `closed()`/`take_resized()` flags (the latter feeds
  `Renderer::notifyResized()`), integer buffer-scale handling from the
  outputs the surface enters.

Consumer loop shape (same as every backend):

```
WaylandDisplay display;                       // check display.valid()
WaylandWindow  window(display, "title", "app.id", w, h);
WaylandSurfaceProvider provider(display, window);
FileAssetReader assets;
Renderer renderer(provider, assets, 3);       // desktop: 3 images
display.set_sink(window.surface(), &frameInput);
while (!window.closed()) {
    frameInput.beginFrame();
    if (!display.dispatch(timeout_ms)) break; // connection died
    if (window.take_resized()) renderer.notifyResized();
    ... layout + canvas + renderer.draw(...) ...
}
```
