// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

namespace {

VkFormat srgbTwinOf(VkFormat unorm) {
    switch (unorm) {
    case VK_FORMAT_R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:       return VK_FORMAT_B8G8R8A8_SRGB;
    case VK_FORMAT_R8G8B8_UNORM:         return VK_FORMAT_R8G8B8_SRGB;
    case VK_FORMAT_B8G8R8_UNORM:         return VK_FORMAT_B8G8R8_SRGB;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32: return VK_FORMAT_A8B8G8R8_SRGB_PACK32;
    default:                             return VK_FORMAT_UNDEFINED;
    }
}

}  // namespace

void Renderer::Impl::resolve_text_composite() {
    // Once, beside resolve_output_target(). create_swapchain() may still
    // clear text_srgb_pass_ if the mutable create is refused; the render
    // pass does not exist yet, so that is still consistent.
    text_srgb_pass_ = false;
    text_linear_surface_ = false;
    text_srgb_format_ = VK_FORMAT_UNDEFINED;

    if (output_.encode == OutputEncode::LinearScRgb) {
        text_linear_surface_ = true;
        LOGI("Text composite: linear swapchain, same pass");
        return;
    }
    if (output_.encode != OutputEncode::Srgb) {
        LOGI("Text composite: legacy encode (%s)", outputEncodeName(output_.encode));
        return;
    }
    if (!device_mutable_swapchain_ || text_mutable_refused_) {
        LOGI("Text composite: legacy encode (no mutable sRGB view)");
        return;
    }
    const VkFormat srgb = srgbTwinOf(swapchain_format_);
    if (srgb == VK_FORMAT_UNDEFINED) {
        LOGI("Text composite: legacy encode (format %d has no sRGB twin)",
             (int)swapchain_format_);
        return;
    }
    // The image is created with COLOR_ATTACHMENT | TRANSFER_SRC, and every
    // format in the mutable list has to support that usage. BLEND is what
    // the text pass actually does; without it the view would still create
    // and the blend state would be illegal.
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physical_dev_, srgb, &props);
    const VkFormatFeatureFlags need =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((props.optimalTilingFeatures & need) != need) {
        LOGI("Text composite: legacy encode (sRGB twin %d features 0x%x)",
             (int)srgb, (unsigned)props.optimalTilingFeatures);
        return;
    }
    text_srgb_format_ = srgb;
    text_srgb_pass_ = true;
    text_linear_surface_ = true;
    LOGI("Text composite: sRGB view fmt=%d (linear blend)", (int)srgb);
}

