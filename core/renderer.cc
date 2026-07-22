#include "renderer.hh"
#include "msdf.hh"
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

Renderer::Renderer(SurfaceProvider& surface, AssetReader& assets,
                   uint32_t desiredSwapchainImages)
    : surface_provider_(surface), assets_(assets),
      desired_swapchain_images_(desiredSwapchainImages) {
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
    image_layer_.init(device_, physical_dev_, assets_, render_pass_, cmd_pool_, queue_);
    initShapes();

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

void Renderer::draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg,
                    const std::vector<ImageDraw>& images,
                    const std::vector<ImageDraw>& foregroundImages,
                    const std::vector<float>& msdfQuads,
                    const std::vector<float>& shapeVerts) {
    if (!device_) return;
    int64_t draw_t0 = now_ns();

    const uint32_t frame = frame_index_;

    // Two frames in flight: wait only for THIS frame slot's previous use, so
    // CPU scene-building for frame N+1 overlaps the GPU executing frame N.
    // The dynamic buffers the CPU writes below (MSDF VBO, shape VBO) are
    // per-frame copies, so no write can race the other frame's read.
    //
    // Exception: the overlay compute path (curve buffer, tile/row buffers,
    // output image) is single-buffered — when this frame uses it, ALSO wait
    // out the other frame slot, restoring the old fully-serialized behavior
    // for exactly the frames that need it.
    vkWaitForFences(device_, 1, &in_flight_fences_[frame], VK_TRUE, UINT64_MAX);
    if (!overlay_curves.empty())
        vkWaitForFences(device_, 1, &in_flight_fences_[1 - frame], VK_TRUE, UINT64_MAX);
    int64_t t_afterwait = now_ns();

    // Retired textures may only be freed once EVERY submitted command buffer
    // that could bind them has finished — with two frames in flight that
    // means both frame fences, not just ours. Check the other one without
    // blocking; if it's still running, staging still gets reaped and the
    // retirees simply wait for a later pass.
    bool queueDrained =
        vkGetFenceStatus(device_, in_flight_fences_[1 - frame]) == VK_SUCCESS;
    image_layer_.collectGarbage(queueDrained);

    overlay_.uploadCurves(overlay_curves.data(),
                          static_cast<uint32_t>(overlay_curves.size() / OverlayRasterizer::CURVE_FLOATS));

    // Upload MSDF text quads into this frame slot's VBO.
    {
        uint32_t verts = static_cast<uint32_t>(msdfQuads.size() / 8);
        if (verts > kMaxMsdfVerts) verts = kMaxMsdfVerts;
        if (msdfReady() && msdfVboMapped_[frame] && verts > 0)
            std::memcpy(msdfVboMapped_[frame], msdfQuads.data(),
                        static_cast<size_t>(verts) * 8 * sizeof(float));
        msdfVertCount_ = verts;
    }

    // Upload SDF shape quads into this frame slot's VBO.
    {
        uint32_t verts = static_cast<uint32_t>(shapeVerts.size() / kShapeFloatsPerVert);
        if (verts > kMaxShapeVerts) verts = kMaxShapeVerts;
        if (shapePipeline_ != VK_NULL_HANDLE && shapeVboMapped_[frame] && verts > 0)
            std::memcpy(shapeVboMapped_[frame], shapeVerts.data(),
                        static_cast<size_t>(verts) * kShapeFloatsPerVert * sizeof(float));
        shapeVertCount_ = verts;
    }

    int64_t t_afterupload = now_ns();

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
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                            image_available_sems_[frame], VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // The swapchain died mid-transition (monitor move, mode change).
        // Recreate and retry the acquire IN THIS CALL instead of returning:
        // dropping the frame presented one whole interval of stale,
        // wrong-extent content — the "blinking horizontal black bars" seen
        // while crossing between monitors. (The failed acquire left the
        // semaphore unsignaled, so reusing it for the retry is valid.)
        recreate_swapchain();
        result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                       image_available_sems_[frame], VK_NULL_HANDLE, &image_index);
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return;
    }
    // VK_SUBOPTIMAL_KHR from acquire: the image is still perfectly usable —
    // render and present it, and let the present side's SUBOPTIMAL trigger
    // the recreate afterward. The old code bailed here, which both dropped
    // the frame AND left the acquire semaphore signaled (invalid to reuse
    // on the next acquire).
    int64_t t_afteracquire = now_ns();

    // The acquired image's command buffer may belong to the OTHER in-flight
    // frame — wait out whichever fence last submitted it before re-recording.
    if (image_index < image_fences_.size() &&
        image_fences_[image_index] != VK_NULL_HANDLE &&
        image_fences_[image_index] != in_flight_fences_[frame])
        vkWaitForFences(device_, 1, &image_fences_[image_index], VK_TRUE, UINT64_MAX);
    if (image_index < image_fences_.size())
        image_fences_[image_index] = in_flight_fences_[frame];

    vkResetFences(device_, 1, &in_flight_fences_[frame]);
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

    if (!images.empty()) {
        image_layer_.recordComposite(cmd_buffers_[image_index], images, width_, height_);
    }

    // SDF shape quads — the vector UI layer's fast path; same layer slot as
    // the overlay composite below (a host uses one or the other per frame).
    if (shapeVertCount_ > 0) {
        recordShapeDraw(cmd_buffers_[image_index], frame);
    }

    if (!overlay_curves.empty()) {
        overlay_.recordComposite(cmd_buffers_[image_index], overlay_rotation_deg);
    }

    if (!foregroundImages.empty()) {
        image_layer_.recordComposite(cmd_buffers_[image_index], foregroundImages, width_, height_);
    }

    if (msdfVertCount_ > 0) {
        recordMsdfDraw(cmd_buffers_[image_index], frame);
    }

    vkCmdEndRenderPass(cmd_buffers_[image_index]);
    vkEndCommandBuffer(cmd_buffers_[image_index]);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore wait_sems[] = {image_available_sems_[frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_sems;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffers_[image_index];

    VkSemaphore signal_sems[] = {render_finished_sems_[frame]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_sems;

    vkQueueSubmit(queue_, 1, &submit_info, in_flight_fences_[frame]);
    frame_index_ = 1 - frame;
    last_drawn_image_index_ = image_index;   // for readbackLastFrame()

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_sems;
    VkSwapchainKHR swapchains[] = {swapchain_};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    VkResult present_result = vkQueuePresentKHR(queue_, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain();
    }

    // ── Instrumentation: per-frame phase breakdown on slow frames ───────────
    int64_t t_end = now_ns();
    int64_t total = t_end - draw_t0;
    if (total > 50'000'000LL) {
        LOGI("SLOW draw %lldms: wait=%lld upload=%lld bind=%lld acquire=%lld rest=%lld",
             (long long)(total / 1'000'000),
             (long long)((t_afterwait    - draw_t0)        / 1'000'000),
             (long long)((t_afterupload  - t_afterwait)    / 1'000'000),
             (long long)((t_afterbind    - t_afterupload)  / 1'000'000),
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

bool Renderer::readbackLastFrame(std::vector<uint8_t>& rgba_out,
                                 uint32_t& out_w, uint32_t& out_h) {
    if (last_drawn_image_index_ >= swapchain_images_.size()) return false;
    VkImage image = swapchain_images_[last_drawn_image_index_];

    // All draw/present work must be finished before we touch the image.
    vkDeviceWaitIdle(device_);

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
        if (vkAllocateMemory(device_, &ai, nullptr, &staging_mem) != VK_SUCCESS) {
            vkDestroyBuffer(device_, staging, nullptr);
            return false;
        }
        vkBindBufferMemory(device_, staging, staging_mem, 0);
    }

    // One-shot command buffer: barrier image PRESENT_SRC -> TRANSFER_SRC,
    // copy the whole image to the buffer, barrier back to PRESENT_SRC.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = cmd_pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

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

    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(device_, &fci, nullptr, &fence);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, fence);
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);

    void* mapped = nullptr;
    vkMapMemory(device_, staging_mem, 0, bytes, 0, &mapped);
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

    // Optional extensions: request only what the loader reports, otherwise
    // instance creation would fail. VK_EXT_swapchain_colorspace exposes HDR
    // colorspaces (BT2020/HLG/PQ); absence just means we fall back to SDR.
    uint32_t ext_count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> avail(ext_count);
    vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, avail.data());
    auto has = [&](const char* name) {
        for (auto& e : avail)
            if (std::strcmp(e.extensionName, name) == 0) return true;
        return false;
    };
    if (has("VK_KHR_external_memory_capabilities"))
        extensions.push_back("VK_KHR_external_memory_capabilities");
    if (has("VK_KHR_get_physical_device_properties2"))
        extensions.push_back("VK_KHR_get_physical_device_properties2");
    if (has("VK_EXT_swapchain_colorspace")) {
        extensions.push_back("VK_EXT_swapchain_colorspace");
        ext_swapchain_colorspace_ = true;
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
    // is on screen, one queued, and one held by the compositor mid-hitch (Android).
    // The count is caller-configurable (constructor): each extra image is a full
    // window-sized allocation, so desktop callers request 3.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_dev_, surface_, &caps);
    uint32_t desired_images = desired_swapchain_images_;
    if (desired_images < 2) desired_images = 2;
    if (desired_images < caps.minImageCount) desired_images = caps.minImageCount;
    if (caps.maxImageCount > 0 && desired_images > caps.maxImageCount) desired_images = caps.maxImageCount;
    LOGI("Swapchain present mode=%d, images=%u", present_mode, desired_images);

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
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = composite_alpha;
    ci.presentMode      = present_mode;
    ci.clipped          = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, nullptr);
    // Per-image "which frame-fence last used this image" tracking for the
    // frames-in-flight scheme; reset on every (re)create since all prior
    // work was drained first.
    image_fences_.assign(img_count, VK_NULL_HANDLE);
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

    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        vkCreateSemaphore(device_, &sem_ci, nullptr, &image_available_sems_[f]);
        vkCreateSemaphore(device_, &sem_ci, nullptr, &render_finished_sems_[f]);
        vkCreateFence(device_, &fence_ci, nullptr, &in_flight_fences_[f]);
    }
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

void Renderer::destroy_swapchain_resources() {
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto iv : swapchain_image_views_) vkDestroyImageView(device_, iv, nullptr);
    swapchain_image_views_.clear();
    swapchain_images_.clear();
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

void Renderer::recreate_swapchain() {
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

// ── MSDF text pipeline ─────────────────────────────────────────────────────

void Renderer::initMsdf(const MsdfFont& font) {
    if (!device_ || !render_pass_) return;
    // Callers may release the font's CPU atlas pixels after upload
    // (MsdfFont::releaseAtlasPixels()); an upload needs them resident.
    if (font.atlas().empty()) {
        LOGE("initMsdf: atlas pixels not resident (call ensureAtlasLoaded first)");
        return;
    }
    if (msdfPipeline_ != VK_NULL_HANDLE) {
        // Re-entrant call: the atlas grew (a fallback font baked new glyphs
        // — see MsdfFont::bakeCodepoints()) and needs to be re-uploaded at
        // its new size. Wait for any in-flight frame still sampling the old
        // atlas/descriptor before tearing it down, then rebuild fresh below.
        vkDeviceWaitIdle(device_);
        cleanupMsdf();
    }

    uploadMsdfAtlas(font.atlas().data(), font.atlasW(), font.atlasH(), font.distanceRange());
    msdfIsMtsdf_ = font.isMtsdf() ? 1.0f : 0.0f;  // gates the shader's true-SDF blend
    createMsdfPipeline();

    // Vertex buffers (host-visible, persistently mapped) — one per frame in
    // flight so the CPU's upload for frame N+1 can't race frame N's read.
    const VkDeviceSize vbBytes = static_cast<VkDeviceSize>(kMaxMsdfVerts) * 8 * sizeof(float);
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        VkBufferCreateInfo vb{};
        vb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vb.size  = vbBytes;
        vb.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(device_, &vb, nullptr, &msdfVbo_[f]);
        VkMemoryRequirements vr{};
        vkGetBufferMemoryRequirements(device_, msdfVbo_[f], &vr);
        VkMemoryAllocateInfo va{};
        va.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        va.allocationSize = vr.size;
        va.memoryTypeIndex = find_memory_type(vr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &va, nullptr, &msdfVboMemory_[f]);
        vkBindBufferMemory(device_, msdfVbo_[f], msdfVboMemory_[f], 0);
        vkMapMemory(device_, msdfVboMemory_[f], 0, vbBytes, 0, &msdfVboMapped_[f]);
    }

    LOGI("MSDF pipeline ready (atlas %ux%u, pxRange %.1f)", msdfAtlasW_, msdfAtlasH_, msdfPxRange_);
}

void Renderer::uploadMsdfAtlas(const uint8_t* rgba, uint32_t w, uint32_t h, float pxRange) {
    msdfAtlasW_ = w;
    msdfAtlasH_ = h;
    msdfPxRange_ = pxRange;

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;

    // Staging buffer
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo sb{};
    sb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sb.size  = imageSize;
    sb.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(device_, &sb, nullptr, &stagingBuf);
    VkMemoryRequirements smr{};
    vkGetBufferMemoryRequirements(device_, stagingBuf, &smr);
    VkMemoryAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    sa.allocationSize = smr.size;
    sa.memoryTypeIndex = find_memory_type(smr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &sa, nullptr, &stagingMem);
    vkBindBufferMemory(device_, stagingBuf, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(device_, stagingMem);

    // Image
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCreateImage(device_, &ci, nullptr, &msdfAtlasImage_);
    VkMemoryRequirements ir{};
    vkGetImageMemoryRequirements(device_, msdfAtlasImage_, &ir);
    VkMemoryAllocateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ia.allocationSize = ir.size;
    ia.memoryTypeIndex = find_memory_type(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ia, nullptr, &msdfAtlasMemory_);
    vkBindImageMemory(device_, msdfAtlasImage_, msdfAtlasMemory_, 0);

    // Transition + copy via one-shot command buffer
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmd_pool_;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cba, &cmd);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image = msdfAtlasImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuf, msdfAtlasImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);

    vkDestroyBuffer(device_, stagingBuf, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

    // Image view
    VkImageViewCreateInfo vc{};
    vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vc.image = msdfAtlasImage_;
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vc.format = VK_FORMAT_R8G8B8A8_UNORM;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &vc, nullptr, &msdfAtlasView_);

    // Sampler
    VkSamplerCreateInfo sc{};
    sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sc.magFilter = VK_FILTER_LINEAR;
    sc.minFilter = VK_FILTER_LINEAR;
    sc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.maxAnisotropy = 1.0f;
    vkCreateSampler(device_, &sc, nullptr, &msdfAtlasSampler_);

    // Descriptor set
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    vkCreateDescriptorSetLayout(device_, &lci, nullptr, &msdfSetLayout_);

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    dpc.maxSets = 1;
    vkCreateDescriptorPool(device_, &dpc, nullptr, &msdfDescPool_);

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = msdfDescPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &msdfSetLayout_;
    vkAllocateDescriptorSets(device_, &dsai, &msdfDescSet_);

    VkDescriptorImageInfo di{};
    di.sampler = msdfAtlasSampler_;
    di.imageView = msdfAtlasView_;
    di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet wd{};
    wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd.dstSet = msdfDescSet_;
    wd.dstBinding = 0;
    wd.descriptorCount = 1;
    wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wd.pImageInfo = &di;
    vkUpdateDescriptorSets(device_, 1, &wd, 0, nullptr);
}

