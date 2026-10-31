// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"
#include "msdf.hh"

void Renderer::Impl::createMsdfPipeline() {
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.size = sizeof(float) * 8;  // screen.xy pxRange pad atlas.xy scroll.xy
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &msdfSetLayout_;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pcr;
    VCE_VK_CHECK_THROW(vkCreatePipelineLayout(device_, &pl, nullptr, &msdfPipelineLayout_));

    auto loadShader = [this](const char* path) -> VkShaderModule {
        std::vector<uint8_t> code;
        if (!assets_.read(path, code)) return VK_NULL_HANDLE;
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &ci, nullptr, &mod) != VK_SUCCESS) {
            LOGE("MSDF shader module creation failed: %s", path);
            return VK_NULL_HANDLE;
        }
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
    // LINEAR_SURFACE, constant_id 1 in msdf_frag.slang. Not OUTPUT_ENCODE
    // (that is constant_id 0 on the other shaders, and this shader does not
    // include it). 1 only when the attachment this pipeline is bound in
    // actually blends in linear light. An sRGB pass that failed to build
    // must not leave the constant set: linear values written into the UNORM
    // pass come out too dark.
    bool linearOut = text_linear_surface_;
    VkRenderPass pass = render_pass_;
    if (text_srgb_pass_) {
        if (text_render_pass_ == VK_NULL_HANDLE) {
            LOGE("Text composite: sRGB pass missing, using legacy encode");
            linearOut = false;
        } else {
            pass = text_render_pass_;
        }
    }
    VkSpecializationMapEntry specEntry{};
    specEntry.constantID = 1;
    specEntry.offset = 0;
    specEntry.size = sizeof(int32_t);
    const int32_t linearSurface = linearOut ? 1 : 0;
    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 1;
    specInfo.pMapEntries = &specEntry;
    specInfo.dataSize = sizeof(linearSurface);
    specInfo.pData = &linearSurface;
    stages[1].pSpecializationInfo = &specInfo;

    // The stride comes from TextFont, never from a literal, and the assert
    // below fails the build if the layout changes without this table changing
    // with it. It was `8 * sizeof(float)` with an `attrs[3]` beside it, in two
    // renderers and a shader, and nothing tied the four together.
    static_assert(TextFont::FLOATS_PER_VERT == 9,
                  "text vertex layout changed: update attrs[] and msdf_vert.slang");
    VkVertexInputBindingDescription bind{
        0, TextFont::FLOATS_PER_VERT * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4]{
        {0, 0, VK_FORMAT_R32G32_SFLOAT,       0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT,       2 * sizeof(float)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 4 * sizeof(float)},
        {3, 0, VK_FORMAT_R32_SFLOAT,          8 * sizeof(float)},   // atlas page
    };
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    // Derived from the array, not restated: the two disagreeing is a
    // validation error at pipeline creation and nothing at compile time.
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(attrs));
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
    // PREMULTIPLIED source. msdf_frag.slang emits linear(colour) * coverage.
    // On an sRGB view or a linear float surface it leaves that linear, and
    // the hardware blends the page in linear light too. On the legacy UNORM
    // path it encodes first. ONE is the premultiply either way. Do not switch
    // this to SRC_ALPHA: that mixes coverage in gamma and light type on a
    // dark page goes thin. text_cover.hh is the tested pair of formulas.
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
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
    gp.renderPass = pass;
    gp.subpass = 0;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &msdfPipeline_) != VK_SUCCESS) {
        LOGE("Failed to create MSDF pipeline");
        msdfPipeline_ = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(device_, vs, nullptr);
    vkDestroyShaderModule(device_, fs, nullptr);
}

void Renderer::Impl::recordMsdfDraw(VkCommandBuffer cmd, uint32_t frame) {
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

void Renderer::Impl::cleanupMsdf() {
    // Every handle is nulled out after destruction (not just guarded on
    // entry) so this is safe to call twice in a row and so initMsdf() can
    // reliably tell "torn down" from "still live" to rebuild the atlas at a
    // larger size (see initMsdf()'s reinit branch) instead of silently
    // no-op'ing on a second call.
    for (uint32_t f = 0; f < kFramesInFlight; f++) {
        if (msdfVboMapped_[f]) { vkUnmapMemory(device_, msdfVboMemory_[f]); msdfVboMapped_[f] = nullptr; }
        if (msdfVbo_[f])       { vkDestroyBuffer(device_, msdfVbo_[f], nullptr); msdfVbo_[f] = VK_NULL_HANDLE; }
        if (msdfVboMemory_[f]) { vkFreeMemory(device_, msdfVboMemory_[f], nullptr); msdfVboMemory_[f] = VK_NULL_HANDLE; }
        msdfVboVerts_[f] = 0;
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

