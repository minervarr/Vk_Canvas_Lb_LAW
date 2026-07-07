#pragma once
// Win32 implementations of the platform seams (platform.hh, input.hh):
// assets from an exe-relative directory, surface from an HWND, input from
// WM_LBUTTONDOWN/UP/MOUSEMOVE/MOUSEWHEEL/KEYDOWN/KEYUP.

#include "platform.hh"
#include "input.hh"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

// Reads assets from <exe dir>/assets/, e.g. "shaders/tiling.spv".
class FileAssetReader : public AssetReader {
public:
    FileAssetReader();  // roots itself at the running executable's directory
    bool read(const char* path, std::vector<uint8_t>& out) override;

private:
    std::string root_;
};

class Win32SurfaceProvider : public SurfaceProvider {
public:
    explicit Win32SurfaceProvider(HWND hwnd) : hwnd_(hwnd) {}
    std::vector<const char*> instance_extensions() const override;
    VkSurfaceKHR create(VkInstance instance) override;
    VkExtent2D   extent() const override;

private:
    HWND hwnd_;
};

// Translates one Win32 window message into InputSink calls, if it's an input
// message this seam covers (WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN/UP,
// WM_MBUTTONDOWN/UP, WM_MOUSEMOVE, WM_MOUSEWHEEL, WM_KEYDOWN/UP). Returns true
// if the message was handled (caller should treat it like any other handled
// message — e.g. still fine to fall through to DefWindowProc for most of
// these, since they don't need default processing). Call from your WndProc
// before your own switch, or alongside it.
//
// WM_MOUSEMOVE synthesizes Enter/Leave via TrackMouseEvent — call this for
// every message (not just mouse messages) so the internal "are we currently
// tracking leave for this hwnd" state stays correct across WM_MOUSELEAVE.
bool win32_translate_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, InputSink& sink);