void Renderer::createMsdfPipeline() {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(float) * 8;  // screen.xy pxRange pad atlas.xy scroll.xy
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &msdfSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(device_, &pl, nullptr, &msdfPipelineLayout_);

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
    VkShaderModule vs = loadShader("shaders/msdf_vert.spv");
    VkShaderModule fs = loadShader("shaders/msdf_frag.spv");
    if (!vs || !fs) {
        LOGE("MSDF shaders missing");
        if (vs) vkDestroyShaderModule(device_, vs, nullptr);
        if (fs) vkDestroyShaderModule(device_, fs, nullptr);
        return;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT,   vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription bind{0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,       0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,       2 * sizeof(float)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * sizeof(float)},
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = msdfPipelineLayout_;
    gp.renderPass = render_pass_;
    gp.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &msdfPipeline_) != VK_SUCCESS) {
        LOGE("Failed to create MSDF pipeline");
        msdfPipeline_ = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
}

void Renderer::recordMsdfDraw(VkCommandBuffer cmd, uint32_t frame) {
    if (!msdfReady() || msdfVertCount_ == 0) return;

    VkViewport vp{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    VkRect2D   sc{{0, 0}, {width_, height_}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, msdfPipeline_);
    float push[8] = {static_cast<float>(width_), static_cast<float>(height_),
                     msdfPxRange_, msdfIsMtsdf_,
                     static_cast<float>(msdfAtlasW_), static_cast<float>(msdfAtlasH_),
                     0.0f, 0.0f};
    vkCmdPushConstants(cmd, msdfPipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), push);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, msdfPipelineLayout_,
                            0, 1, &msdfDescSet_, 0, nullptr);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &msdfVbo_[frame], &off);
    vkCmdDraw(cmd, msdfVertCount_, 1, 0, 0);
}