void Renderer::Impl::create_swapchain() {
    // swapchain_format_/_colorspace_ were fixed by resolve_output_target() in
    // the constructor and deliberately do not move across a resize — see there.

    // Present mode: prefer MAILBOX over FIFO. With FIFO the present queue is a
    // hard line — vkQueuePresentKHR/acquire block until the compositor releases an
    // image at the display's vsync cadence. While recording, the compositor
    // periodically holds our image ~60ms (status-bar inset churn, extra
    // composition passes), which with our tight image count starved rendering and
    // froze the preview ~1Hz. MAILBOX never blocks the producer: we always get a
    // free image to render into and the newest frame replaces the queued one, so a
    // transient compositor hold no longer stalls us. FIFO is the guaranteed
    // fallback if MAILBOX is unsupported.
    //
    // ...for a consumer that asked for Latency. A consumer that asked for Vsync
    // is presenting frames a clock scheduled, not "whatever is newest", so
    // there is nothing for MAILBOX to prefer and nothing gained by never
    // blocking — while the cost, a render loop free-running at hundreds of fps
    // over identical content, is entirely real. FIFO is always available.
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (present_policy_ == PresentPolicy::Latency) {
        uint32_t pm_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_dev_, surface_, &pm_count, nullptr);
        std::vector<VkPresentModeKHR> modes(pm_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_dev_, surface_, &pm_count, modes.data());
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode = VK_PRESENT_MODE_MAILBOX_KHR; break; }
        }
    }

    // Honor the surface's max image count (0 == no limit) and give the compositor
    // headroom: MAILBOX needs >=3, and a 4th image lets us keep rendering while one
    // is on screen, one queued, and one held by the compositor mid-hitch (Android).
    // The count is caller-configurable (constructor): each extra image is a full
    // window-sized allocation, so desktop callers request 3.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_dev_, surface_, &caps);
    uint32_t desired_images = desired_swapchain_images_;
    if (desired_images < 2) desired_images = 2;
    if (desired_images < caps.minImageCount) desired_images = caps.minImageCount;
    if (caps.maxImageCount > 0 && desired_images > caps.maxImageCount) desired_images = caps.maxImageCount;
    LOGI("Swapchain present mode=%d (%s), images=%u", present_mode,
         present_policy_ == PresentPolicy::Vsync ? "vsync-paced" : "low-latency",
         desired_images);

    VkCompositeAlphaFlagBitsKHR composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & composite_alpha)) {
        // Fall back to whatever the surface actually advertises.
        for (auto bit : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR}) {
            if (caps.supportedCompositeAlpha & bit) { composite_alpha = bit; break; }
        }
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = desired_images;
    ci.imageFormat      = swapchain_format_;
    ci.imageColorSpace  = swapchain_colorspace_;
    ci.imageExtent      = { width_, height_ };
    ci.imageArrayLayers = 1;
    // TRANSFER_SRC lets readbackLastFrame() copy a rendered image to a host
    // buffer (headless UI capture). Harmless for the windowed present path.
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // preTransform is a PROMISE, not a request. Setting it to
    // caps.currentTransform tells the presentation engine "my images are
    // ALREADY rotated by that much" — so it applies nothing further. On a
    // desktop currentTransform is always IDENTITY and the distinction never
    // surfaces; on Android it becomes ROTATE_90/180/270 the moment the device
    // turns, and an app that keeps drawing upright then has its frame
    // presented un-rotated onto a turned panel. Measured on a moto g06: the
    // whole UI came out upside down and squashed across the long edge.
    //
    // Asking for IDENTITY hands the rotation back to the compositor, which is
    // what an app that does not pre-rotate its own geometry must do. It costs
    // one composition pass on some hardware — the price of not having a
    // rotation matrix threaded through every draw call in the engine, which
    // is the alternative and is not worth it for this app.
    //
    // Falls back to currentTransform where IDENTITY is not advertised, which
    // is the only honest answer there: refusing to create a swapchain would be
    // worse than a rotated one.
    VkSurfaceTransformFlagBitsKHR pre_transform = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    LOGI("Swapchain extent=%ux%u currentExtent=%ux%u currentTransform=0x%x preTransform=0x%x",
         width_, height_, caps.currentExtent.width, caps.currentExtent.height,
         (unsigned)caps.currentTransform, (unsigned)pre_transform);
    ci.preTransform     = pre_transform;
    ci.compositeAlpha   = composite_alpha;
    ci.presentMode      = present_mode;
    ci.clipped          = VK_TRUE;

    // sRGB view of the same images. The list must include the creation
    // format. Stack lifetime covers the create call. A refused create on
    // the first try drops back to a plain UNORM swapchain and the legacy
    // encode; the render pass is not built yet. A refused create later is
    // fatal: the UI pass's finalLayout already expects the text pass to
    // finish the transition to PRESENT_SRC, and an sRGB view of a
    // non-mutable swapchain is illegal.
    VkFormat viewFmts[2] = { swapchain_format_, text_srgb_format_ };
    VkImageFormatListCreateInfo fmtList{};
    fmtList.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    fmtList.viewFormatCount = 2;
    fmtList.pViewFormats = viewFmts;
    const bool wantMutable = text_srgb_pass_ && !text_mutable_refused_
                          && text_srgb_format_ != VK_FORMAT_UNDEFINED;
    if (wantMutable) {
        ci.flags = VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;
        ci.pNext = &fmtList;
    }

    VkSwapchainKHR created = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &created) != VK_SUCCESS) {
        if (!wantMutable || render_pass_ != VK_NULL_HANDLE)
            throw std::runtime_error("vkCreateSwapchainKHR failed");
        LOGE("Text composite: mutable sRGB swapchain refused — legacy encode");
        text_mutable_refused_ = true;
        text_srgb_pass_ = false;
        text_linear_surface_ = false;
        text_srgb_format_ = VK_FORMAT_UNDEFINED;
        ci.flags = 0;
        ci.pNext = nullptr;
        if (vkCreateSwapchainKHR(device_, &ci, nullptr, &created) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");
    }
    swapchain_ = created;

    uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, nullptr);
    // Per-image "which frame-fence last used this image" tracking for the
    // frames-in-flight scheme; reset on every (re)create since all prior
    // work was drained first.
    image_fences_.assign(img_count, VK_NULL_HANDLE);
}

