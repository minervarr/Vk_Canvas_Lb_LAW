#pragma once
// Headless SurfaceProvider (platform.hh seam) backed by VK_EXT_headless_surface:
// a real VkSurfaceKHR + swapchain that present to nothing, so the whole
// Renderer path (swapchain, render pass, pipelines, draw) runs unchanged with
// no window and no compositor. Pair with Renderer::readbackLastFrame() to pull
// the rendered pixels back to the CPU (e.g. for the UI-capture tool).
//
// The extension is optional per-ICD; capture code should check availability
// first (headless_surface_supported()) for a readable error.

#include "platform.hh"

// True when the loader reports VK_EXT_headless_surface as an instance
// extension. Call before constructing a Renderer with a HeadlessSurfaceProvider.
bool headless_surface_supported();

class HeadlessSurfaceProvider : public SurfaceProvider {
public:
    HeadlessSurfaceProvider(uint32_t width, uint32_t height)
        : extent_{width, height} {}

    std::vector<const char*> instance_extensions() const override;
    VkSurfaceKHR create(VkInstance instance) override;
    VkExtent2D   extent() const override { return extent_; }

private:
    VkExtent2D extent_;
};
