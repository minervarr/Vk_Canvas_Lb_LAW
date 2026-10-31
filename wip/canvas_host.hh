// canvas_host.hh — camera-free Vulkan host for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
//
// ---------------------------------------------------------------------------
// CanvasHost owns a full Vulkan stack (instance, surface, device, swapchain,
// render pass, command buffer, sync) and an OverlayRasterizer, and presents one
// frame from a finished Canvas curve buffer. Unlike the engine's camera-derived
// Renderer, it composites the UI over a plain cleared background — the minimal
// "just draw a 2D canvas app" host. It is application-agnostic: it knows nothing
// about what the curves represent.
//
// Usage:
//   CanvasHost host;
//   host.init(window, assetManager);
//   // per dirty frame:
//   std::vector<float> curves;                 // reused buffer
//   Canvas c(curves, host.width(), host.height(), &font, 0,0,0,0);
//   c.clear(col::bg); c.text(...); ...
//   host.renderCurves(curves);
// ---------------------------------------------------------------------------
#pragma once
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/native_window.h>
#include <android/asset_manager.h>
#include <cstdint>
#include <vector>

#include "overlay.hh"
#include "msdf.hh"

class CanvasHost {
public:
    // Bring up the whole Vulkan stack against `window`. Idempotent-safe to call
    // once per surface; pair with cleanup() on APP_CMD_TERM_WINDOW.
    void init(ANativeWindow* window, AAssetManager* assetManager, const char* cachePath = nullptr);
    void cleanup();

    bool     ready()  const { return ready_; }
    uint32_t width()  const { return extent_.width;  }
    uint32_t height() const { return extent_.height; }

    // Upload one frame's geometry and present it:
    //   curves : OverlayRasterizer curve records (backgrounds, rects, SDF shapes);
    //            length must be a multiple of OverlayRasterizer::CURVE_FLOATS.
    //   quads  : MSDF text vertices (8 floats/vert) from Canvas::useMsdf, drawn on
    //            top of the curves. Pass an empty vector when not using MSDF text.
    void renderFrame(const std::vector<float>& curves, const std::vector<float>& quads);

    // The MSDF font (metrics + atlas) loaded at init, or nullptr if unavailable.
    // Hand to Canvas::useMsdf so text becomes textured quads instead of per-frame
    // Bézier winding fills — far cheaper, and free of the winding-row artifacts.
    const MsdfFont* msdfFont() const { return msdfReady_ ? &msdf_ : nullptr; }

private:
    bool ready_ = false;

    AAssetManager*   assetManager_ = nullptr;
    VkInstance       instance_     = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_      = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDev_  = VK_NULL_HANDLE;
    uint32_t         gfxFamily_    = UINT32_MAX;
    uint32_t         presentFamily_= UINT32_MAX;
    VkDevice         device_       = VK_NULL_HANDLE;
    VkQueue          gfxQueue_     = VK_NULL_HANDLE;
    VkQueue          presentQueue_ = VK_NULL_HANDLE;

    VkSwapchainKHR   swapchain_    = VK_NULL_HANDLE;
    VkFormat         swapFormat_   = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent_       = {};
    std::vector<VkImage>       images_;
    std::vector<VkImageView>   imageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    VkRenderPass     renderPass_   = VK_NULL_HANDLE;
    VkCommandPool    cmdPool_      = VK_NULL_HANDLE;
    VkCommandBuffer  cmdBuffer_    = VK_NULL_HANDLE;
    VkSemaphore      imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore      renderFinished_ = VK_NULL_HANDLE;
    VkFence          inFlight_     = VK_NULL_HANDLE;

    OverlayRasterizer overlay_;

    // ── MSDF text path ─────────────────────────────────────────────────────
    MsdfFont       msdf_;
    bool           msdfReady_   = false;
    std::string              msdfCachePath_;
    float                    msdfPxRange_ = 4.0f, msdfAtlasW_ = 0.0f, msdfAtlasH_ = 0.0f;
    VkImage        msdfImage_    = VK_NULL_HANDLE;
    VkDeviceMemory msdfImageMem_ = VK_NULL_HANDLE;
    VkImageView    msdfView_     = VK_NULL_HANDLE;
    VkSampler      msdfSampler_  = VK_NULL_HANDLE;
    VkDescriptorSetLayout msdfSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      msdfPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       msdfSet_       = VK_NULL_HANDLE;
    VkPipelineLayout msdfPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline       msdfPipe_       = VK_NULL_HANDLE;
    VkBuffer         msdfVbo_        = VK_NULL_HANDLE;
    VkDeviceMemory   msdfVboMem_     = VK_NULL_HANDLE;
    void*            msdfVboMapped_  = nullptr;
    uint32_t         msdfVertCount_  = 0;
    static constexpr uint32_t kMaxMsdfVerts = 16384;

    bool createInstance();
    bool createSurface(ANativeWindow* window);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandsAndSync();

    bool createMsdf();
    void drawMsdf(VkCommandBuffer cmd);
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required);
    bool allocBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);
};
