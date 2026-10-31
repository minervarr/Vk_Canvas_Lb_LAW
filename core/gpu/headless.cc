#include "headless.hh"

#include <cstring>
#include <vector>

bool headless_surface_supported()
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, "VK_EXT_headless_surface") == 0)
            return true;
    return false;
}

std::vector<const char*> HeadlessSurfaceProvider::instance_extensions() const
{
    return { "VK_KHR_surface", "VK_EXT_headless_surface" };
}

VkSurfaceKHR HeadlessSurfaceProvider::create(VkInstance instance)
{
    auto fn = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
    if (!fn) return VK_NULL_HANDLE;

    VkHeadlessSurfaceCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (fn(instance, &ci, nullptr, &surface) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return surface;
}
