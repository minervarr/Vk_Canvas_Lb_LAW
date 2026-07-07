#include "image_layer.hh"
#include "log.hh"
#include <cstring>

#define LOG_TAG "ImageLayer"
#define LOGE(...) VCE_LOGE(LOG_TAG, __VA_ARGS__)
#define LOGI(...) VCE_LOGI(LOG_TAG, __VA_ARGS__)

uint32_t ImageLayer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; i++) {
        bool compatible = (typeBits & (1u << i)) != 0;
        bool hasFlags   = (props.memoryTypes[i].propertyFlags & required) == required;
        if (compatible && hasFlags) return i;
    }
    return UINT32_MAX;
}

VkShaderModule ImageLayer::loadShader(const char* path) {
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

void ImageLayer::init(VkDevice device, VkPhysicalDevice physicalDevice,
                       AssetReader& assets, VkRenderPass renderPass,
                       VkCommandPool cmdPool, VkQueue queue) {
    device_         = device;
    physicalDevice_ = physicalDevice;
    assets_         = &assets;
    cmdPool_        = cmdPool;
    queue_          = queue;

    VkDescriptorSetLayoutBinding b{};
    b.binding         = 0;
    b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo sli{};
    sli.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sli.bindingCount = 1;
    sli.pBindings    = &b;
    vkCreateDescriptorSetLayout(device_, &sli, nullptr, &descLayout_);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
    VkDescriptorPoolCreateInfo pci{};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pci.poolSizeCount = 1;
    pci.pPoolSizes    = &ps;
    pci.maxSets       = kMaxTextures;
    vkCreateDescriptorPool(device_, &pci, nullptr, &descPool_);

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(float) * 10;  // dstX,Y,W,H, u0,v0,u1,v1, screenW,H

    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 1;
    pli.pSetLayouts            = &descLayout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    vkCreatePipelineLayout(device_, &pli, nullptr, &pipelineLayout_);

    VkShaderModule vm = loadShader("shaders/image_vert.spv");
    VkShaderModule fm = loadShader("shaders/image_frag.spv");
    if (!vm || !fm) { LOGE("image layer shaders missing"); return; }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vm; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fm; stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo   vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo      vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo   ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Premultiplied-alpha blend, matching the overlay composite pass so
    // artwork/icons layer correctly under the vector/text UI on top.
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
    gpci.pDynamicState       = &dy; gpci.layout = pipelineLayout_;
    gpci.renderPass          = renderPass; gpci.subpass = 0;
    vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, vm, nullptr);
    vkDestroyShaderModule(device_, fm, nullptr);

    LOGI("ImageLayer ready");
}

