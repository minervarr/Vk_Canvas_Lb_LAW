// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

bool Renderer::Impl::readbackLastFrame(std::vector<uint8_t>& rgba_out,
                                 uint32_t& out_w, uint32_t& out_h) {
    if (!device_ || device_lost_ || surface_lost_) return false;
    if (last_drawn_image_index_ >= swapchain_images_.size()) return false;

    // The contract is tightly packed RGBA8, PNG-ready with no swizzle, which
    // only holds for the SDR 8-bit target. Under an HDR swapchain the source
    // is FP16 or 10-bit-packed and the copy below would produce garbage — and
    // a PNG could not represent the luminance anyway. Capture pins SDR.
    if (output_.hdr) {
        LOGE("readbackLastFrame: unsupported under an HDR swapchain (target=%s, fmt=%d) — "
             "the PNG path requires the SDR 8-bit target",
             outputTargetName(output_.target), swapchain_format_);
        return false;
    }

    VkImage image = swapchain_images_[last_drawn_image_index_];

    // All draw/present work must be finished before we touch the image.
    if (!vce_vk_ok(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(readback)", __FILE__, __LINE__))
        return false;

    const VkDeviceSize bytes = VkDeviceSize(width_) * height_ * 4;

    // Host-visible staging buffer to receive the copy.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &bi, nullptr, &staging) != VK_SUCCESS) return false;

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, staging, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) {
            LOGE("readbackLastFrame: no host-visible memory type");
            vkDestroyBuffer(device_, staging, nullptr);
            return false;
        }
        if (vkAllocateMemory(device_, &ai, nullptr, &staging_mem) != VK_SUCCESS) {
            vkDestroyBuffer(device_, staging, nullptr);
            return false;
        }
        VCE_VK_CHECK_OR(vkBindBufferMemory(device_, staging, staging_mem, 0),
                        { vkDestroyBuffer(device_, staging, nullptr);
                          vkFreeMemory(device_, staging_mem, nullptr); return false; });
    }

    // One-shot command buffer: barrier image PRESENT_SRC -> TRANSFER_SRC,
    // copy the whole image to the buffer, barrier back to PRESENT_SRC.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = cmd_pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cai, &cmd) != VK_SUCCESS) {
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    auto barrier = [&](VkImageLayout from, VkImageLayout to,
                       VkAccessFlags src_access, VkAccessFlags dst_access,
                       VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.srcAccessMask = src_access;
        b.dstAccessMask = dst_access;
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width_, height_, 1 };
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fci, nullptr, &fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    const bool submitted =
        vce_vk_ok(vkQueueSubmit(queue_, 1, &si, fence), "vkQueueSubmit(readback)", __FILE__, __LINE__)
        && vce_vk_ok(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX),
                     "vkWaitForFences(readback)", __FILE__, __LINE__);
    if (!submitted) {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device_, staging_mem, 0, bytes, 0, &mapped) != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }
    rgba_out.resize(size_t(bytes));
    std::memcpy(rgba_out.data(), mapped, size_t(bytes));
    vkUnmapMemory(device_, staging_mem);

    out_w = width_;
    out_h = height_;

    vkDestroyFence(device_, fence, nullptr);
    vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);
    return true;
}
