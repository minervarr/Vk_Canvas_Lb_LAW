#include "renderer.hh"
#include "log.hh"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <chrono>

static inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#define LOG_TAG "vk_canvas"
#define LOGI(...) VCE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGE(...) VCE_LOGE(LOG_TAG, __VA_ARGS__)

Renderer::Renderer(SurfaceProvider& surface, AssetReader& assets)
    : surface_provider_(surface), assets_(assets) {
    VkExtent2D ext = surface_provider_.extent();
    width_  = ext.width;
    height_ = ext.height;
    create_instance();
    create_surface();
    pick_physical_device();
    create_logical_device();
    create_swapchain();
    create_render_pass();
    create_framebuffers();
    create_command_buffers();
    create_sync_objects();
    overlay_.init(device_, physical_dev_, assets_, render_pass_, width_, height_);

    LOGI("Renderer ready (%ux%u)", width_, height_);
}

Renderer::~Renderer() {
    cleanup();
}

#if defined(__ANDROID__)
void Renderer::update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb) {
    // ── Instrumentation: rate camera frames ARRIVE at the renderer ──────────
    {
        int64_t t = now_ns();
        if (pv_prev_ns_) { int64_t g = t - pv_prev_ns_; if (g > pv_max_gap_ns_) pv_max_gap_ns_ = g; }
        pv_prev_ns_ = t;
        if (pv_window_ns_ == 0) pv_window_ns_ = t;
        ++pv_frames_;
        if (t - pv_window_ns_ >= 1'000'000'000LL) {
            LOGI("preview-IN: %d fps, worst gap %lld ms", pv_frames_, (long long)(pv_max_gap_ns_ / 1'000'000));
            pv_window_ns_ = t; pv_frames_ = 0; pv_max_gap_ns_ = 0;
        }
    }

    if (!hwb) {
        if (release_cb) release_cb();
        return;
    }
    
    AHardwareBuffer_acquire(hwb);
    
    std::lock_guard<std::mutex> lock(hwb_mutex_);
    if (pending_hwb_) {
        AHardwareBuffer_release(pending_hwb_);
    }
    if (pending_release_cb_) {
        pending_release_cb_();
        pending_release_cb_ = nullptr;
    }
    pending_hwb_ = hwb;
    pending_release_cb_ = std::move(release_cb);

    // Signal the render loop that a fresh frame is ready and wake it from its
    // blocking poll, so it presents promptly at the camera's cadence.
    frame_ready_.store(true, std::memory_order_release);
    if (waker_) waker_->wake();
}

void Renderer::clear_camera_frames() {
    if (device_) vkDeviceWaitIdle(device_);
    
    std::lock_guard<std::mutex> lock(hwb_mutex_);
    if (pending_hwb_) {
        AHardwareBuffer_release(pending_hwb_);
        pending_hwb_ = nullptr;
    }
    if (current_hwb_) {
        AHardwareBuffer_release(current_hwb_);
        current_hwb_ = nullptr;
    }
    if (current_release_cb_) {
        current_release_cb_();
        current_release_cb_ = nullptr;
    }
    if (pending_release_cb_) {
        pending_release_cb_();
        pending_release_cb_ = nullptr;
    }

    // Evict the per-HWB Vulkan objects and reclaim their descriptor sets. The
    // camera hands out new AHardwareBuffer pointers every time its session is
    // (re)started — e.g. on a lifecycle bounce from a USB hotplug — and each new
    // buffer permanently consumed a descriptor set from a fixed-size pool. Left
    // unchecked the pool (maxSets=10) overflows after a few restarts and
    // bind_hwb() writes to a failed (null) descriptor set → driver crash.
    if (device_) {
        for (auto& kv : hwb_cache_) {
            if (kv.second.view)   vkDestroyImageView(device_, kv.second.view, nullptr);
            if (kv.second.image)  vkDestroyImage(device_, kv.second.image, nullptr);
            if (kv.second.memory) vkFreeMemory(device_, kv.second.memory, nullptr);
        }
        hwb_cache_.clear();
        if (desc_pool_) vkResetDescriptorPool(device_, desc_pool_, 0);
    }
}

