#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "platform.hh"
#include "texture.hh"

// ImageLayer: uploads app-supplied RGBA pixel buffers as sampled textures and
// draws them as textured quads (album art, icons) — the raster counterpart to
// OverlayRasterizer's vector/text path. Textures are drawn as a background
// layer inside the same render pass, before the vector overlay composites on
// top, so icons/text always sit visually above artwork.
class ImageLayer {
public:
    // Attach to an existing device + render pass. cmdPool/queue are reused
    // (borrowed, not owned) for the one-shot upload command buffer in
    // create_texture() — Renderer already owns a graphics command pool/queue
    // by the time this is called.
    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              AssetReader& assets, VkRenderPass renderPass,
              VkCommandPool cmdPool, VkQueue queue);
    void cleanup();

    bool ready() const { return pipeline_ != VK_NULL_HANDLE; }

    // Uploads `rgba` (w*h*4 bytes, straight alpha, row-major top-to-bottom) as
    // a new sampled texture. Blocks until the upload completes (vkQueueWaitIdle
    // on the borrowed queue) — fine for the load-time/art-change frequency
    // this engine uses it at, not meant for streaming/video-rate uploads.
    // mips=false skips the mip chain (−33% VRAM, no blit pass) — right for
    // textures drawn at (or above) their upload resolution, where mips would
    // never be sampled; keep true whenever the texture is minified.
    TextureHandle create_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                 bool mips = true);
    void destroy_texture(TextureHandle handle);

    // Deferred GPU-safety reaper. create_texture() submits its upload with a
    // fence instead of vkQueueWaitIdle (same-queue submission order already
    // guarantees the upload lands before any later frame samples it), and
    // destroy_texture() only unlinks the texture — the actual VK object
    // destruction happens here. queueDrained=true means the caller has just
    // proven all previously submitted work finished (Renderer::draw() calls
    // this right after its frame-fence wait; cleanup() after device idle),
    // which is what makes freeing retired textures safe. With
    // queueDrained=false only fence-signaled upload staging is reclaimed.
    // This removed a full vkQueueWaitIdle per texture create AND per destroy
    // — the stall that made grid-art streaming hitch while scrolling.
    void collectGarbage(bool queueDrained);

    // Record (INSIDE the render pass, before the vector overlay composite):
    // draw every entry in `draws` as a textured quad, screen-space pixels.
    void recordComposite(VkCommandBuffer cmd, const std::vector<ImageDraw>& draws,
                          uint32_t screenW, uint32_t screenH);

private:
    struct TextureRec {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
        VkSampler      sampler = VK_NULL_HANDLE;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
    };

    VkDevice         device_         = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    AssetReader*     assets_         = nullptr;
    VkCommandPool    cmdPool_        = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;

    VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descPool_   = VK_NULL_HANDLE;
    static constexpr uint32_t kMaxTextures = 256;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_       = VK_NULL_HANDLE;

    std::unordered_map<TextureHandle, TextureRec> textures_;
    TextureHandle nextHandle_ = 1;

    // Retire lists drained by collectGarbage() — see its comment.
    struct PendingUpload {
        VkCommandBuffer cmd        = VK_NULL_HANDLE;
        VkBuffer        staging    = VK_NULL_HANDLE;
        VkDeviceMemory  stagingMem = VK_NULL_HANDLE;
        VkFence         fence      = VK_NULL_HANDLE;
    };
    std::vector<PendingUpload> pendingUploads_;
    std::vector<TextureRec>    pendingDestroy_;
    void freeRec(const TextureRec& rec);

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required);
    VkShaderModule loadShader(const char* path);
};