void Renderer::Impl::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // The text pass, when there is one, is what transitions to PRESENT_SRC.
    // finalLayout is fixed for the life of this pass, which is not rebuilt
    // on resize — text_srgb_pass_ has to stay in step with it.
    color_attachment.finalLayout = text_srgb_pass_
        ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &color_attachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;

    if (vkCreateRenderPass(device_, &ci, nullptr, &render_pass_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass failed");
    if (!text_srgb_pass_) return;

    VkAttachmentDescription textAtt{};
    textAtt.format = text_srgb_format_;
    textAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    textAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    textAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    textAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    textAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    textAtt.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    textAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference textRef{};
    textRef.attachment = 0;
    textRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription textSub{};
    textSub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    textSub.colorAttachmentCount = 1;
    textSub.pColorAttachments = &textRef;

    // The UI pass's colour writes are earlier in this same command buffer.
    // The implicit external dependency starts at TOP_OF_PIPE and does not
    // wait for them, so without this the text pass blends against a tile
    // the UI pass has not stored.
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
                      | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo tci{};
    tci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    tci.attachmentCount = 1;
    tci.pAttachments = &textAtt;
    tci.subpassCount = 1;
    tci.pSubpasses = &textSub;
    tci.dependencyCount = 1;
    tci.pDependencies = &dep;
    VkRenderPass textPass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &tci, nullptr, &textPass) != VK_SUCCESS)
        throw std::runtime_error("text sRGB render pass failed");
    text_render_pass_ = textPass;
}

void Renderer::Impl::create_framebuffers() {
    uint32_t image_count;
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_image_views_.resize(image_count);
    framebuffers_.resize(image_count);
    if (text_srgb_pass_) {
        text_image_views_.assign(image_count, VK_NULL_HANDLE);
        text_framebuffers_.assign(image_count, VK_NULL_HANDLE);
    }

    for (size_t i = 0; i < image_count; i++) {
        VkImageViewCreateInfo iv_ci{};
        iv_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv_ci.image = swapchain_images_[i];
        iv_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv_ci.format = swapchain_format_;
        iv_ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        iv_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv_ci.subresourceRange.baseMipLevel = 0;
        iv_ci.subresourceRange.levelCount = 1;
        iv_ci.subresourceRange.baseArrayLayer = 0;
        iv_ci.subresourceRange.layerCount = 1;

        vkCreateImageView(device_, &iv_ci, nullptr, &swapchain_image_views_[i]);

        VkFramebufferCreateInfo fb_ci{};
        fb_ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_ci.renderPass = render_pass_;
        fb_ci.attachmentCount = 1;
        fb_ci.pAttachments = &swapchain_image_views_[i];
        fb_ci.width = width_;
        fb_ci.height = height_;
        fb_ci.layers = 1;

        vkCreateFramebuffer(device_, &fb_ci, nullptr, &framebuffers_[i]);

        if (!text_srgb_pass_) continue;
        VkImageViewCreateInfo tv = iv_ci;
        tv.format = text_srgb_format_;
        VkImageView view = VK_NULL_HANDLE;
        if (vkCreateImageView(device_, &tv, nullptr, &view) != VK_SUCCESS)
            throw std::runtime_error("sRGB text view failed");
        text_image_views_[i] = view;
        VkFramebufferCreateInfo tfb = fb_ci;
        tfb.renderPass = text_render_pass_;
        tfb.pAttachments = &text_image_views_[i];
        VkFramebuffer fb = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(device_, &tfb, nullptr, &fb) != VK_SUCCESS)
            throw std::runtime_error("sRGB text framebuffer failed");
        text_framebuffers_[i] = fb;
    }
}

void Renderer::Impl::create_command_buffers() {
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = queue_family_;

    vkCreateCommandPool(device_, &pool_ci, nullptr, &cmd_pool_);

    cmd_buffers_.resize(framebuffers_.size());
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t)cmd_buffers_.size();

    vkAllocateCommandBuffers(device_, &alloc_info, cmd_buffers_.data());
}

void Renderer::Impl::create_sync_objects() {
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        vkCreateSemaphore(device_, &sem_ci, nullptr, &image_available_sems_[f]);
        vkCreateSemaphore(device_, &sem_ci, nullptr, &render_finished_sems_[f]);
        vkCreateFence(device_, &fence_ci, nullptr, &in_flight_fences_[f]);
    }
}

#if defined(__ANDROID__)
void Renderer::Impl::cleanup_hwb_resources() {
    // Null every handle right after destroying it: clear_camera_frames() is called
    // at the end of this function and ALSO touches desc_pool_ / hwb_cache_ (it
    // vkResetDescriptorPool's the pool). Without nulling, that reset hits the
    // already-destroyed pool → the Mali driver locks a freed mutex → SIGABRT
    // ("pthread_mutex_lock called on a destroyed mutex") on every teardown.
    if (pipeline_) { vkDestroyPipeline(device_, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (pipeline_layout_) { vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr); pipeline_layout_ = VK_NULL_HANDLE; }

    for (auto& pair : hwb_cache_) {
        vkDestroyImageView(device_, pair.second.view, nullptr);
        vkDestroyImage(device_, pair.second.image, nullptr);
        vkFreeMemory(device_, pair.second.memory, nullptr);
    }
    hwb_cache_.clear();
    hwb_lru_.clear();

    if (desc_pool_) { vkDestroyDescriptorPool(device_, desc_pool_, nullptr); desc_pool_ = VK_NULL_HANDLE; }
    if (desc_layout_) { vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr); desc_layout_ = VK_NULL_HANDLE; }

    if (hwb_sampler_) { vkDestroySampler(device_, hwb_sampler_, nullptr); hwb_sampler_ = VK_NULL_HANDLE; }
    if (ycbcr_conversion_ && vkDestroySamplerYcbcrConversion_) { vkDestroySamplerYcbcrConversion_(device_, ycbcr_conversion_, nullptr); ycbcr_conversion_ = VK_NULL_HANDLE; }
    clear_camera_frames();
}
#endif  // __ANDROID__