void Renderer::setup_hwb_resources(AHardwareBuffer* hwb) {
    VkAndroidHardwareBufferFormatPropertiesANDROID hwb_format_props{};
    hwb_format_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;

    VkAndroidHardwareBufferPropertiesANDROID hwb_props{};
    hwb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    hwb_props.pNext = &hwb_format_props;

    vkGetAndroidHardwareBufferPropertiesANDROID_(device_, hwb, &hwb_props);
    last_external_format_ = hwb_format_props.externalFormat;
    LOGI("HWB externalFormat: %llu", (unsigned long long)last_external_format_);

    VkExternalFormatANDROID ext_fmt{};
    ext_fmt.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ext_fmt.externalFormat = last_external_format_;

    VkSamplerYcbcrConversionCreateInfo ycbcr_ci{};
    ycbcr_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
    ycbcr_ci.pNext = &ext_fmt;
    ycbcr_ci.format = VK_FORMAT_UNDEFINED;
    ycbcr_ci.ycbcrModel = hwb_format_props.suggestedYcbcrModel;
    ycbcr_ci.ycbcrRange = hwb_format_props.suggestedYcbcrRange;
    ycbcr_ci.components = hwb_format_props.samplerYcbcrConversionComponents;
    ycbcr_ci.xChromaOffset = hwb_format_props.suggestedXChromaOffset;
    ycbcr_ci.yChromaOffset = hwb_format_props.suggestedYChromaOffset;
    ycbcr_ci.chromaFilter = VK_FILTER_LINEAR;
    ycbcr_ci.forceExplicitReconstruction = VK_FALSE;

    if (vkCreateSamplerYcbcrConversion_(device_, &ycbcr_ci, nullptr, &ycbcr_conversion_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create YCbCr conversion");
    }

    VkSamplerYcbcrConversionInfo sampler_ycbcr_info{};
    sampler_ycbcr_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    sampler_ycbcr_info.conversion = ycbcr_conversion_;

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.pNext = &sampler_ycbcr_info;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    vkCreateSampler(device_, &sampler_info, nullptr, &hwb_sampler_);

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = &hwb_sampler_;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;
    vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &desc_layout_);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 10;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 10;
    vkCreateDescriptorPool(device_, &poolInfo, nullptr, &desc_pool_);

    

    // Pipeline
    auto loadShader = [this](const char* path) -> VkShaderModule {
        std::vector<uint8_t> code;
        if (!assets_.read(path, code)) return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod;
        vkCreateShaderModule(device_, &ci, nullptr, &mod);
        return mod;
    };

    VkShaderModule vert = loadShader("shaders/composite_vert.spv");
    VkShaderModule frag = loadShader("shaders/composite_frag.spv");

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vert;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = frag;
    shaderStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamic_states;

    // Fragment push constant: the HLG tone-map flag (1 float, padded to 16).
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = 16;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &desc_layout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipeline_layout_);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline_layout_;
    pipelineInfo.renderPass = render_pass_;
    pipelineInfo.subpass = 0;
    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
}

