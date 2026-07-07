#include "overlay.hh"
#include "log.hh"
#include <cstring>
#include <cmath>
#include <vector>

#define LOG_TAG "OverlayRasterizer"
#define LOGE(...) VCE_LOGE(LOG_TAG, __VA_ARGS__)
#define LOGI(...) VCE_LOGI(LOG_TAG, __VA_ARGS__)

uint32_t OverlayRasterizer::findMemoryType(uint32_t typeBits,
                                           VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        bool compatible = (typeBits & (1u << i)) != 0;
        bool hasFlags   = (props.memoryTypes[i].propertyFlags & required) == required;
        if (compatible && hasFlags) return i;
    }
    return UINT32_MAX;
}

VkShaderModule OverlayRasterizer::loadShader(const char* path) {
    std::vector<uint8_t> code;
    if (!assets_->read(path, code)) {
        LOGE("Could not open shader asset: %s", path);
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size();
    info.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &info, nullptr, &mod) != VK_SUCCESS) {
        LOGE("Could not create shader module: %s", path);
        return VK_NULL_HANDLE;
    }
    return mod;
}

void OverlayRasterizer::init(VkDevice device, VkPhysicalDevice physicalDevice,
                             AssetReader& assets, VkRenderPass renderPass,
                             uint32_t width, uint32_t height) {
    device_         = device;
    physicalDevice_ = physicalDevice;
    assets_         = &assets;
    width_          = width;
    height_         = height;

    createDescriptorLayoutAndPool();
    createBuffers();
    createOutputImage();
    createComputePipelines();
    writeDescriptors();
    createCompositePipeline(renderPass);
    LOGI("OverlayRasterizer ready (%ux%u)", width_, height_);
}

void OverlayRasterizer::createDescriptorLayoutAndPool() {
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (int i = 0; i < 4; i++) {
        bindings[i].binding         = i;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType  = (i == 2) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings    = bindings;
    vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descLayout_);

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 3;
    poolSizes[1].type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes    = poolSizes;
    poolInfo.maxSets       = 1;
    vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descPool_);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = descPool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &descLayout_;
    vkAllocateDescriptorSets(device_, &allocInfo, &descSet_);
}

void OverlayRasterizer::createBuffers() {
    const VkDeviceSize curveBufferSize =
        static_cast<VkDeviceSize>(MAX_CURVES) * CURVE_FLOATS * sizeof(float);

    VkBufferCreateInfo cbInfo{};
    cbInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cbInfo.size        = curveBufferSize;
    cbInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    cbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateBuffer(device_, &cbInfo, nullptr, &curveBuffer_);

    VkMemoryRequirements cbReqs{};
    vkGetBufferMemoryRequirements(device_, curveBuffer_, &cbReqs);
    VkMemoryAllocateInfo cbAlloc{};
    cbAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cbAlloc.allocationSize  = cbReqs.size;
    cbAlloc.memoryTypeIndex = findMemoryType(cbReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &cbAlloc, nullptr, &curveBufferMemory_);
    vkBindBufferMemory(device_, curveBuffer_, curveBufferMemory_, 0);
    vkMapMemory(device_, curveBufferMemory_, 0, curveBufferSize, 0, &curveBufferMapped_);
    std::memset(curveBufferMapped_, 0, static_cast<size_t>(curveBufferSize));

    createTileRowBuffers();
}

void OverlayRasterizer::createTileRowBuffers() {
    const uint32_t tileCountX = (width_  + TILE_SIZE - 1) / TILE_SIZE;
    const uint32_t tileCountY = (height_ + TILE_SIZE - 1) / TILE_SIZE;

    auto makeDeviceBuffer = [&](VkDeviceSize size, VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device_, &bi, nullptr, &buf);
        VkMemoryRequirements reqs{};
        vkGetBufferMemoryRequirements(device_, buf, &reqs);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = reqs.size;
        ai.memoryTypeIndex = findMemoryType(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device_, &ai, nullptr, &mem);
        vkBindBufferMemory(device_, buf, mem, 0);
    };

    makeDeviceBuffer(static_cast<VkDeviceSize>(tileCountX) * tileCountY * TILE_STRIDE_U32 * sizeof(uint32_t),
                     tileBuffer_, tileBufferMemory_);
    makeDeviceBuffer(static_cast<VkDeviceSize>(tileCountX) * tileCountY * WIND_STRIDE_U32 * sizeof(uint32_t),
                     rowBuffer_, rowBufferMemory_);
}

