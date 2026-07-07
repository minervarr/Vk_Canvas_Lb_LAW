// canvas_host.cc — camera-free Vulkan host for the canvas engine.
//
// Copyright (C) 2026 nava.
//
// This program is free software: you can redistribute it and/or modify it under
// the terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option) any
// later version. This program is distributed WITHOUT ANY WARRANTY; see the GNU
// Affero General Public License for more details.
#include "canvas_host.hh"

#include <android/log.h>
#include <cstring>
#include <set>
#include <vector>

#define LOG_TAG "CanvasHost"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

namespace {
VkShaderModule loadShaderModule(VkDevice device, AAssetManager* mgr, const char* path) {
    AAsset* a = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    if (!a) { LOGE("shader asset missing: %s", path); return VK_NULL_HANDLE; }
    size_t n = AAsset_getLength(a);
    std::vector<uint32_t> code(n / 4);
    AAsset_read(a, code.data(), n);
    AAsset_close(a);
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = n;
    ci.pCode    = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &m);
    return m;
}
}  // namespace

void CanvasHost::init(ANativeWindow* window, AAssetManager* assetManager, const char* cachePath) {
    if (ready_) return;
    assetManager_ = assetManager;
    if (cachePath) msdfCachePath_ = cachePath;
    if (!createInstance())        { LOGE("createInstance failed");        return; }
    if (!createSurface(window))   { LOGE("createSurface failed");         return; }
    if (!pickPhysicalDevice())    { LOGE("pickPhysicalDevice failed");    return; }
    if (!createDevice())          { LOGE("createDevice failed");          return; }
    if (!createSwapchain())       { LOGE("createSwapchain failed");       return; }
    if (!createRenderPass())      { LOGE("createRenderPass failed");      return; }
    if (!createFramebuffers())    { LOGE("createFramebuffers failed");    return; }
    if (!createCommandsAndSync()) { LOGE("createCommandsAndSync failed"); return; }

    overlay_.init(device_, physicalDev_, assetManager_, renderPass_,
                  extent_.width, extent_.height);
    if (!overlay_.ready()) { LOGE("OverlayRasterizer not ready (shaders?)"); return; }

    if (!createMsdf())
        LOGE("MSDF text unavailable; text falls back to the Bezier path");

    ready_ = true;
    LOGI("CanvasHost ready (%ux%u)", extent_.width, extent_.height);
}

bool CanvasHost::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "canvas_host";
    appInfo.apiVersion       = VK_API_VERSION_1_1;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = 2;
    ci.ppEnabledExtensionNames = extensions;
    return vkCreateInstance(&ci, nullptr, &instance_) == VK_SUCCESS;
}

bool CanvasHost::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR ci{};
    ci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    ci.window = window;
    return vkCreateAndroidSurfaceKHR(instance_, &ci, nullptr, &surface_) == VK_SUCCESS;
}

bool CanvasHost::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            physicalDev_ = d;
            break;
        }
        if (physicalDev_ == VK_NULL_HANDLE) physicalDev_ = d;
    }
    return physicalDev_ != VK_NULL_HANDLE;
}

bool CanvasHost::createDevice() {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDev_, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDev_, &count, families.data());
    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxFamily_ = i;
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDev_, i, surface_, &present);
        if (present) presentFamily_ = i;
    }
    if (gfxFamily_ == UINT32_MAX || presentFamily_ == UINT32_MAX) return false;

    float priority = 1.0f;
    std::set<uint32_t> unique = {gfxFamily_, presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t fam : unique) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }
    const char* deviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.enabledExtensionCount   = 1;
    ci.ppEnabledExtensionNames = deviceExt;
    if (vkCreateDevice(physicalDev_, &ci, nullptr, &device_) != VK_SUCCESS) return false;

    vkGetDeviceQueue(device_, gfxFamily_, 0, &gfxQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
    return true;
}