void Renderer::bind_hwb(AHardwareBuffer* hwb) {
    if (hwb_cache_.find(hwb) != hwb_cache_.end()) return;

    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(hwb, &desc);

    VkExternalMemoryImageCreateInfo ext_mem_ci{};
    ext_mem_ci.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_mem_ci.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkExternalFormatANDROID ext_fmt{};
    ext_fmt.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
    ext_fmt.pNext = &ext_mem_ci;
    ext_fmt.externalFormat = last_external_format_;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &ext_fmt;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = desc.width;
    image_info.extent.height = desc.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_UNDEFINED;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    HwbCache cache;
    if (vkCreateImage(device_, &image_info, nullptr, &cache.image) != VK_SUCCESS) return;

    VkImportAndroidHardwareBufferInfoANDROID import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
    import_info.buffer = hwb;

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext = &import_info;
    dedicated_info.image = cache.image;

    VkAndroidHardwareBufferPropertiesANDROID hwb_props{};
    hwb_props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
    vkGetAndroidHardwareBufferPropertiesANDROID_(device_, hwb, &hwb_props);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &dedicated_info;
    alloc_info.allocationSize = hwb_props.allocationSize;
    alloc_info.memoryTypeIndex = find_memory_type(hwb_props.memoryTypeBits, 0);

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &cache.memory) != VK_SUCCESS) return;

    vkBindImageMemory(device_, cache.image, cache.memory, 0);

    VkSamplerYcbcrConversionInfo ycbcr_info{};
    ycbcr_info.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
    ycbcr_info.conversion = ycbcr_conversion_;

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.pNext = &ycbcr_info;
    view_info.image = cache.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_UNDEFINED;
    view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_, &view_info, nullptr, &cache.view) != VK_SUCCESS) return;

    VkDescriptorSetAllocateInfo alloc_info_desc{};
    alloc_info_desc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info_desc.descriptorPool = desc_pool_;
    alloc_info_desc.descriptorSetCount = 1;
    alloc_info_desc.pSetLayouts = &desc_layout_;
    if (vkAllocateDescriptorSets(device_, &alloc_info_desc, &cache.desc_set) != VK_SUCCESS ||
        cache.desc_set == VK_NULL_HANDLE) {
        // Pool exhausted (or other failure) — don't write to a null set (would
        // crash in the driver). Drop this frame's resources and skip binding;
        // the next clear_camera_frames() resets the pool.
        vkDestroyImageView(device_, cache.view, nullptr);
        vkDestroyImage(device_, cache.image, nullptr);
        vkFreeMemory(device_, cache.memory, nullptr);
        return;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = cache.view;
    imageInfo.sampler = hwb_sampler_;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = cache.desc_set;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

    hwb_cache_[hwb] = cache;
}
#endif  // __ANDROID__

void Renderer::draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg) {
    if (!device_) return;
    int64_t draw_t0 = now_ns();
    overlay_.uploadCurves(overlay_curves.data(),
                          static_cast<uint32_t>(overlay_curves.size() / OverlayRasterizer::CURVE_FLOATS));
    int64_t t_afterupload = now_ns();
    vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);
    int64_t t_afterwait = now_ns();

#if defined(__ANDROID__)
    AHardwareBuffer* hwb_to_bind = nullptr;
    std::function<void()> release_to_call;
    {
        std::lock_guard<std::mutex> lock(hwb_mutex_);
        if (pending_hwb_) {
            hwb_to_bind = pending_hwb_;
            release_to_call = std::move(current_release_cb_);
            current_release_cb_ = nullptr;
            current_release_cb_ = std::move(pending_release_cb_);
            pending_release_cb_ = nullptr;
            pending_hwb_ = nullptr;
        }
    }

    if (release_to_call) {
        release_to_call();
    }

    if (hwb_to_bind) {
        if (!ycbcr_conversion_) {
            setup_hwb_resources(hwb_to_bind);
        }
        bind_hwb(hwb_to_bind);
        if (current_hwb_) {
            AHardwareBuffer_release(current_hwb_);
        }
        current_hwb_ = hwb_to_bind;
    }
#endif  // __ANDROID__
    int64_t t_afterbind = now_ns();

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_sem_, VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return;
    int64_t t_afteracquire = now_ns();

    vkResetFences(device_, 1, &in_flight_fence_);
    vkResetCommandBuffer(cmd_buffers_[image_index], 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd_buffers_[image_index], &begin_info);

    if (!overlay_curves.empty()) {
        overlay_.recordDispatch(cmd_buffers_[image_index]);
    }

    VkRenderPassBeginInfo rp_info{};
    rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_info.renderPass = render_pass_;
    rp_info.framebuffer = framebuffers_[image_index];
    rp_info.renderArea.offset = {0, 0};
    rp_info.renderArea.extent = {width_, height_};

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    rp_info.clearValueCount = 1;
    rp_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd_buffers_[image_index], &rp_info, VK_SUBPASS_CONTENTS_INLINE);

