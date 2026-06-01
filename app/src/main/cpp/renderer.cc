#include "renderer.hh"
#include <android/log.h>
#include <stdexcept>

#define LOG_TAG "vk_canvas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

Renderer::Renderer(ANativeWindow* window) {
    width_  = static_cast<uint32_t>(ANativeWindow_getWidth(window));
    height_ = static_cast<uint32_t>(ANativeWindow_getHeight(window));
    create_instance();
    create_surface(window);
    pick_physical_device();
    create_logical_device();
    create_swapchain();
    LOGI("Renderer ready (%ux%u)", width_, height_);
}

Renderer::~Renderer() {
    cleanup();
}

void Renderer::draw(const Canvas& /*canvas*/) {
    // TODO: record command buffer, submit, present
}

void Renderer::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vk_canvas";
    app_info.apiVersion       = VK_API_VERSION_1_1;

    const char* extensions[] = {
        "VK_KHR_surface",
        "VK_KHR_android_surface",
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app_info;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = extensions;

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");
}

void Renderer::create_surface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;

    auto fn = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        vkGetInstanceProcAddr(instance_, "vkCreateAndroidSurfaceKHR"));
    if (!fn || fn(instance_, &ci, nullptr, &surface_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateAndroidSurfaceKHR failed");
}

void Renderer::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance_, &count, devs.data());
    physical_dev_ = devs[0];
}

void Renderer::create_logical_device() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    const char* dev_exts[] = { "VK_KHR_swapchain" };

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = dev_exts;

    if (vkCreateDevice(physical_dev_, &ci, nullptr, &device_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(device_, 0, 0, &queue_);
}

void Renderer::create_swapchain() {
    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = 2;
    ci.imageFormat      = VK_FORMAT_R8G8B8A8_UNORM;
    ci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent      = { width_, height_ };
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");
}

void Renderer::cleanup() {
    if (device_)    { vkDeviceWaitIdle(device_); }
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (device_)    vkDestroyDevice(device_, nullptr);
    if (surface_)   vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)  vkDestroyInstance(instance_, nullptr);
}
