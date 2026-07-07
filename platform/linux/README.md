# platform/linux — Wayland backend (planned, late-game)

Native Wayland, no X11/Qt/GTK. Will provide the three `core/platform.hh` seams:

- `WaylandSurfaceProvider` — reports `VK_KHR_surface` + `VK_KHR_wayland_surface`,
  creates via `vkCreateWaylandSurfaceKHR`; owns the `wl_display`/`wl_surface`
  plus xdg-shell (`xdg_wm_base` / `xdg_toplevel`) setup and client-side
  decoration handling
- `FileAssetReader` — reads `assets/` relative to the executable (shareable
  with the Windows backend if duplication ever hurts)
- `FrameWaker` — `eventfd` written from other threads, polled in the event loop

Deliberately unimplemented until the Windows backend has validated the seams.