bool CanvasHost::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDev_, surface_, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &formatCount, nullptr);
    if (formatCount == 0) return false;
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDev_, surface_, &formatCount, formats.data());

    // Prefer a plain UNORM format so the composited overlay colours are written
    // verbatim (no implicit linear->sRGB encode that would darken them).
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = f;
            break;
        }
    }
    swapFormat_ = chosen.format;

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDev_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDev_, surface_, &presentModeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available
    for (VkPresentModeKHR m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; }
    }

    extent_ = caps.currentExtent;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = chosen.format;
    ci.imageColorSpace  = chosen.colorSpace;
    ci.imageExtent      = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = presentMode;
    ci.clipped          = VK_TRUE;

    uint32_t indices[] = {gfxFamily_, presentFamily_};
    if (gfxFamily_ != presentFamily_) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = indices;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) return false;

    uint32_t got = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &got, nullptr);
    if (got == 0) return false;
    images_.resize(got);
    vkGetSwapchainImagesKHR(device_, swapchain_, &got, images_.data());

    imageViews_.resize(got);
    for (uint32_t i = 0; i < got; i++) {
        VkImageViewCreateInfo vi{};
        vi.sType                       = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image                       = images_[i];
        vi.viewType                    = VK_IMAGE_VIEW_TYPE_2D;
        vi.format                      = swapFormat_;
        vi.subresourceRange            = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(device_, &vi, nullptr, &imageViews_[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool CanvasHost::createRenderPass() {
    VkAttachmentDescription color{};
    color.format         = swapFormat_;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &color;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    return vkCreateRenderPass(device_, &ci, nullptr, &renderPass_) == VK_SUCCESS;
}

bool CanvasHost::createFramebuffers() {
    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); i++) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = renderPass_;
        ci.attachmentCount = 1;
        ci.pAttachments    = &imageViews_[i];
        ci.width           = extent_.width;
        ci.height          = extent_.height;
        ci.layers          = 1;
        if (vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

bool CanvasHost::createCommandsAndSync() {
    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = gfxFamily_;
    if (vkCreateCommandPool(device_, &pci, nullptr, &cmdPool_) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = cmdPool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &ai, &cmdBuffer_) != VK_SUCCESS) return false;

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateSemaphore(device_, &si, nullptr, &imageAvailable_) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(device_, &si, nullptr, &renderFinished_) != VK_SUCCESS) return false;
    if (vkCreateFence(device_, &fi, nullptr, &inFlight_) != VK_SUCCESS) return false;
    return true;
}

void CanvasHost::renderFrame(const std::vector<float>& curves,
                             const std::vector<float>& quads) {
    if (!ready_) return;

    vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                             imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return;  // surface resized; handled on next window event

    vkResetFences(device_, 1, &inFlight_);

    overlay_.uploadCurves(curves.data(),
                          static_cast<uint32_t>(curves.size() / OverlayRasterizer::CURVE_FLOATS));

    // Upload this frame's MSDF text vertices (8 floats/vert).
    uint32_t verts = static_cast<uint32_t>(quads.size() / 8);
    if (verts > kMaxMsdfVerts) verts = kMaxMsdfVerts;
    if (msdfReady_ && msdfVboMapped_ && verts > 0)
        std::memcpy(msdfVboMapped_, quads.data(),
                    static_cast<size_t>(verts) * 8 * sizeof(float));
    msdfVertCount_ = verts;

    vkResetCommandBuffer(cmdBuffer_, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmdBuffer_, &begin);

    // Rasterise the curves into the overlay image (before the render pass).
    overlay_.recordDispatch(cmdBuffer_);

    // The overlay image is sampled in GENERAL layout by the composite. Make the
    // compute writes visible to the fragment reads — the engine's rasterizer
    // omits this barrier (it tolerates a 1-frame tear over a live camera feed),
    // but a UI must not flicker, so the host adds it.
    VkMemoryBarrier mb{};
    mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuffer_,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo rp{};
    rp.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass        = renderPass_;
    rp.framebuffer       = framebuffers_[imageIndex];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = extent_;
    rp.clearValueCount   = 1;
    rp.pClearValues      = &clear;
    vkCmdBeginRenderPass(cmdBuffer_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Composite the overlay (premultiplied-alpha) over the cleared background.
    overlay_.recordComposite(cmdBuffer_, 0);

    // Draw MSDF text quads on top (straight-alpha blended).
    drawMsdf(cmdBuffer_);

    vkCmdEndRenderPass(cmdBuffer_);
    vkEndCommandBuffer(cmdBuffer_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &imageAvailable_;
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmdBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &renderFinished_;
    vkQueueSubmit(gfxQueue_, 1, &submit, inFlight_);

    VkPresentInfoKHR present{};
    present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores    = &renderFinished_;
    present.swapchainCount     = 1;
    present.pSwapchains        = &swapchain_;
    present.pImageIndices      = &imageIndex;
    vkQueuePresentKHR(presentQueue_, &present);
}

uint32_t CanvasHost::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDev_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return UINT32_MAX;
}

bool CanvasHost::allocBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags props, VkBuffer& buf,
                             VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{};
    bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size        = size;
    bi.usage       = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &bi, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(device_, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(device_, buf, mem, 0);
    return true;
}

bool CanvasHost::createMsdf() {
    // Prefer the pre-baked v2 atlas (font.msdf + atlas.rgba) — it carries the
    // OpenType MATH payload (constants, stretchy constructions, math-italic) that
    // the runtime msdfgen path can't produce. Fall back to generating from the OTF
    // only if the baked atlas is missing/unreadable (text-only, no math).
    if (!msdf_.load(assetManager_, "fonts/font.msdf", "fonts/atlas.rgba")) {
        LOGE("baked font.msdf unavailable; generating text-only atlas from font.otf");
        if (!msdf_.generate(assetManager_, "fonts/font.otf",
                            msdfCachePath_.empty() ? nullptr : msdfCachePath_.c_str()))
            return false;
    }
    if (!msdf_.valid()) return false;
    msdfPxRange_ = msdf_.distanceRange();
    msdfAtlasW_  = static_cast<float>(msdf_.atlasW());
    msdfAtlasH_  = static_cast<float>(msdf_.atlasH());
    const uint32_t aw = msdf_.atlasW(), ah = msdf_.atlasH();

    // ── Atlas image (device-local, sampled) ─────────────────────────────────
    VkImageCreateInfo img{};
    img.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img.imageType     = VK_IMAGE_TYPE_2D;
    img.format        = VK_FORMAT_R8G8B8A8_UNORM;
    img.extent        = {aw, ah, 1};
    img.mipLevels     = 1;
    img.arrayLayers   = 1;
    img.samples       = VK_SAMPLE_COUNT_1_BIT;
    img.tiling        = VK_IMAGE_TILING_OPTIMAL;
    img.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &img, nullptr, &msdfImage_) != VK_SUCCESS) return false;
    VkMemoryRequirements ir{};
    vkGetImageMemoryRequirements(device_, msdfImage_, &ir);
    VkMemoryAllocateInfo ia{};
    ia.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ia.allocationSize  = ir.size;
    ia.memoryTypeIndex = findMemoryType(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ia, nullptr, &msdfImageMem_) != VK_SUCCESS) return false;
    vkBindImageMemory(device_, msdfImage_, msdfImageMem_, 0);

    VkImageViewCreateInfo iv{};
    iv.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image            = msdfImage_;
    iv.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    iv.format           = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &iv, nullptr, &msdfView_);

    VkSamplerCreateInfo sm{};
    sm.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sm.magFilter    = VK_FILTER_LINEAR;
    sm.minFilter    = VK_FILTER_LINEAR;
    sm.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sm.addressModeU = sm.addressModeV = sm.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &sm, nullptr, &msdfSampler_);

    // ── One-shot atlas upload via staging buffer ────────────────────────────
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(aw) * ah * 4;
    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    if (!allocBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMem)) return false;
    void* sp = nullptr;
    vkMapMemory(device_, stagingMem, 0, bytes, 0, &sp);
    std::memcpy(sp, msdf_.atlas().data(), static_cast<size_t>(bytes));
    vkUnmapMemory(device_, stagingMem);

    VkCommandBufferAllocateInfo ca{};
    ca.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool        = cmdPool_;
    ca.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer up = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &ca, &up);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(up, &bi);
    VkImageMemoryBarrier toDst{};
    toDst.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image            = msdfImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(up, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {aw, ah, 1};
    vkCmdCopyBufferToImage(up, staging, msdfImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(up, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(up);
    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &up;
    vkQueueSubmit(gfxQueue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &up);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

    // ── Descriptor (combined image sampler) ─────────────────────────────────
    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{};
    dl.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dl.bindingCount = 1;
    dl.pBindings    = &b;
    vkCreateDescriptorSetLayout(device_, &dl, nullptr, &msdfSetLayout_);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp{};
    dp.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dp.poolSizeCount = 1;
    dp.pPoolSizes    = &ps;
    dp.maxSets       = 1;
    vkCreateDescriptorPool(device_, &dp, nullptr, &msdfPool_);
    VkDescriptorSetAllocateInfo das{};
    das.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    das.descriptorPool     = msdfPool_;
    das.descriptorSetCount = 1;
    das.pSetLayouts        = &msdfSetLayout_;
    vkAllocateDescriptorSets(device_, &das, &msdfSet_);
    VkDescriptorImageInfo di{};
    di.sampler     = msdfSampler_;
    di.imageView   = msdfView_;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wr{};
    wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet          = msdfSet_;
    wr.dstBinding      = 0;
    wr.descriptorCount = 1;
    wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo      = &di;
    vkUpdateDescriptorSets(device_, 1, &wr, 0, nullptr);

    // ── Vertex buffer (host-visible, persistently mapped) ───────────────────
    const VkDeviceSize vbBytes = static_cast<VkDeviceSize>(kMaxMsdfVerts) * 8 * sizeof(float);
    if (!allocBuffer(vbBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     msdfVbo_, msdfVboMem_)) return false;
    vkMapMemory(device_, msdfVboMem_, 0, vbBytes, 0, &msdfVboMapped_);

    // ── Pipeline ────────────────────────────────────────────────────────────
    VkShaderModule vs = loadShaderModule(device_, assetManager_, "shaders/msdf_vert.spv");
    VkShaderModule fs = loadShaderModule(device_, assetManager_, "shaders/msdf_frag.spv");
    if (!vs || !fs) return false;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size       = sizeof(float) * 8;  // screen.xy pxRange pad atlas.xy scroll.xy
    VkPipelineLayoutCreateInfo pl{};
    pl.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount         = 1;
    pl.pSetLayouts            = &msdfSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges    = &pcr;
    vkCreatePipelineLayout(device_, &pl, nullptr, &msdfPipeLayout_);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,       0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,       2 * sizeof(float)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * sizeof(float)},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia2{};
    ia2.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia2.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount          = 2;
    gp.pStages             = stages;
    gp.pVertexInputState   = &vi;
    gp.pInputAssemblyState = &ia2;
    gp.pViewportState      = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState   = &ms;
    gp.pColorBlendState    = &cb;
    gp.pDynamicState       = &ds;
    gp.layout              = msdfPipeLayout_;
    gp.renderPass          = renderPass_;
    gp.subpass             = 0;
    VkResult pr = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &msdfPipe_);
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    if (pr != VK_SUCCESS) { msdfPipe_ = VK_NULL_HANDLE; return false; }

    msdfReady_ = true;
    LOGI("MSDF text ready: %u glyphs, atlas %ux%u", 0u, aw, ah);
    return true;
}

