#include "win32_platform.hh"

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <cstdio>

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
