#include "wayland_platform.hh"

#include "wayland_display.hh"
#include "wayland_window.hh"

#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdio>

// ── FileAssetReader ─────────────────────────────────────────────────────────

FileAssetReader::FileAssetReader()
{
    char exe[4096]{};
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    root_ = (n > 0) ? std::string(exe, (size_t)n) : std::string();
    size_t slash = root_.find_last_of('/');
    root_ = (slash == std::string::npos) ? "" : root_.substr(0, slash + 1);
    root_ += "assets/";
}

bool FileAssetReader::read(const char* path, std::vector<uint8_t>& out)
{
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

// ── EventFdFrameWaker ───────────────────────────────────────────────────────

EventFdFrameWaker::EventFdFrameWaker()
{
    fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
}

EventFdFrameWaker::~EventFdFrameWaker()
{
    if (fd_ >= 0) close(fd_);
}

void EventFdFrameWaker::wake()
{
    if (fd_ < 0) return;
    uint64_t one = 1;
    ssize_t ignored = write(fd_, &one, sizeof one);
    (void)ignored;
}

void EventFdFrameWaker::drain()
{
    if (fd_ < 0) return;
    uint64_t count = 0;
    ssize_t ignored = read(fd_, &count, sizeof count);
    (void)ignored;
}

// ── WaylandSurfaceProvider ──────────────────────────────────────────────────

std::vector<const char*> WaylandSurfaceProvider::instance_extensions() const
{
    return { "VK_KHR_surface", "VK_KHR_wayland_surface" };
}

VkSurfaceKHR WaylandSurfaceProvider::create(VkInstance instance)
{
    VkWaylandSurfaceCreateInfoKHR ci{};
    ci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    ci.display = display_.display();
    ci.surface = window_.surface();

    auto fn = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateWaylandSurfaceKHR"));
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!fn || fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}

VkExtent2D WaylandSurfaceProvider::extent() const
{
    return window_.extent();
}
