#pragma once
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/asset_manager.h>
#include <android/native_window.h>
#include <vector>
#include <cstdint>

#include "vulkan_canvas_engine/vulkan_font_engine/app/src/main/cpp/renderer.hh"
#include "vulkan_canvas_engine/vulkan_font_engine/app/src/main/cpp/msdf.hh"
#include "font_weight.hh"

class VulkanState {
public:
    void init(ANativeWindow* window, AAssetManager* mgr);
    // Load one weight's atlas. weightIdx = 0..kFontWeightCount-1.
    void initMsdf(const MsdfFont& msdf, int weightIdx = 0);
    void cleanup();

    // Render one frame. Per-weight quad arrays; null entries are skipped.
    //   quads[w]        — MSDF vertex data in DOCUMENT space for weight w
    //   quadVerts[w]    — vertex count for weight w (quads[w].size() / 8)
    //   content_changed — true when any weight's quads need re-uploading
    //   scroll_x/y      — viewport scroll offset applied GPU-side
    void draw(const std::vector<float>* quads, const uint32_t* quadVerts,
              bool content_changed, float scroll_x, float scroll_y);

    uint32_t width()  const { return extent_.width;  }
    uint32_t height() const { return extent_.height; }
    bool     msdfReady(int weightIdx = 0) const { return renderer_.msdfReady(weightIdx); }

private:
    static constexpr uint32_t kFrames = 2;   // frames in flight

    AAssetManager*   mgr_       = nullptr;
    VkInstance       instance_  = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_   = VK_NULL_HANDLE;
    VkPhysicalDevice physDev_   = VK_NULL_HANDLE;
    uint32_t         gfxFamily_ = UINT32_MAX;
    uint32_t         presFamily_= UINT32_MAX;
    VkDevice         device_    = VK_NULL_HANDLE;
    VkQueue          gfxQueue_  = VK_NULL_HANDLE;
    VkQueue          presQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain_ = VK_NULL_HANDLE;
    VkFormat         swapFmt_   = VK_FORMAT_UNDEFINED;
    VkExtent2D       extent_    = {};
    std::vector<VkImage>       swapImages_;
    std::vector<VkImageView>   swapViews_;
    VkRenderPass     renderPass_  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    // Composite pipeline (kept for future non-MSDF use)
    VkDescriptorSetLayout compSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      compPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       compSet_       = VK_NULL_HANDLE;
    VkPipelineLayout      compLayout_    = VK_NULL_HANDLE;
    VkPipeline            compPipeline_  = VK_NULL_HANDLE;

    // Per-frame resources (double-buffered to eliminate CPU↔GPU sync stall)
    VkCommandPool   cmdPool_             = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf_[kFrames]     = {};
    VkSemaphore     imgReady_[kFrames]   = {};
    VkSemaphore     rendDone_[kFrames]   = {};
    VkFence         fence_[kFrames]      = {};
    uint32_t        frameIdx_            = 0;

    Renderer        renderer_;
    VkImageLayout   outLayout_ = VK_IMAGE_LAYOUT_GENERAL;

    VkShaderModule loadShader(const char* assetPath);
};
