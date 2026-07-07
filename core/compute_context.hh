#pragma once

#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR   // expose vkGetAndroidHardwareBufferProperties etc.
#endif
#include <vulkan/vulkan.h>
#include <android/asset_manager.h>
#include <android/hardware_buffer.h>
#include <cstdint>

// ---------------------------------------------------------------------------
// vce = vulkan_canvas_engine. A minimal compute-only Vulkan context: its own
// instance/device/queue (no swapchain, no surface, no YCbCr), so heavy compute
// dispatches never contend with a separate rendering device's preview drawing.
// General-purpose substrate for GPU image/buffer processing (an ISP, ML pre/
// post, GPGPU): buffer/image creation, persistently-mapped staging, shader
// loading from the asset manager, fences, and optional zero-copy import of an
// AHardwareBuffer.
// ---------------------------------------------------------------------------
namespace vce {
namespace gpu {

class ComputeContext {
public:
    ComputeContext()  = default;
    ~ComputeContext() { destroy(); }

    bool init();
    void destroy();

    bool ok() const { return device_ != VK_NULL_HANDLE; }

    VkDevice        device() const { return device_; }
    VkQueue         queue()  const { return queue_; }
    uint32_t        queue_family() const { return queue_family_; }
    VkCommandPool   pool()   const { return cmd_pool_; }

    // Host-visible-or-device buffer. `mapped` is non-null only for
    // host-visible allocations (persistently mapped).
    struct Buffer {
        VkBuffer       buf  = VK_NULL_HANDLE;
        VkDeviceMemory mem  = VK_NULL_HANDLE;
        void*          mapped = nullptr;
        VkDeviceSize   size = 0;
    };
    bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags props, Buffer& out);
    void destroy_buffer(Buffer& b);

    // 2D single-mip device-local image.
    struct Image {
        VkImage        img  = VK_NULL_HANDLE;
        VkDeviceMemory mem  = VK_NULL_HANDLE;
        VkImageView    view = VK_NULL_HANDLE;
    };
    bool create_image(uint32_t w, uint32_t h, VkFormat format,
                      VkImageUsageFlags usage, Image& out);
    void destroy_image(Image& i);

    // Imports an AHardwareBuffer as a zero-copy sampled image of `want` format.
    // Returns false if AHB import is unsupported or the buffer is not a plain
    // (non-external-format / non-YCbCr) image of `want` — so the caller can fall
    // back to a host copy. The default (R16_UINT) suits a RAW16 Bayer source fed
    // to an integer compute shader; pass another single-plane format for other
    // sources. The returned Image owns the import; destroy_image frees it
    // (releasing the AHB reference the import took).
    bool import_ahb(AHardwareBuffer* ahb, Image& out,
                    VkFormat want = VK_FORMAT_R16_UINT);
    bool ahb_import_supported() const { return ahb_supported_; }
    // Queue family the gralloc/producer "owns" imported buffers as, for
    // ownership-transfer barriers: FOREIGN if that extension is enabled, else
    // EXTERNAL.
    uint32_t external_queue_family() const {
        return foreign_queue_ext_ ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL;
    }

    VkShaderModule  load_shader(AAssetManager* assets, const char* asset_path);
    VkCommandBuffer alloc_cmd();
    VkFence         create_fence(bool signaled);

private:
    uint32_t find_mem_type(uint32_t type_bits, VkMemoryPropertyFlags props);

    VkInstance       instance_  = VK_NULL_HANDLE;
    VkPhysicalDevice phys_      = VK_NULL_HANDLE;
    VkDevice         device_    = VK_NULL_HANDLE;
    VkQueue          queue_     = VK_NULL_HANDLE;
    uint32_t         queue_family_ = 0;
    VkCommandPool    cmd_pool_  = VK_NULL_HANDLE;

    // AHB-import (zero-copy input) capability, resolved at init.
    bool ahb_supported_     = false;
    bool foreign_queue_ext_ = false;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID pfn_ahb_props_ = nullptr;
};

}  // namespace gpu
}  // namespace vce