#if defined(__ANDROID__)
    auto hwb_it = current_hwb_ ? hwb_cache_.find(current_hwb_) : hwb_cache_.end();
    if (pipeline_ && hwb_it != hwb_cache_.end() && hwb_it->second.desc_set != VK_NULL_HANDLE) {
        AHardwareBuffer_Desc desc;
        AHardwareBuffer_describe(current_hwb_, &desc);
        
        // Android camera frames are typically 90 degrees rotated.
        float raw_w = desc.height;
        float raw_h = desc.width;
        float scale = std::min((float)width_ / raw_w, (float)height_ / raw_h);
        float draw_w = raw_w * scale;
        float draw_h = raw_h * scale;
        float x_offset = (width_ - draw_w) / 2.0f;
        float y_offset = (height_ - draw_h) / 2.0f;
        
        VkViewport viewport{};
        viewport.x = x_offset;
        viewport.y = y_offset;
        viewport.width = draw_w;
        viewport.height = draw_h;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd_buffers_[image_index], 0, 1, &viewport);
        
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {width_, height_};
        vkCmdSetScissor(cmd_buffers_[image_index], 0, 1, &scissor);

        vkCmdBindPipeline(cmd_buffers_[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd_buffers_[image_index], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &hwb_it->second.desc_set, 0, nullptr);
        float pc[4] = { camera_hlg_, 0, 0, 0 };
        vkCmdPushConstants(cmd_buffers_[image_index], pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
        vkCmdDraw(cmd_buffers_[image_index], 3, 1, 0, 0);
    }
#endif  // __ANDROID__

    if (!overlay_curves.empty()) {
        overlay_.recordComposite(cmd_buffers_[image_index], overlay_rotation_deg);
    }

    vkCmdEndRenderPass(cmd_buffers_[image_index]);
    vkEndCommandBuffer(cmd_buffers_[image_index]);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore wait_sems[] = {image_available_sem_};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_sems;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffers_[image_index];

    VkSemaphore signal_sems[] = {render_finished_sem_};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_sems;

    vkQueueSubmit(queue_, 1, &submit_info, in_flight_fence_);

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_sems;
    VkSwapchainKHR swapchains[] = {swapchain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    vkQueuePresentKHR(queue_, &present_info);

    // ── Instrumentation: per-frame phase breakdown on slow frames ───────────
    int64_t t_end = now_ns();
    int64_t total = t_end - draw_t0;
    if (total > 50'000'000LL) {
        LOGI("SLOW draw %lldms: upload=%lld wait=%lld bind=%lld acquire=%lld rest=%lld",
             (long long)(total / 1'000'000),
             (long long)((t_afterupload  - draw_t0)        / 1'000'000),
             (long long)((t_afterwait    - t_afterupload)  / 1'000'000),
             (long long)((t_afterbind    - t_afterwait)    / 1'000'000),
             (long long)((t_afteracquire - t_afterbind)    / 1'000'000),
             (long long)((t_end          - t_afteracquire) / 1'000'000));
    }
    {
        if (total > dr_max_ns_) dr_max_ns_ = total;
        if (dr_window_ns_ == 0) dr_window_ns_ = draw_t0;
        ++dr_frames_;
        if (t_end - dr_window_ns_ >= 1'000'000'000LL) {
            LOGI("render: %d fps, slowest frame %lld ms", dr_frames_, (long long)(dr_max_ns_ / 1'000'000));
            dr_window_ns_ = t_end; dr_frames_ = 0; dr_max_ns_ = 0;
        }
    }
}

uint32_t Renderer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_dev_, &mem_props);
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) && (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void Renderer::create_instance() {
    VkApplicationInfo app_info{};
    app_info.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "vk_canvas";
    app_info.apiVersion       = VK_API_VERSION_1_1;

    // Windowing extensions come from the platform's SurfaceProvider
    // (VK_KHR_surface + VK_KHR_android_surface / VK_KHR_win32_surface / ...).
    std::vector<const char*> extensions = surface_provider_.instance_extensions();
    extensions.push_back("VK_KHR_external_memory_capabilities");
    extensions.push_back("VK_KHR_get_physical_device_properties2");

    // VK_EXT_swapchain_colorspace exposes HDR colorspaces (BT2020/HLG/PQ) on the
    // surface. Only request it if the loader reports it, otherwise instance
    // creation would fail; absence just means we fall back to SDR.
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> avail(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, avail.data());
    for (auto& e : avail) {
        if (std::strcmp(e.extensionName, "VK_EXT_swapchain_colorspace") == 0) {
            extensions.push_back("VK_EXT_swapchain_colorspace");
            ext_swapchain_colorspace_ = true;
            break;
        }
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app_info;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");
}

void Renderer::create_surface() {
    surface_ = surface_provider_.create(instance_);
    if (surface_ == VK_NULL_HANDLE)
        throw std::runtime_error("SurfaceProvider::create failed");
}

void Renderer::pick_physical_device() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) throw std::runtime_error("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance_, &count, devs.data());
    physical_dev_ = devs[0];

    // Capability scaffolding: optional techniques gate on caps_, never on the
    // platform, so a capable phone and a weak desktop GPU each get what they
    // can actually do.
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_dev_, &props);
    caps_.api_version                       = props.apiVersion;
    caps_.max_image_dim_2d                  = props.limits.maxImageDimension2D;
    caps_.max_compute_workgroup_invocations = props.limits.maxComputeWorkGroupInvocations;
    caps_.max_storage_buffer_range          = props.limits.maxStorageBufferRange;

    uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_dev_, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(physical_dev_, nullptr, &ext_count, exts.data());
    for (auto& e : exts) {
        if (!std::strcmp(e.extensionName, "VK_KHR_sampler_ycbcr_conversion"))
            caps_.has_sampler_ycbcr_conversion = true;
        if (!std::strcmp(e.extensionName, "VK_ANDROID_external_memory_android_hardware_buffer"))
            caps_.has_external_memory_ahb = true;
    }
}