void CanvasHost::drawMsdf(VkCommandBuffer cmd) {
    if (!msdfReady_ || msdfVertCount_ == 0) return;
    VkViewport vp{0.0f, 0.0f, static_cast<float>(extent_.width),
                  static_cast<float>(extent_.height), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, msdfPipe_);
    float push[8] = {static_cast<float>(extent_.width), static_cast<float>(extent_.height),
                     msdfPxRange_, 0.0f, msdfAtlasW_, msdfAtlasH_, 0.0f, 0.0f};
    vkCmdPushConstants(cmd, msdfPipeLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, msdfPipeLayout_,
                            0, 1, &msdfSet_, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &msdfVbo_, &off);
    vkCmdDraw(cmd, msdfVertCount_, 1, 0, 0);
}

void CanvasHost::cleanup() {
    if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
    ready_ = false;

    overlay_.cleanup();

    if (msdfVboMapped_) { vkUnmapMemory(device_, msdfVboMem_); msdfVboMapped_ = nullptr; }
    if (msdfVbo_)        vkDestroyBuffer(device_, msdfVbo_, nullptr);
    if (msdfVboMem_)     vkFreeMemory(device_, msdfVboMem_, nullptr);
    if (msdfPipe_)       vkDestroyPipeline(device_, msdfPipe_, nullptr);
    if (msdfPipeLayout_) vkDestroyPipelineLayout(device_, msdfPipeLayout_, nullptr);
    if (msdfPool_)       vkDestroyDescriptorPool(device_, msdfPool_, nullptr);
    if (msdfSetLayout_)  vkDestroyDescriptorSetLayout(device_, msdfSetLayout_, nullptr);
    if (msdfSampler_)    vkDestroySampler(device_, msdfSampler_, nullptr);
    if (msdfView_)       vkDestroyImageView(device_, msdfView_, nullptr);
    if (msdfImage_)      vkDestroyImage(device_, msdfImage_, nullptr);
    if (msdfImageMem_)   vkFreeMemory(device_, msdfImageMem_, nullptr);

    if (inFlight_)       vkDestroyFence(device_, inFlight_, nullptr);
    if (renderFinished_) vkDestroySemaphore(device_, renderFinished_, nullptr);
    if (imageAvailable_) vkDestroySemaphore(device_, imageAvailable_, nullptr);
    if (cmdPool_)        vkDestroyCommandPool(device_, cmdPool_, nullptr);

    for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    if (renderPass_) vkDestroyRenderPass(device_, renderPass_, nullptr);
    for (VkImageView v : imageViews_) vkDestroyImageView(device_, v, nullptr);
    imageViews_.clear();
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (device_)    vkDestroyDevice(device_, nullptr);
    if (surface_)   vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)  vkDestroyInstance(instance_, nullptr);

    *this = CanvasHost{};
}
