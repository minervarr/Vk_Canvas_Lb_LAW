#include "android_platform.hh"

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

bool AndroidAssetReader::read(const char* path, std::vector<uint8_t>& out) {
    AAsset* asset = AAssetManager_open(mgr_, path, AASSET_MODE_BUFFER);
    if (!asset) return false;
    size_t size = static_cast<size_t>(AAsset_getLength(asset));
    out.resize(size);
    int64_t got = AAsset_read(asset, out.data(), size);
    AAsset_close(asset);
    return got == static_cast<int64_t>(size);
}

std::vector<const char*> AndroidSurfaceProvider::instance_extensions() const {
    return { "VK_KHR_surface", "VK_KHR_android_surface" };
}

VkSurfaceKHR AndroidSurfaceProvider::create(VkInstance instance) {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window_;

    auto fn = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!fn || fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}

VkExtent2D AndroidSurfaceProvider::extent() const {
    return { static_cast<uint32_t>(ANativeWindow_getWidth(window_)),
             static_cast<uint32_t>(ANativeWindow_getHeight(window_)) };
}