void Renderer::create_logical_device() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = 0;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> dev_exts = { "VK_KHR_swapchain" };
#if defined(__ANDROID__)
    // Camera AHardwareBuffer import chain (Android-only).
    dev_exts.insert(dev_exts.end(), {
        "VK_KHR_sampler_ycbcr_conversion",
        "VK_KHR_external_memory",
        "VK_ANDROID_external_memory_android_hardware_buffer",
        "VK_EXT_queue_family_foreign",
        "VK_KHR_bind_memory2",
        "VK_KHR_maintenance1"
    });
#endif

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = static_cast<uint32_t>(dev_exts.size());
    ci.ppEnabledExtensionNames = dev_exts.data();
    ci.pEnabledFeatures        = &features;

    if (vkCreateDevice(physical_dev_, &ci, nullptr, &device_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");

    vkGetDeviceQueue(device_, 0, 0, &queue_);

#if defined(__ANDROID__)
    vkGetAndroidHardwareBufferPropertiesANDROID_ =
        (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(device_, "vkGetAndroidHardwareBufferPropertiesANDROID");
    vkCreateSamplerYcbcrConversion_ =
        (PFN_vkCreateSamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkCreateSamplerYcbcrConversion");
    vkDestroySamplerYcbcrConversion_ =
        (PFN_vkDestroySamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkDestroySamplerYcbcrConversion");
    if (!vkCreateSamplerYcbcrConversion_) {
        vkCreateSamplerYcbcrConversion_ = (PFN_vkCreateSamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkCreateSamplerYcbcrConversionKHR");
        vkDestroySamplerYcbcrConversion_ = (PFN_vkDestroySamplerYcbcrConversion)vkGetDeviceProcAddr(device_, "vkDestroySamplerYcbcrConversionKHR");
    }
#endif
}

void Renderer::create_swapchain() {
    // Preview is presented in SDR. The camera's preview stream is an SDR output
    // (the HLG/10-bit path is used only for the video encoder), so an 8-bit sRGB
    // swapchain shows correct, un-washed colors. Presenting HLG-encoded pixels to
    // the only 10-bit colorspace this panel exposes (BT2020 *linear*) made the
    // preview look washed out — the display read non-linear data as linear.
    swapchain_format_     = VK_FORMAT_R8G8B8A8_UNORM;
    swapchain_colorspace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_hdr_        = false;
    LOGI("Swapchain: SDR 8-bit (fmt=%d)", swapchain_format_);

    // Present mode: prefer MAILBOX over FIFO. With FIFO the present queue is a
    // hard line — vkQueuePresentKHR/acquire block until the compositor releases an
    // image at the display's vsync cadence. While recording, the compositor
    // periodically holds our image ~60ms (status-bar inset churn, extra
    // composition passes), which with our tight image count starved rendering and
    // froze the preview ~1Hz. MAILBOX never blocks the producer: we always get a
    // free image to render into and the newest frame replaces the queued one, so a
    // transient compositor hold no longer stalls us. FIFO is the guaranteed
    // fallback if MAILBOX is unsupported.
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    {
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
    // is on screen, one queued, and one held by the compositor mid-hitch.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_dev_, surface_, &caps);
    uint32_t desired_images = 4;
    if (desired_images < caps.minImageCount) desired_images = caps.minImageCount;
    if (caps.maxImageCount > 0 && desired_images > caps.maxImageCount) desired_images = caps.maxImageCount;
    LOGI("Swapchain present mode=%d, images=%u", present_mode, desired_images);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = surface_;
    ci.minImageCount    = desired_images;
    ci.imageFormat      = swapchain_format_;
    ci.imageColorSpace  = swapchain_colorspace_;
    ci.imageExtent      = { width_, height_ };
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.presentMode      = present_mode;
    ci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");
}

void Renderer::create_render_pass() {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swapchain_format_;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

    vkCreateRenderPass(device_, &ci, nullptr, &render_pass_);
}

void Renderer::create_framebuffers() {
    uint32_t image_count;
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_image_views_.resize(image_count);
    framebuffers_.resize(image_count);

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
    }
}

void Renderer::create_command_buffers() {
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_ci.queueFamilyIndex = 0;

    vkCreateCommandPool(device_, &pool_ci, nullptr, &cmd_pool_);

    cmd_buffers_.resize(framebuffers_.size());
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t)cmd_buffers_.size();

    vkAllocateCommandBuffers(device_, &alloc_info, cmd_buffers_.data());
}

void Renderer::create_sync_objects() {
    VkSemaphoreCreateInfo sem_ci{};
    sem_ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    vkCreateSemaphore(device_, &sem_ci, nullptr, &image_available_sem_);
    vkCreateSemaphore(device_, &sem_ci, nullptr, &render_finished_sem_);
    vkCreateFence(device_, &fence_ci, nullptr, &in_flight_fence_);
}

#if defined(__ANDROID__)
void Renderer::cleanup_hwb_resources() {
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

    if (desc_pool_) { vkDestroyDescriptorPool(device_, desc_pool_, nullptr); desc_pool_ = VK_NULL_HANDLE; }
    if (desc_layout_) { vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr); desc_layout_ = VK_NULL_HANDLE; }

    if (hwb_sampler_) { vkDestroySampler(device_, hwb_sampler_, nullptr); hwb_sampler_ = VK_NULL_HANDLE; }
    if (ycbcr_conversion_ && vkDestroySamplerYcbcrConversion_) { vkDestroySamplerYcbcrConversion_(device_, ycbcr_conversion_, nullptr); ycbcr_conversion_ = VK_NULL_HANDLE; }
    clear_camera_frames();
}
#endif  // __ANDROID__

void Renderer::cleanup() {
    if (device_) vkDeviceWaitIdle(device_);
    overlay_.cleanup();
#if defined(__ANDROID__)
    cleanup_hwb_resources();
#endif

    if (image_available_sem_) vkDestroySemaphore(device_, image_available_sem_, nullptr);
    if (render_finished_sem_) vkDestroySemaphore(device_, render_finished_sem_, nullptr);
    if (in_flight_fence_)     vkDestroyFence(device_, in_flight_fence_, nullptr);
    if (cmd_pool_)            vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchain_image_views_) vkDestroyImageView(device_, iv, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (device_)    vkDestroyDevice(device_, nullptr);
    if (surface_)   vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)  vkDestroyInstance(instance_, nullptr);
}
