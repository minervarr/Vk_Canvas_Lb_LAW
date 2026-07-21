#pragma once
// Wayland implementations of the platform seams (platform.hh): assets from an
// exe-relative directory, Vulkan surface from a wl_surface, wake via eventfd.
//
// Raw protocol only: libwayland-client + libxkbcommon — the Wayland
// equivalents of user32.dll, not toolkits. The connection/window/input
// machinery lives in wayland_display.hh / wayland_window.hh; this header is
// just the platform.hh seam impls, mirroring win32_platform.hh.

#include "platform.hh"

#include <string>

class WaylandDisplay;
class WaylandWindow;

// Reads assets from <exe dir>/assets/, e.g. "shaders/tiling.spv".
class FileAssetReader : public AssetReader {
public:
    FileAssetReader();  // roots itself at /proc/self/exe's directory
    bool read(const char* path, std::vector<uint8_t>& out) override;

private:
    std::string root_;
};

// eventfd written from any thread, polled by WaylandDisplay::dispatch() —
// exactly the FrameWaker shape ALooper_wake/PostMessage fill elsewhere.
class EventFdFrameWaker : public FrameWaker {
public:
    EventFdFrameWaker();
    ~EventFdFrameWaker() override;
    void wake() override;

    int  fd() const { return fd_; }
    void drain();   // reset after a poll wake-up (dispatch() calls this)

private:
    int fd_ = -1;
};

class WaylandSurfaceProvider : public SurfaceProvider {
public:
    WaylandSurfaceProvider(WaylandDisplay& display, WaylandWindow& window)
        : display_(display), window_(window) {}
    std::vector<const char*> instance_extensions() const override;
    VkSurfaceKHR create(VkInstance instance) override;
    VkExtent2D   extent() const override;   // buffer px (logical size × scale)

private:
    WaylandDisplay& display_;
    WaylandWindow&  window_;
};