void OverlayRasterizer::createOutputImage() {
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = {width_, height_, 1};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device_, &imgInfo, nullptr, &outputImage_);

    VkMemoryRequirements imgReqs{};
    vkGetImageMemoryRequirements(device_, outputImage_, &imgReqs);
    VkMemoryAllocateInfo imgAlloc{};
    imgAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAlloc.allocationSize  = imgReqs.size;
    imgAlloc.memoryTypeIndex = findMemoryType(imgReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &imgAlloc, nullptr, &outputImageMemory_);
    vkBindImageMemory(device_, outputImage_, outputImageMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = outputImage_;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &viewInfo, nullptr, &outputImageView_);

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter    = VK_FILTER_NEAREST;
    sampInfo.minFilter    = VK_FILTER_NEAREST;
    sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &sampInfo, nullptr, &outputSampler_);
}

void OverlayRasterizer::createComputePipelines() {
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(uint32_t) * 6;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &descLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &computePipelineLayout_);

    VkShaderModule tilingShader   = loadShader("shaders/tiling.spv");
    VkShaderModule coverageShader = loadShader("shaders/coverage.spv");
    if (!tilingShader || !coverageShader) { LOGE("overlay compute shaders missing"); return; }

    VkComputePipelineCreateInfo infos[2]{};
    for (int i = 0; i < 2; i++) {
        infos[i].sType       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        infos[i].layout      = computePipelineLayout_;
        infos[i].stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        infos[i].stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        infos[i].stage.pName = "main";
    }
    infos[0].stage.module = tilingShader;
    infos[1].stage.module = coverageShader;
    VkPipeline pipelines[2]{};
    vkCreateComputePipelines(device_, VK_NULL_HANDLE, 2, infos, nullptr, pipelines);
    tilingPipeline_   = pipelines[0];
    coveragePipeline_ = pipelines[1];
    vkDestroyShaderModule(device_, tilingShader,   nullptr);
    vkDestroyShaderModule(device_, coverageShader, nullptr);
}

void OverlayRasterizer::writeDescriptors() {
    const VkDeviceSize curveBufferSize =
        static_cast<VkDeviceSize>(MAX_CURVES) * CURVE_FLOATS * sizeof(float);
    const uint32_t tileCountX = (width_  + TILE_SIZE - 1) / TILE_SIZE;
    const uint32_t tileCountY = (height_ + TILE_SIZE - 1) / TILE_SIZE;
    const VkDeviceSize tileBufferSize =
        static_cast<VkDeviceSize>(tileCountX) * tileCountY * TILE_STRIDE_U32 * sizeof(uint32_t);
    const VkDeviceSize rowBufferSize =
        static_cast<VkDeviceSize>(tileCountX) * tileCountY * WIND_STRIDE_U32 * sizeof(uint32_t);

    VkDescriptorBufferInfo curveDesc{curveBuffer_, 0, curveBufferSize};
    VkDescriptorBufferInfo tileDesc {tileBuffer_,  0, tileBufferSize};
    VkDescriptorBufferInfo rowDesc  {rowBuffer_,   0, rowBufferSize};
    VkDescriptorImageInfo  imageDesc{VK_NULL_HANDLE, outputImageView_, VK_IMAGE_LAYOUT_GENERAL};

    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; i++) {
        writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet          = descSet_;
        writes[i].dstBinding      = i;
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[0].pBufferInfo = &curveDesc;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[1].pBufferInfo = &tileDesc;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;  writes[2].pImageInfo  = &imageDesc;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[3].pBufferInfo = &rowDesc;
    vkUpdateDescriptorSets(device_, 4, writes, 0, nullptr);
}