void Renderer::cleanupMsdf() {
    // Every handle is nulled out after destruction (not just guarded on
    // entry) so this is safe to call twice in a row and so initMsdf() can
    // reliably tell "torn down" from "still live" to rebuild the atlas at a
    // larger size (see initMsdf()'s reinit branch) instead of silently
    // no-op'ing on a second call.
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        if (msdfVboMapped_[f]) { vkUnmapMemory(device_, msdfVboMemory_[f]); msdfVboMapped_[f] = nullptr; }
        if (msdfVbo_[f])       { vkDestroyBuffer(device_, msdfVbo_[f], nullptr); msdfVbo_[f] = VK_NULL_HANDLE; }
        if (msdfVboMemory_[f]) { vkFreeMemory(device_, msdfVboMemory_[f], nullptr); msdfVboMemory_[f] = VK_NULL_HANDLE; }
    }
    if (msdfPipeline_)   { vkDestroyPipeline(device_, msdfPipeline_, nullptr); msdfPipeline_ = VK_NULL_HANDLE; }
    if (msdfPipelineLayout_) { vkDestroyPipelineLayout(device_, msdfPipelineLayout_, nullptr); msdfPipelineLayout_ = VK_NULL_HANDLE; }
    if (msdfDescPool_)   { vkDestroyDescriptorPool(device_, msdfDescPool_, nullptr); msdfDescPool_ = VK_NULL_HANDLE; }
    if (msdfSetLayout_)  { vkDestroyDescriptorSetLayout(device_, msdfSetLayout_, nullptr); msdfSetLayout_ = VK_NULL_HANDLE; }
    if (msdfAtlasSampler_) { vkDestroySampler(device_, msdfAtlasSampler_, nullptr); msdfAtlasSampler_ = VK_NULL_HANDLE; }
    if (msdfAtlasView_)  { vkDestroyImageView(device_, msdfAtlasView_, nullptr); msdfAtlasView_ = VK_NULL_HANDLE; }
    if (msdfAtlasImage_) { vkDestroyImage(device_, msdfAtlasImage_, nullptr); msdfAtlasImage_ = VK_NULL_HANDLE; }
    if (msdfAtlasMemory_) { vkFreeMemory(device_, msdfAtlasMemory_, nullptr); msdfAtlasMemory_ = VK_NULL_HANDLE; }
}

