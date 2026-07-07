#pragma once
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <android/asset_manager.h>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// OverlayRasterizer: renders a Canvas's curve records (20-float Bézier records,
// produced by canvas.cc) into a screen-sized RGBA image via the two-pass
// tiling + coverage compute shaders, then composites that image over whatever
// is already in the render pass (e.g. the camera frame) using premultiplied-
// alpha blending. This is the reusable "draw the canvas" half of the engine,
// kept as its own class so it can attach to any existing VkDevice/render pass
// (here: the camera Renderer) without colliding with that class.
//
// The coverage pass writes premultiplied alpha and leaves untouched pixels at
// (0,0,0,0), so the overlay is transparent everywhere the UI didn't draw.
// ---------------------------------------------------------------------------
class OverlayRasterizer {
public:
    static constexpr uint32_t MAX_CURVES           = 8192;
    static constexpr uint32_t CURVE_FLOATS         = 20;
    static constexpr uint32_t TILE_SIZE            = 16;
    // Per-tile capacity. Overflow drops a RANDOM subset (the tiling pass grabs
    // slots with atomics in parallel), so headroom matters more than memory:
    // dense plot scenes (curves coiled into a few tiles at far zoom-out) plus
    // panels must fit or later-drawn UI vanishes in those tiles.
    static constexpr uint32_t MAX_CURVES_PER_TILE  = 96;
    static constexpr uint32_t TILE_STRIDE_U32      = MAX_CURVES_PER_TILE + 1;
    static constexpr uint32_t MAX_WINDING_PER_TILE = 64;
    static constexpr uint32_t WIND_STRIDE_U32      = MAX_WINDING_PER_TILE + 1;

    // Attach to an existing device + render pass; create all GPU resources.
    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              AAssetManager* assetManager, VkRenderPass renderPass,
              uint32_t width, uint32_t height);
    void cleanup();

    bool ready() const { return coveragePipeline_ != VK_NULL_HANDLE &&
                                compPipeline_ != VK_NULL_HANDLE; }

    // Copy curve records (count = number of 20-float records) into the mapped
    // curve buffer for this frame.
    void uploadCurves(const float* curveData, uint32_t count);

    // Record (BEFORE the render pass): clear tile/row buffers, run tiling +
    // coverage compute into the overlay image, then barrier it to be sampled.
    void recordDispatch(VkCommandBuffer cmd);

    // Record (INSIDE the render pass, after the camera quad): draw a fullscreen
    // triangle sampling the overlay image, premultiplied-alpha blended. The
    // overlay is rotated by rotation_deg (0/90/180/270) about its centre so it
    // stays upright to the user as the device turns.
    void recordComposite(VkCommandBuffer cmd, int rotation_deg);

private:
    VkDevice         device_         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    AAssetManager*   assetManager_   = nullptr;
    uint32_t         width_  = 0;
    uint32_t         height_ = 0;
    uint32_t         curveCount_ = 0;
    uint32_t         hasWinding_ = 0;   // 1 if any uploaded curve is type 4/5/6

    // Compute (curve rasterisation) resources
    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;
    VkDescriptorSet       descSet_    = VK_NULL_HANDLE;

    VkBuffer       curveBuffer_       = VK_NULL_HANDLE;
    VkDeviceMemory curveBufferMemory_ = VK_NULL_HANDLE;
    void*          curveBufferMapped_ = nullptr;
    VkBuffer       tileBuffer_        = VK_NULL_HANDLE;
    VkDeviceMemory tileBufferMemory_  = VK_NULL_HANDLE;
    VkBuffer       rowBuffer_         = VK_NULL_HANDLE;
    VkDeviceMemory rowBufferMemory_   = VK_NULL_HANDLE;

    VkImage        outputImage_       = VK_NULL_HANDLE;
    VkDeviceMemory outputImageMemory_ = VK_NULL_HANDLE;
    VkImageView    outputImageView_   = VK_NULL_HANDLE;
    VkSampler      outputSampler_     = VK_NULL_HANDLE;

    VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       tilingPipeline_        = VK_NULL_HANDLE;
    VkPipeline       coveragePipeline_      = VK_NULL_HANDLE;

    // Composite (overlay-over-camera) graphics resources
    VkDescriptorSetLayout compSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      compPool_      = VK_NULL_HANDLE;
    VkDescriptorSet       compSet_       = VK_NULL_HANDLE;
    VkPipelineLayout      compLayout_    = VK_NULL_HANDLE;
    VkPipeline            compPipeline_  = VK_NULL_HANDLE;

    void createDescriptorLayoutAndPool();
    void createBuffers();
    void createOutputImage();
    void createComputePipelines();
    void writeDescriptors();
    void createCompositePipeline(VkRenderPass renderPass);

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required);
    VkShaderModule loadShader(const char* path);

    std::vector<float> lastCurves_;
    bool curvesDirty_ = true;
    bool firstRun_    = true;
};
