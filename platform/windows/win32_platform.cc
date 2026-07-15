#include "win32_platform.hh"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <windowsx.h>  // GET_X_LPARAM/GET_Y_LPARAM/GET_WHEEL_DELTA_WPARAM

#include <cstdio>
#include <unordered_map>

FileAssetReader::FileAssetReader() {
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    root_ = exe;
    size_t slash = root_.find_last_of("\\/");
    root_ = (slash == std::string::npos) ? "" : root_.substr(0, slash + 1);
    root_ += "assets\\";
}

bool FileAssetReader::read(const char* path, std::vector<uint8_t>& out) {
    std::string full = root_ + path;
    FILE* f = std::fopen(full.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

std::vector<const char*> Win32SurfaceProvider::instance_extensions() const {
    return { "VK_KHR_surface", "VK_KHR_win32_surface" };
}

VkSurfaceKHR Win32SurfaceProvider::create(VkInstance instance) {
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd      = hwnd_;

    auto fn = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!fn || fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}

VkExtent2D Win32SurfaceProvider::extent() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    return { static_cast<uint32_t>(rc.right - rc.left),
             static_cast<uint32_t>(rc.bottom - rc.top) };
}

namespace {
// Per-HWND "are we currently subscribed to WM_MOUSELEAVE" flag — TrackMouseEvent
// resets itself after each leave, so it must be re-armed on every fresh
// WM_MOUSEMOVE that follows one.
std::unordered_map<HWND, bool> g_tracking_leave;
}  // namespace

bool win32_translate_input(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, InputSink& sink) {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN: {
            int button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
            sink.onPointer({PointerAction::Down, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), button});
            return true;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP: {
            int button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
            sink.onPointer({PointerAction::Up, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), button});
            return true;
        }
        case WM_MOUSEMOVE: {
            if (!g_tracking_leave[hwnd]) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize    = sizeof(tme);
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                g_tracking_leave[hwnd] = true;
                sink.onPointer({PointerAction::Enter, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 0});
            }
            sink.onPointer({PointerAction::Move, (float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 0});
            return true;
        }
        case WM_MOUSELEAVE: {
            g_tracking_leave[hwnd] = false;
            sink.onPointer({PointerAction::Leave, 0, 0, 0});
            return true;
        }
        case WM_MOUSEWHEEL: {
            // Wheel messages carry SCREEN coordinates, unlike every other
            // mouse message here — convert to client space for consistency.
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            sink.onWheel({(float)pt.x, (float)pt.y,
                         (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA});
            return true;
        }
        case WM_KEYDOWN:
        case WM_KEYUP:
            sink.onKey({(int)wp, msg == WM_KEYDOWN});
            return true;
        case WM_CHAR:
            // wp is a UTF-16 code unit; surrogate pairs (non-BMP input) arrive
            // as two WM_CHARs and are forwarded as-is — BMP-only consumers
            // (ASCII filters like FrameInput's) are unaffected.
            sink.onChar({(uint32_t)wp});
            return true;
        default:
            return false;
    }
}