void OverlayRasterizer::createCompositePipeline(VkRenderPass renderPass) {
    // Descriptor: combined image sampler of the overlay image (fragment stage).
    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sli{};
    sli.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sli.bindingCount = 1;
    sli.pBindings    = &b;
    vkCreateDescriptorSetLayout(device_, &sli, nullptr, &compSetLayout_);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.poolSizeCount = 1;
    pci.pPoolSizes    = &ps;
    pci.maxSets       = 1;
    vkCreateDescriptorPool(device_, &pci, nullptr, &compPool_);

    VkDescriptorSetAllocateInfo sa{};
    sa.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sa.descriptorPool     = compPool_;
    sa.descriptorSetCount = 1;
    sa.pSetLayouts        = &compSetLayout_;
    vkAllocateDescriptorSets(device_, &sa, &compSet_);

    updateCompositeImageDescriptor();

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(float) * 4;  // cosA, sinA, w, h

    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &compSetLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    vkCreatePipelineLayout(device_, &pli, nullptr, &compLayout_);

    VkShaderModule vm = loadShader("shaders/overlay_vert.spv");
    VkShaderModule fm = loadShader("shaders/overlay_frag.spv");
    if (!vm || !fm) { LOGE("overlay composite shaders missing"); return; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vm; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fm; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo   vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo      vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo   ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied-alpha blend: dst = src + dst*(1-src.a)
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable         = VK_TRUE;
    ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.colorBlendOp        = VK_BLEND_OP_ADD;
    ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    ba.alphaBlendOp        = VK_BLEND_OP_ADD;
    ba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &ba;

    VkDynamicState dynSt[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2; dy.pDynamicStates = dynSt;

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount          = 2; gpci.pStages = stages;
    gpci.pVertexInputState   = &vi; gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp; gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms; gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &dy; gpci.layout = compLayout_;
    gpci.renderPass          = renderPass; gpci.subpass = 0;
    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &compPipeline_);
    vkDestroyShaderModule(device_, vm, nullptr);
    vkDestroyShaderModule(device_, fm, nullptr);
}

void OverlayRasterizer::updateCompositeImageDescriptor() {
    VkDescriptorImageInfo ii{outputSampler_, outputImageView_, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr{};
    wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet          = compSet_;
    wr.descriptorCount = 1;
    wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo      = &ii;
    vkUpdateDescriptorSets(device_, 1, &wr, 0, nullptr);
}

void OverlayRasterizer::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;              // minimized; keep old resources
    if (width == width_ && height == height_) return;

    if (outputSampler_)     vkDestroySampler(device_, outputSampler_, nullptr);
    if (outputImageView_)   vkDestroyImageView(device_, outputImageView_, nullptr);
    if (outputImage_)       vkDestroyImage(device_, outputImage_, nullptr);
    if (outputImageMemory_) vkFreeMemory(device_, outputImageMemory_, nullptr);
    outputSampler_ = VK_NULL_HANDLE; outputImageView_ = VK_NULL_HANDLE;
    outputImage_   = VK_NULL_HANDLE; outputImageMemory_ = VK_NULL_HANDLE;

    if (tileBuffer_)       vkDestroyBuffer(device_, tileBuffer_, nullptr);
    if (tileBufferMemory_) vkFreeMemory(device_, tileBufferMemory_, nullptr);
    if (rowBuffer_)        vkDestroyBuffer(device_, rowBuffer_, nullptr);
    if (rowBufferMemory_)  vkFreeMemory(device_, rowBufferMemory_, nullptr);
    tileBuffer_ = VK_NULL_HANDLE; tileBufferMemory_ = VK_NULL_HANDLE;
    rowBuffer_  = VK_NULL_HANDLE; rowBufferMemory_  = VK_NULL_HANDLE;

    width_ = width;
    height_ = height;

    createTileRowBuffers();
    createOutputImage();
    writeDescriptors();               // rewires descSet_ to the new image/buffers
    updateCompositeImageDescriptor(); // rewires compSet_ to the new image

    // Buffers were recreated (fresh, unbound descriptors) and the image's
    // contents are undefined again — force a full re-dispatch next frame.
    firstRun_    = true;
    curvesDirty_ = true;
}

void OverlayRasterizer::uploadCurves(const float* curveData, uint32_t count) {
    if (count > MAX_CURVES) count = MAX_CURVES;
    
    size_t newSize = static_cast<size_t>(count) * CURVE_FLOATS;
    bool changed = (newSize != lastCurves_.size());
    if (!changed && newSize > 0) {
        changed = (std::memcmp(lastCurves_.data(), curveData, newSize * sizeof(float)) != 0);
    }
    
    if (changed) {
        lastCurves_.assign(curveData, curveData + newSize);
        curvesDirty_ = true;
        // Detect winding curves (types 4/5/6) so the coverage pass can skip its
        // per-pixel tile-row scan entirely when the scene has none.
        hasWinding_ = 0;
        for (uint32_t i = 0; i < count; i++) {
            if (static_cast<uint32_t>(curveData[i * CURVE_FLOATS + 0]) >= 4u) {
                hasWinding_ = 1;
                break;
            }
        }
        if (count > 0 && curveBufferMapped_) {
            std::memcpy(curveBufferMapped_, curveData, newSize * sizeof(float));
        }
    }
    curveCount_ = count;
}

void OverlayRasterizer::recordDispatch(VkCommandBuffer cmd) {
    if (!ready()) return;
    if (!curvesDirty_) return;
    curvesDirty_ = false;

    // outputImage: UNDEFINED -> GENERAL for compute writes (contents discarded;
    // the coverage pass rewrites every pixel each frame).
    VkImageMemoryBarrier toGeneral{};
    toGeneral.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout        = firstRun_ ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.image            = outputImage_;
    toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toGeneral.srcAccessMask    = firstRun_ ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toGeneral.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, firstRun_ ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);
    firstRun_ = false;

    const uint32_t tileCountX = (width_  + TILE_SIZE - 1) / TILE_SIZE;
    const uint32_t tileCountY = (height_ + TILE_SIZE - 1) / TILE_SIZE;
    uint32_t pushData[6] = {width_, height_, curveCount_, tileCountX, tileCountY, hasWinding_};

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            computePipelineLayout_, 0, 1, &descSet_, 0, nullptr);

    const VkDeviceSize tileBufferSize =
        static_cast<VkDeviceSize>(tileCountX) * tileCountY * TILE_STRIDE_U32 * sizeof(uint32_t);
    const VkDeviceSize rowBufferSize =
        static_cast<VkDeviceSize>(tileCountX) * tileCountY * WIND_STRIDE_U32 * sizeof(uint32_t);
    vkCmdFillBuffer(cmd, tileBuffer_, 0, tileBufferSize, 0);
    // The row (winding) buffer is only read by Pass 2; skip clearing it entirely
    // when the scene has no winding curves.
    if (hasWinding_) vkCmdFillBuffer(cmd, rowBuffer_, 0, rowBufferSize, 0);

    {
        VkBufferMemoryBarrier bb[2]{};
        bb[0].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bb[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bb[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        bb[0].buffer        = tileBuffer_; bb[0].size = VK_WHOLE_SIZE;
        bb[1] = bb[0]; bb[1].buffer = rowBuffer_;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, bb, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tilingPipeline_);
    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushData), pushData);
    if (curveCount_ > 0) {
        vkCmdDispatch(cmd, (curveCount_ + 63) / 64, 1, 1);
        VkBufferMemoryBarrier bb[2]{};
        bb[0].sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        bb[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bb[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        bb[0].buffer        = tileBuffer_; bb[0].size = VK_WHOLE_SIZE;
        bb[1] = bb[0]; bb[1].buffer = rowBuffer_;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, bb, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, coveragePipeline_);
    vkCmdPushConstants(cmd, computePipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushData), pushData);
    if (tileCountX > 0 && tileCountY > 0)
        vkCmdDispatch(cmd, tileCountX, tileCountY, 1);

    // We omit the compute->fragment barrier! The GPU will execute the camera's fragment shader 
    // concurrently with the UI's compute shader. This eliminates the "micro stop" lag spike 
    // on the camera feed when the UI changes (e.g. on rotation or timer tick), at the cost 
    // of a potential one-frame visual tear on the UI itself (which is acceptable).
}

