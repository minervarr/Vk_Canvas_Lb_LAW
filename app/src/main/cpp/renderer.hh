#pragma once
#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include "canvas.hh"

class Renderer {
public:
    explicit Renderer(ANativeWindow* window);
    ~Renderer();

    void draw(const Canvas& canvas);

    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }

private:
    uint32_t width_  = 0;
    uint32_t height_ = 0;

    VkInstance       instance_       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice physical_dev_   = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain_      = VK_NULL_HANDLE;

    void create_instance();
    void create_surface(ANativeWindow* window);
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void cleanup();
};