// ── SDF shape pipeline ───────────────────────────────────────────────────────

void Renderer::initShapes() {
    if (!device_ || !render_pass_) return;

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.size = sizeof(float) * 2;  // screenW, screenH
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(device_, &pl, nullptr, &shapePipelineLayout_);

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
    VkShaderModule vs = loadShader("shaders/shape_vert.spv");
    VkShaderModule fs = loadShader("shaders/shape_frag.spv");
    if (!vs || !fs) {
        // Missing shaders (older asset dir): hosts that never call
        // Canvas::useShapes() are unaffected; ones that do simply draw no
        // shapes. Loudly logged so it can't pass silently.
        LOGE("shape shaders missing — SDF shape pipeline disabled");
        if (vs) vkDestroyShaderModule(device_, vs, nullptr);
        if (fs) vkDestroyShaderModule(device_, fs, nullptr);
        return;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT,   vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    VkVertexInputBindingDescription bind{0, kShapeFloatsPerVert * sizeof(float),
                                          VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,        0},                  // pos
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,  2 * sizeof(float)},  // color
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,  6 * sizeof(float)},  // data0
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 10 * sizeof(float)},  // data1
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Same straight-alpha blend as the MSDF text pipeline.
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = shapePipelineLayout_;
    gp.renderPass = render_pass_;
    gp.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &shapePipeline_) != VK_SUCCESS) {
        LOGE("Failed to create shape pipeline");
        shapePipeline_ = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
    if (!shapePipeline_) return;

    // Per-frame vertex buffers (host-visible, persistently mapped).
    const VkDeviceSize vbBytes =
        static_cast<VkDeviceSize>(kMaxShapeVerts) * kShapeFloatsPerVert * sizeof(float);
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        VkBufferCreateInfo vb{};
        vb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vb.size  = vbBytes;
        vb.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vkCreateBuffer(device_, &vb, nullptr, &shapeVbo_[f]);
        VkMemoryRequirements vr{};
        vkGetBufferMemoryRequirements(device_, shapeVbo_[f], &vr);
        VkMemoryAllocateInfo va{};
        va.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        va.allocationSize = vr.size;
        va.memoryTypeIndex = find_memory_type(vr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &va, nullptr, &shapeVboMemory_[f]);
        vkBindBufferMemory(device_, shapeVbo_[f], shapeVboMemory_[f], 0);
        vkMapMemory(device_, shapeVboMemory_[f], 0, vbBytes, 0, &shapeVboMapped_[f]);
    }
    LOGI("SDF shape pipeline ready");
}