void Renderer::Impl::destroy_swapchain_resources() {
    for (auto fb : text_framebuffers_)
        if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
    text_framebuffers_.clear();
    for (auto iv : text_image_views_)
        if (iv) vkDestroyImageView(device_, iv, nullptr);
    text_image_views_.clear();
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto iv : swapchain_image_views_) vkDestroyImageView(device_, iv, nullptr);
    swapchain_image_views_.clear();
    swapchain_images_.clear();
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

bool Renderer::Impl::recreate_surface() {
    // See the header for what this is for. The order matters twice over:
    // everything using the old swapchain must be gone before the SURFACE it
    // was made from is destroyed, and on Android the VkSurfaceKHR must be
    // destroyed before the ANativeWindow behind it is released — which is what
    // the caller does next.
    vkDeviceWaitIdle(device_);
    destroy_swapchain_resources();
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }

    // A window that has gone away and not yet come back. Not an error: the
    // caller retries when the platform hands it a new one.
    VkExtent2D ext = surface_provider_.extent();
    if (ext.width == 0 || ext.height == 0) return true;

    create_surface();
    if (surface_ == VK_NULL_HANDLE) return false;

    // The render pass and every pipeline were built against the OLD surface's
    // format. If the new one cannot give us the same, none of them are valid
    // and the honest answer is to say so rather than render into a mismatch.
    // resolve_output_target() is deliberately not re-run — its own comment
    // says it must not — so this only CHECKS.
    {
        uint32_t n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_dev_, surface_, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_dev_, surface_, &n, formats.data());
        bool ok = false;
        for (const VkSurfaceFormatKHR& f : formats)
            if (f.format == swapchain_format_ && f.colorSpace == swapchain_colorspace_) {
                ok = true;
                break;
            }
        if (!ok) {
            LOGI("recreate_surface: new surface lacks format=%d colorspace=%d — full rebuild needed",
                 (int)swapchain_format_, (int)swapchain_colorspace_);
            return false;
        }
    }

    width_  = ext.width;
    height_ = ext.height;
    create_swapchain();
    create_framebuffers();

    // Same guard recreate_swapchain() carries, for the same reason: the image
    // count is normally deterministic for a surface, but cmd_buffers_ is
    // indexed by image and must not be short.
    if (cmd_buffers_.size() != framebuffers_.size()) {
        if (!cmd_buffers_.empty())
            vkFreeCommandBuffers(device_, cmd_pool_, (uint32_t)cmd_buffers_.size(), cmd_buffers_.data());
        cmd_buffers_.resize(framebuffers_.size());
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = cmd_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = (uint32_t)cmd_buffers_.size();
        vkAllocateCommandBuffers(device_, &alloc_info, cmd_buffers_.data());
    }

    overlay_.resize(width_, height_);
    LOGI("Surface recreated (%ux%u) — device, pipelines and atlas kept", width_, height_);
    return true;
}

void Renderer::Impl::recreate_swapchain() {
    VkExtent2D ext = surface_provider_.extent();
    if (ext.width == 0 || ext.height == 0) return;  // minimized; retry next draw()

    vkDeviceWaitIdle(device_);
    destroy_swapchain_resources();

    width_  = ext.width;
    height_ = ext.height;
    create_swapchain();
    create_framebuffers();

    // Swapchain image count is deterministic from caps.min/maxImageCount for
    // a given surface, so this is normally a no-op — guarded in case it ever
    // changes (e.g. a driver update) so cmd_buffers_[image_index] can't go
    // out of bounds in draw().
    if (cmd_buffers_.size() != framebuffers_.size()) {
        if (!cmd_buffers_.empty())
            vkFreeCommandBuffers(device_, cmd_pool_, (uint32_t)cmd_buffers_.size(), cmd_buffers_.data());
        cmd_buffers_.resize(framebuffers_.size());
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = cmd_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = (uint32_t)cmd_buffers_.size();
        vkAllocateCommandBuffers(device_, &alloc_info, cmd_buffers_.data());
    }

    overlay_.resize(width_, height_);
    // image_layer_ has no size-dependent resources (per-texture, not per-screen).

    LOGI("Swapchain recreated (%ux%u)", width_, height_);
}