void OverlayRasterizer::recordComposite(VkCommandBuffer cmd, int rotation_deg) {
    if (!ready()) return;
    VkViewport vp{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 1.0f};
    VkRect2D   sc{{0, 0}, {width_, height_}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compPipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compLayout_, 0, 1, &compSet_, 0, nullptr);

    // Sampling rotation is the inverse of the desired display rotation.
    float rad = float(rotation_deg) * 0.01745329252f;
    float pc[4] = {std::cos(rad), -std::sin(rad), (float)width_, (float)height_};
    vkCmdPushConstants(cmd, compLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void OverlayRasterizer::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    if (compPipeline_)   vkDestroyPipeline(device_, compPipeline_, nullptr);
    if (compLayout_)     vkDestroyPipelineLayout(device_, compLayout_, nullptr);
    if (compPool_)       vkDestroyDescriptorPool(device_, compPool_, nullptr);
    if (compSetLayout_)  vkDestroyDescriptorSetLayout(device_, compSetLayout_, nullptr);

    if (tilingPipeline_)        vkDestroyPipeline(device_, tilingPipeline_, nullptr);
    if (coveragePipeline_)      vkDestroyPipeline(device_, coveragePipeline_, nullptr);
    if (computePipelineLayout_) vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);

    if (outputSampler_)   vkDestroySampler(device_, outputSampler_, nullptr);
    if (outputImageView_) vkDestroyImageView(device_, outputImageView_, nullptr);
    if (outputImage_)     vkDestroyImage(device_, outputImage_, nullptr);
    if (outputImageMemory_) vkFreeMemory(device_, outputImageMemory_, nullptr);

    if (curveBufferMapped_) { vkUnmapMemory(device_, curveBufferMemory_); curveBufferMapped_ = nullptr; }
    if (curveBuffer_)       vkDestroyBuffer(device_, curveBuffer_, nullptr);
    if (curveBufferMemory_) vkFreeMemory(device_, curveBufferMemory_, nullptr);
    if (tileBuffer_)        vkDestroyBuffer(device_, tileBuffer_, nullptr);
    if (tileBufferMemory_)  vkFreeMemory(device_, tileBufferMemory_, nullptr);
    if (rowBuffer_)         vkDestroyBuffer(device_, rowBuffer_, nullptr);
    if (rowBufferMemory_)   vkFreeMemory(device_, rowBufferMemory_, nullptr);

    if (descPool_)   vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (descLayout_) vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);

    *this = OverlayRasterizer{};
}