TextureHandle ImageLayer::create_texture(const uint8_t* rgba, uint32_t w, uint32_t h) {
    if (!rgba || w == 0 || h == 0) return kInvalidTexture;
    VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

    // Staging buffer (host-visible) holding the source pixels.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device_, &bi, nullptr, &staging);
        VkMemoryRequirements reqs{};
        vkGetBufferMemoryRequirements(device_, staging, &reqs);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = reqs.size;
        ai.memoryTypeIndex = findMemoryType(reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device_, &ai, nullptr, &stagingMem);
        vkBindBufferMemory(device_, staging, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(device_, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, rgba, static_cast<size_t>(size));
        vkUnmapMemory(device_, stagingMem);
    }

    TextureRec rec{};

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = {w, h, 1};
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    vkCreateImage(device_, &imgInfo, nullptr, &rec.image);

    VkMemoryRequirements imgReqs{};
    vkGetImageMemoryRequirements(device_, rec.image, &imgReqs);
    VkMemoryAllocateInfo imgAlloc{};
    imgAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAlloc.allocationSize  = imgReqs.size;
    imgAlloc.memoryTypeIndex = findMemoryType(imgReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &imgAlloc, nullptr, &rec.memory);
    vkBindImageMemory(device_, rec.image, rec.memory, 0);

    // One-shot upload: UNDEFINED -> TRANSFER_DST -> copy -> SHADER_READ_ONLY.
    VkCommandBufferAllocateInfo cbAlloc{};
    cbAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAlloc.commandPool        = cmdPool_;
    cbAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAlloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device_, &cbAlloc, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier toDst{};
    toDst.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image            = rec.image;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.srcAccessMask    = 0;
    toDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset       = {0, 0, 0};
    region.imageExtent       = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, rec.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead{};
    toRead.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.image            = rec.image;
    toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toRead.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);

    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, stagingMem, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image            = rec.image;
    viewInfo.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format           = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &viewInfo, nullptr, &rec.view);

    VkSamplerCreateInfo sampInfo{};
    sampInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampInfo.magFilter    = VK_FILTER_LINEAR;
    sampInfo.minFilter    = VK_FILTER_LINEAR;
    sampInfo.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device_, &sampInfo, nullptr, &rec.sampler);

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool     = descPool_;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts        = &descLayout_;
    if (vkAllocateDescriptorSets(device_, &dsAlloc, &rec.descSet) != VK_SUCCESS) {
        LOGE("create_texture: descriptor pool exhausted (max %u textures live)", kMaxTextures);
        vkDestroySampler(device_, rec.sampler, nullptr);
        vkDestroyImageView(device_, rec.view, nullptr);
        vkDestroyImage(device_, rec.image, nullptr);
        vkFreeMemory(device_, rec.memory, nullptr);
        return kInvalidTexture;
    }

    VkDescriptorImageInfo ii{rec.sampler, rec.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr{};
    wr.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr.dstSet          = rec.descSet;
    wr.descriptorCount = 1;
    wr.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo      = &ii;
    vkUpdateDescriptorSets(device_, 1, &wr, 0, nullptr);

    TextureHandle handle = nextHandle_++;
    textures_[handle] = rec;
    return handle;
}

void ImageLayer::destroy_texture(TextureHandle handle) {
    auto it = textures_.find(handle);
    if (it == textures_.end()) return;
    TextureRec& rec = it->second;
    if (rec.descSet) vkFreeDescriptorSets(device_, descPool_, 1, &rec.descSet);
    if (rec.sampler) vkDestroySampler(device_, rec.sampler, nullptr);
    if (rec.view)    vkDestroyImageView(device_, rec.view, nullptr);
    if (rec.image)   vkDestroyImage(device_, rec.image, nullptr);
    if (rec.memory)  vkFreeMemory(device_, rec.memory, nullptr);
    textures_.erase(it);
}

void ImageLayer::recordComposite(VkCommandBuffer cmd, const std::vector<ImageDraw>& draws,
                                  uint32_t screenW, uint32_t screenH) {
    if (!ready() || draws.empty()) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport vp{0.0f, 0.0f, (float)screenW, (float)screenH, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);

    for (const ImageDraw& d : draws) {
        auto it = textures_.find(d.tex);
        if (it == textures_.end()) continue;

        VkRect2D scissor;
        if (d.hasClip) {
            int32_t sx = (int32_t)d.clipX, sy = (int32_t)d.clipY;
            uint32_t sw = (uint32_t)d.clipW, sh = (uint32_t)d.clipH;
            scissor = {{sx, sy}, {sw, sh}};
        } else {
            scissor = {{0, 0}, {screenW, screenH}};
        }
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                                0, 1, &it->second.descSet, 0, nullptr);

        float pc[10] = {d.x, d.y, d.w, d.h, d.u0, d.v0, d.u1, d.v1,
                        (float)screenW, (float)screenH};
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
        vkCmdDraw(cmd, 4, 1, 0, 0);
    }
}

void ImageLayer::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    for (auto& [handle, rec] : textures_) {
        if (rec.descSet) vkFreeDescriptorSets(device_, descPool_, 1, &rec.descSet);
        if (rec.sampler) vkDestroySampler(device_, rec.sampler, nullptr);
        if (rec.view)    vkDestroyImageView(device_, rec.view, nullptr);
        if (rec.image)   vkDestroyImage(device_, rec.image, nullptr);
        if (rec.memory)  vkFreeMemory(device_, rec.memory, nullptr);
    }
    textures_.clear();

    if (pipeline_)       vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    if (descPool_)       vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (descLayout_)     vkDestroyDescriptorSetLayout(device_, descLayout_, nullptr);

    *this = ImageLayer{};
}