void Renderer::recordShapeDraw(VkCommandBuffer cmd, uint32_t frame) {
    if (shapePipeline_ == VK_NULL_HANDLE || shapeVertCount_ == 0) return;

    VkViewport vp{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    VkRect2D   sc{{0, 0}, {width_, height_}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shapePipeline_);
    float push[2] = {static_cast<float>(width_), static_cast<float>(height_)};
    vkCmdPushConstants(cmd, shapePipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(push), push);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &shapeVbo_[frame], &off);
    vkCmdDraw(cmd, shapeVertCount_, 1, 0, 0);
}

void Renderer::cleanupShapes() {
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        if (shapeVboMapped_[f]) { vkUnmapMemory(device_, shapeVboMemory_[f]); shapeVboMapped_[f] = nullptr; }
        if (shapeVbo_[f])       { vkDestroyBuffer(device_, shapeVbo_[f], nullptr); shapeVbo_[f] = VK_NULL_HANDLE; }
        if (shapeVboMemory_[f]) { vkFreeMemory(device_, shapeVboMemory_[f], nullptr); shapeVboMemory_[f] = VK_NULL_HANDLE; }
    }
    if (shapePipeline_)       { vkDestroyPipeline(device_, shapePipeline_, nullptr); shapePipeline_ = VK_NULL_HANDLE; }
    if (shapePipelineLayout_) { vkDestroyPipelineLayout(device_, shapePipelineLayout_, nullptr); shapePipelineLayout_ = VK_NULL_HANDLE; }
}

void Renderer::cleanup() {
    if (device_) vkDeviceWaitIdle(device_);
    overlay_.cleanup();
    image_layer_.cleanup();
    cleanupMsdf();
    cleanupShapes();
#if defined(__ANDROID__)
    cleanup_hwb_resources();
#endif

    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        if (image_available_sems_[f]) vkDestroySemaphore(device_, image_available_sems_[f], nullptr);
        if (render_finished_sems_[f]) vkDestroySemaphore(device_, render_finished_sems_[f], nullptr);
        if (in_flight_fences_[f])     vkDestroyFence(device_, in_flight_fences_[f], nullptr);
    }
    if (cmd_pool_)            vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    destroy_swapchain_resources();
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (device_)    vkDestroyDevice(device_, nullptr);
    if (surface_)   vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)  vkDestroyInstance(instance_, nullptr);
}
