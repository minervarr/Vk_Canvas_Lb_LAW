// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

bool Renderer::Impl::ensureShapeVboCapacity(uint32_t frame, uint32_t verts) {
    if (verts <= shapeVboVerts_[frame] && shapeVboMapped_[frame]) return true;

    uint32_t want = shapeVboVerts_[frame] ? shapeVboVerts_[frame] : kInitialShapeVerts;
    while (want < verts) want *= 2;
    want -= want % kShapeVertsPerQuad;

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(want) *
                               kShapeFloatsPerVert * sizeof(float);

    if (shapeVboMapped_[frame]) { vkUnmapMemory(device_, shapeVboMemory_[frame]); shapeVboMapped_[frame] = nullptr; }
    if (shapeVbo_[frame])       { vkDestroyBuffer(device_, shapeVbo_[frame], nullptr); shapeVbo_[frame] = VK_NULL_HANDLE; }
    if (shapeVboMemory_[frame]) { vkFreeMemory(device_, shapeVboMemory_[frame], nullptr); shapeVboMemory_[frame] = VK_NULL_HANDLE; }
    shapeVboVerts_[frame] = 0;

    VkBufferCreateInfo vb{};
    vb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vb.size  = bytes;
    vb.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &vb, nullptr, &shapeVbo_[frame]) != VK_SUCCESS) {
        LOGE("shape VBO: cannot create a buffer for %u vertices", want);
        return false;
    }
    VkMemoryRequirements vr{};
    vkGetBufferMemoryRequirements(device_, shapeVbo_[frame], &vr);
    VkMemoryAllocateInfo va{};
    va.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    va.allocationSize = vr.size;
    va.memoryTypeIndex = find_memory_type(vr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &va, nullptr, &shapeVboMemory_[frame]) != VK_SUCCESS) {
        LOGE("shape VBO: cannot allocate %llu bytes for %u vertices",
             (unsigned long long)bytes, want);
        vkDestroyBuffer(device_, shapeVbo_[frame], nullptr);
        shapeVbo_[frame] = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device_, shapeVbo_[frame], shapeVboMemory_[frame], 0);
    vkMapMemory(device_, shapeVboMemory_[frame], 0, bytes, 0, &shapeVboMapped_[frame]);
    shapeVboVerts_[frame] = want;
    return true;
}


// ── SDF shape pipeline ───────────────────────────────────────────────────────

void Renderer::Impl::initShapes() {
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
    // Bake the swapchain's output encoding in (OUTPUT_ENCODE, see
    // shaders_src/output_encode.slang). Must outlive the create call below.
    OutputEncodeSpec encodeSpec(output_.encode);
    stages[1].pSpecializationInfo = encodeSpec.get();

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

    for (uint32_t f = 0; f < kFramesInFlight; f++)
        ensureShapeVboCapacity(f, kInitialShapeVerts);
    LOGI("SDF shape pipeline ready");
}

void Renderer::Impl::recordShapeDraw(VkCommandBuffer cmd, uint32_t frame,
                               uint32_t firstVert, uint32_t vertCount) {
    if (shapePipeline_ == VK_NULL_HANDLE || vertCount == 0) return;

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
    vkCmdDraw(cmd, vertCount, 1, firstVert, 0);
}

void Renderer::Impl::cleanupShapes() {
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        if (shapeVboMapped_[f]) { vkUnmapMemory(device_, shapeVboMemory_[f]); shapeVboMapped_[f] = nullptr; }
        if (shapeVbo_[f])       { vkDestroyBuffer(device_, shapeVbo_[f], nullptr); shapeVbo_[f] = VK_NULL_HANDLE; }
        if (shapeVboMemory_[f]) { vkFreeMemory(device_, shapeVboMemory_[f], nullptr); shapeVboMemory_[f] = VK_NULL_HANDLE; }
        shapeVboVerts_[f] = 0;
    }
    if (shapePipeline_)       { vkDestroyPipeline(device_, shapePipeline_, nullptr); shapePipeline_ = VK_NULL_HANDLE; }
    if (shapePipelineLayout_) { vkDestroyPipelineLayout(device_, shapePipelineLayout_, nullptr); shapePipelineLayout_ = VK_NULL_HANDLE; }
}
