// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

#if defined(__ANDROID__)

void Renderer::Impl::update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb) {
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

void Renderer::Impl::clear_camera_frames() {
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
    hwb_lru_.clear();
        if (desc_pool_) vkResetDescriptorPool(device_, desc_pool_, 0);
    }
}

void Renderer::Impl::setup_hwb_resources(AHardwareBuffer* hwb) {
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
    // The driver's suggestion is right for a camera buffer, whose producer and
    // gralloc format agree. It is wrong for a decoded VIDEO frame: a P010
    // buffer carries no colourspace, so the driver suggests BT.709 for BT.2020
    // content and the matrix error reads as a grading choice rather than a
    // bug. set_external_colour() is how a consumer that KNOWS says so.
    ycbcr_ci.ycbcrModel = external_colour_set_ ? external_model_
                                               : hwb_format_props.suggestedYcbcrModel;
    ycbcr_ci.ycbcrRange = external_colour_set_ ? external_range_
                                               : hwb_format_props.suggestedYcbcrRange;
    LOGI("HWB ycbcr: model %d range %d (%s)", (int)ycbcr_ci.ycbcrModel,
         (int)ycbcr_ci.ycbcrRange, external_colour_set_ ? "consumer" : "driver-suggested");
    ycbcr_ci.components = hwb_format_props.samplerYcbcrConversionComponents;
    // The same argument as the model above, one step further in: the driver's
    // suggestion describes the BUFFER, and where the chroma samples sit is a
    // property of the ENCODE. Half a chroma sample of error puts a colour
    // fringe on one side of every hard saturated edge.
    ycbcr_ci.xChromaOffset = external_siting_set_ ? external_x_siting_
                                                  : hwb_format_props.suggestedXChromaOffset;
    ycbcr_ci.yChromaOffset = external_siting_set_ ? external_y_siting_
                                                  : hwb_format_props.suggestedYChromaOffset;
    LOGI("HWB chroma siting: x %d y %d (%s)", (int)ycbcr_ci.xChromaOffset,
         (int)ycbcr_ci.yChromaOffset, external_siting_set_ ? "container" : "driver-suggested");
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
    // Sized for a VIDEO stream, not a camera preview.
    //
    // 10 was right for a camera: its producer recycles a handful of gralloc
    // buffers and the working set never grows. A decoder's does — an
    // AImageReader feeding a player handed out 10 distinct AHardwareBuffers
    // within one second, at which point allocation failed, bind_hwb() left the
    // buffer uncached, draw()'s lookup missed, and the screen went black at a
    // perfectly healthy frame rate.
    //
    // FREE_DESCRIPTOR_SET_BIT is what makes the eviction in bind_hwb()
    // possible at all: without it a set can only be reclaimed by resetting the
    // whole pool.
    poolSize.descriptorCount = kMaxCachedHwb;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = kMaxCachedHwb;
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

    // Bake the swapchain's output encoding into the COMPOSITE pipeline too.
    // It was the only fragment stage that never got this, which was invisible
    // while its only consumer was a camera on an SDR swapchain and fatal the
    // moment one presented into HDR10 — see composite_frag.slang.
    OutputEncodeSpec compositeEncodeSpec(output_.encode);

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vert;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = frag;
    shaderStages[1].pName = "main";
    shaderStages[1].pSpecializationInfo = compositeEncodeSpec.get();
    // NOTE: composite_frag.slang lives in the font-engine submodule and does
    // not yet declare OUTPUT_ENCODE, so this pipeline is NOT specialized. It
    // only carries the Android camera composite, which is SDR by definition —
    // but see USAGE_hdr_output.md's remaining-work note before using it under
    // an HDR target.

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

    // Fragment push constant: the composite block above. Sized from the struct
    // rather than from a literal, which is how it came to say 32 while the
    // shader had grown past it.
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(CompositePush);

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

void Renderer::Impl::bind_hwb(AHardwareBuffer* hwb) {
    if (hwb_cache_.find(hwb) != hwb_cache_.end()) {
        touch_hwb(hwb);
        return;
    }

    // Make room first, so allocation below cannot be the thing that fails.
    // Least-recently-bound goes, and never the buffer currently on screen.
    //
    // vkDeviceWaitIdle before destroying: an evicted image may still be
    // referenced by a frame in flight, and this happens rarely enough (only
    // once the working set exceeds the pool) that the stall is cheaper than
    // tracking per-image fences.
    while (hwb_cache_.size() >= kMaxCachedHwb && !hwb_lru_.empty()) {
        AHardwareBuffer* victim = nullptr;
        for (auto it = hwb_lru_.begin(); it != hwb_lru_.end(); ++it) {
            if (*it == current_hwb_) continue;
            victim = *it;
            hwb_lru_.erase(it);
            break;
        }
        if (!victim) break;   // everything is the current buffer; nothing to give
        auto vit = hwb_cache_.find(victim);
        if (vit == hwb_cache_.end()) continue;
        vkDeviceWaitIdle(device_);
        vkFreeDescriptorSets(device_, desc_pool_, 1, &vit->second.desc_set);
        vkDestroyImageView(device_, vit->second.view, nullptr);
        vkDestroyImage(device_, vit->second.image, nullptr);
        vkFreeMemory(device_, vit->second.memory, nullptr);
        hwb_cache_.erase(vit);
    }

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
    // Every failure below used to return in silence, which presents as a black
    // screen with a healthy-looking frame rate and nothing in any log — the
    // frames arrive, the cache lookup in draw() misses, and nothing is
    // composited. Each one says so now, once.
    if (vkCreateImage(device_, &image_info, nullptr, &cache.image) != VK_SUCCESS) {
        LOGI("bind_hwb: vkCreateImage failed (externalFormat %llu, %ux%u)",
             (unsigned long long)last_external_format_, desc.width, desc.height);
        return;
    }

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

    if (vkAllocateMemory(device_, &alloc_info, nullptr, &cache.memory) != VK_SUCCESS) {
        LOGI("bind_hwb: vkAllocateMemory failed (%llu bytes, typeBits 0x%x)",
             (unsigned long long)hwb_props.allocationSize, hwb_props.memoryTypeBits);
        vkDestroyImage(device_, cache.image, nullptr);
        return;
    }

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

    if (vkCreateImageView(device_, &view_info, nullptr, &cache.view) != VK_SUCCESS) {
        LOGI("bind_hwb: vkCreateImageView failed");
        vkDestroyImage(device_, cache.image, nullptr);
        vkFreeMemory(device_, cache.memory, nullptr);
        return;
    }

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
        LOGI("bind_hwb: descriptor set allocation failed (%zu buffers cached, pool holds 10)",
             hwb_cache_.size());
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
    hwb_lru_.push_back(hwb);
}

// Most-recently-bound moves to the back; the front is the eviction candidate.
void Renderer::Impl::touch_hwb(AHardwareBuffer* hwb) {
    for (auto it = hwb_lru_.begin(); it != hwb_lru_.end(); ++it) {
        if (*it == hwb) { hwb_lru_.erase(it); break; }
    }
    hwb_lru_.push_back(hwb);
}
#endif  // __ANDROID__

void Renderer::Impl::set_external_colour(VkSamplerYcbcrModelConversion model,
                                   VkSamplerYcbcrRange range,
                                   const VkChromaLocation* xChromaOffset,
                                   const VkChromaLocation* yChromaOffset) {
    if (ycbcr_conversion_ != VK_NULL_HANDLE) {
        // The conversion is created on the first imported buffer and every
        // cached image view references it, so it cannot be swapped underneath
        // them. Saying so is better than silently keeping the old colour.
        LOGI("set_external_colour() ignored: the YCbCr conversion already exists. "
             "Call it before the first frame.");
        return;
    }
    external_colour_set_ = true;
    external_model_ = model;
    external_range_ = range;
    // Both or neither: a container that states one siting and not the other is
    // describing an encode nobody makes, and mixing a stated axis with a
    // suggested one is a worse answer than taking the driver's pair.
    if (xChromaOffset && yChromaOffset) {
        external_siting_set_ = true;
        external_x_siting_ = *xChromaOffset;
        external_y_siting_ = *yChromaOffset;
    }
}

// The platform-neutral entry point. See renderer.hh for why it exists.
void Renderer::Impl::update_external_frame(void* handle, std::function<void()> release_cb) {
#if defined(__ANDROID__)
    update_camera_frame(static_cast<AHardwareBuffer*>(handle), std::move(release_cb));
#else
    // No desktop external-image path yet. Release immediately rather than
    // silently holding the producer's buffer forever — a decoder's output pool
    // is small and stalls the moment one is not returned.
    (void)handle;
    if (release_cb) release_cb();
#endif
}
