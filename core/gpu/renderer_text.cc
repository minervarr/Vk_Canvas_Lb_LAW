// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"
#include "msdf.hh"

// ── MSDF text pipeline ─────────────────────────────────────────────────────

void Renderer::Impl::initMsdf(const TextFont& font) {
    if (!device_ || !render_pass_) return;
    // Callers may release the font's CPU atlas pixels after upload
    // (MsdfFont::releaseAtlasPixels()); an upload needs them resident.
    if (!font.atlasOnGpu() && font.atlas().empty()) {
        LOGE("initMsdf: atlas pixels not resident (call ensureAtlasLoaded first)");
        return;
    }
    const uint32_t pages = font.atlasPages() ? font.atlasPages() : 1;
    const VkFormat wantFormat = font.atlasChannels() == 1 ? VK_FORMAT_R8_UNORM
                                                          : VK_FORMAT_R8G8B8A8_UNORM;

    // ── The common case: same image, a few pages rewritten ──────────────────
    //
    // A lazily-baked glyph adds cells to ONE page. Rebuilding the image, view,
    // sampler, descriptor pool and pipeline for that — and re-uploading every
    // byte of a 124 MB atlas — is what made a bake visible as a stall. When
    // nothing about the image's shape has changed, patch the pages that
    // actually changed and leave the rest of the pipeline alone.
    if (msdfPipeline_ != VK_NULL_HANDLE && msdfAtlasImage_ != VK_NULL_HANDLE &&
        msdfAtlasW_ == font.atlasW() && msdfAtlasH_ == font.atlasH() &&
        msdfAtlasLayers_ == pages && msdfAtlasFormat_ == wantFormat) {
        // A GPU-resident atlas has nothing to upload — whatever wrote it wrote
        // it in place. The image is already correct and already in
        // SHADER_READ_ONLY_OPTIMAL.
        if (!font.atlasOnGpu()) updateMsdfAtlasPages(font);
        return;
    }

    if (msdfPipeline_ != VK_NULL_HANDLE) {
        // The atlas changed SHAPE — it grew a page, or a different font was
        // bound. Wait for any in-flight frame still sampling the old
        // atlas/descriptor before tearing it down, then rebuild fresh below.
        vkDeviceWaitIdle(device_);
        cleanupMsdf();
    }

    // A full upload covers every page, so whatever the font had recorded as
    // dirty is now clean. Taking the record here is what stops the next
    // incremental pass from redundantly re-uploading pages just written.
    std::vector<uint32_t> ignored;
    font.takeDirtyPages(ignored);

    if (caps_.max_image_array_layers && pages > caps_.max_image_array_layers) {
        LOGE("MSDF atlas: %u pages exceeds this device's maxImageArrayLayers "
             "(%u); text will be incomplete", pages, caps_.max_image_array_layers);
    }

    uploadMsdfAtlas(font.atlas().data(), font.atlasW(), font.atlasH(),
                    pages,
                    font.distanceRange(), font.atlasChannels());
    // The fragment shader's mode word: 0 = MSDF, 1 = MTSDF (gates the
    // true-SDF blend), 2 = raster coverage. See msdf_frag.slang.
    msdfIsMtsdf_ = font.textMode() == TextMode::Raster ? 2.0f
                                                       : (font.isMtsdf() ? 1.0f : 0.0f);
    createMsdfPipeline();

    // Vertex buffers (host-visible, persistently mapped) — one per frame in
    // flight so the CPU's upload for frame N+1 can't race frame N's read.
    for (uint32_t f = 0; f < kFramesInFlight; f++)
        ensureMsdfVboCapacity(f, kInitialMsdfVerts);

    LOGI("MSDF pipeline ready (atlas %ux%u, pxRange %.1f)", msdfAtlasW_, msdfAtlasH_, msdfPxRange_);
}

void Renderer::Impl::updateMsdfAtlasPages(const TextFont& font) {
    std::vector<uint32_t> dirty;
    font.takeDirtyPages(dirty);
    if (dirty.empty()) return;

    const uint32_t channels = font.atlasChannels();
    const VkDeviceSize pageBytes =
        static_cast<VkDeviceSize>(msdfAtlasW_) * msdfAtlasH_ * channels;
    const VkDeviceSize total = pageBytes * dirty.size();
    if (font.atlas().size() < pageBytes * msdfAtlasLayers_) {
        LOGE("MSDF atlas: source holds %zu bytes, expected %llu for %u pages",
             font.atlas().size(), (unsigned long long)(pageBytes * msdfAtlasLayers_),
             msdfAtlasLayers_);
        return;
    }

    // Pack only the dirty pages into the staging buffer, back to back; the
    // copy regions below map slot i to its real layer.
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo sb{};
    sb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sb.size  = total;
    sb.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(device_, &sb, nullptr, &stagingBuf) != VK_SUCCESS) return;
    VkMemoryRequirements smr{};
    vkGetBufferMemoryRequirements(device_, stagingBuf, &smr);
    VkMemoryAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    sa.allocationSize = smr.size;
    sa.memoryTypeIndex = find_memory_type(smr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &sa, nullptr, &stagingMem) != VK_SUCCESS) {
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        return;
    }
    vkBindBufferMemory(device_, stagingBuf, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(device_, stagingMem, 0, total, 0, &mapped);
    for (size_t i = 0; i < dirty.size(); i++)
        std::memcpy(static_cast<uint8_t*>(mapped) + pageBytes * i,
                    font.atlas().data() + pageBytes * dirty[i],
                    static_cast<size_t>(pageBytes));
    vkUnmapMemory(device_, stagingMem);

    // Nothing in flight may still be sampling the image while it is written.
    vkDeviceWaitIdle(device_);

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

    // SHADER_READ_ONLY_OPTIMAL, *not* UNDEFINED. The full-upload path uses
    // UNDEFINED because its image is brand new; using it here would permit the
    // implementation to discard the image's contents — every page that is not
    // being re-copied, which is most of them.
    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image = msdfAtlasImage_;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, msdfAtlasLayers_};
    toDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    std::vector<VkBufferImageCopy> regions(dirty.size());
    for (size_t i = 0; i < dirty.size(); i++) {
        regions[i] = {};
        regions[i].bufferOffset = pageBytes * i;
        regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dirty[i], 1};
        regions[i].imageExtent = {msdfAtlasW_, msdfAtlasH_, 1};
    }
    vkCmdCopyBufferToImage(cmd, stagingBuf, msdfAtlasImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

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
}

bool Renderer::Impl::ensureMsdfVboCapacity(uint32_t frame, uint32_t verts) {
    if (verts <= msdfVboVerts_[frame] && msdfVboMapped_[frame]) return true;

    // Grow by doubling, never by exactly what was asked for: a scene that
    // creeps past the capacity one glyph at a time would otherwise reallocate
    // every frame.
    uint32_t want = msdfVboVerts_[frame] ? msdfVboVerts_[frame] : kInitialMsdfVerts;
    while (want < verts) want *= 2;

    // Whole quads only, so a capacity can never cut a glyph in half.
    want -= want % static_cast<uint32_t>(TextFont::VERTS_PER_GLYPH);

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(want) *
                               TextFont::FLOATS_PER_VERT * sizeof(float);

    // The caller has already waited on this slot's fence, so nothing in flight
    // can still be reading the old buffer.
    if (msdfVboMapped_[frame]) { vkUnmapMemory(device_, msdfVboMemory_[frame]); msdfVboMapped_[frame] = nullptr; }
    if (msdfVbo_[frame])       { vkDestroyBuffer(device_, msdfVbo_[frame], nullptr); msdfVbo_[frame] = VK_NULL_HANDLE; }
    if (msdfVboMemory_[frame]) { vkFreeMemory(device_, msdfVboMemory_[frame], nullptr); msdfVboMemory_[frame] = VK_NULL_HANDLE; }
    msdfVboVerts_[frame] = 0;

    VkBufferCreateInfo vb{};
    vb.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vb.size  = bytes;
    vb.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(device_, &vb, nullptr, &msdfVbo_[frame]) != VK_SUCCESS) {
        LOGE("text VBO: cannot create a buffer for %u vertices", want);
        return false;
    }
    VkMemoryRequirements vr{};
    vkGetBufferMemoryRequirements(device_, msdfVbo_[frame], &vr);
    VkMemoryAllocateInfo va{};
    va.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    va.allocationSize = vr.size;
    va.memoryTypeIndex = find_memory_type(vr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device_, &va, nullptr, &msdfVboMemory_[frame]) != VK_SUCCESS) {
        LOGE("text VBO: cannot allocate %llu bytes for %u vertices",
             (unsigned long long)bytes, want);
        vkDestroyBuffer(device_, msdfVbo_[frame], nullptr);
        msdfVbo_[frame] = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(device_, msdfVbo_[frame], msdfVboMemory_[frame], 0);
    vkMapMemory(device_, msdfVboMemory_[frame], 0, bytes, 0, &msdfVboMapped_[frame]);
    msdfVboVerts_[frame] = want;
    return true;
}


void Renderer::Impl::uploadMsdfAtlas(const uint8_t* rgba, uint32_t w, uint32_t h,
                               uint32_t layers,
                              float pxRange, uint32_t channels) {
    msdfAtlasW_ = w;
    msdfAtlasH_ = h;
    msdfPxRange_ = pxRange;

    // 4 for an MSDF/MTSDF sheet (RGB field + alpha), 1 for raster coverage.
    // Getting this wrong reads past the end of the source buffer, which is
    // exactly what a hardcoded *4 did the first time a 1-channel atlas
    // arrived here.
    msdfAtlasFormat_ = channels == 1 ? VK_FORMAT_R8_UNORM
                                     : VK_FORMAT_R8G8B8A8_UNORM;
    if (layers == 0) layers = 1;
    msdfAtlasLayers_ = layers;
    msdfAtlasGen_++;   // a new image is an EMPTY image; see msdfAtlasGeneration()

    // One staging buffer for every page; the pages are contiguous and
    // page-major in the source, so layer N starts at N * pageBytes.
    const VkDeviceSize pageBytes = static_cast<VkDeviceSize>(w) * h * channels;
    VkDeviceSize imageSize = pageBytes * layers;

    // bufferOffset must be 4-byte aligned for a colour copy, and every page
    // offset is a multiple of pageBytes.
    if (pageBytes % 4 != 0)
        LOGE("MSDF atlas: page of %llu bytes is not 4-byte aligned; "
             "per-layer copies will be rejected",
             (unsigned long long)pageBytes);

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
    // A GPU-resident atlas has no CPU pixels: the image is created empty and
    // whatever owns it writes it in place. Zeroing rather than skipping the
    // copy keeps the layout transitions below on one path.
    if (rgba) std::memcpy(mapped, rgba, static_cast<size_t>(imageSize));
    else      std::memset(mapped, 0, static_cast<size_t>(imageSize));
    vkUnmapMemory(device_, stagingMem);

    // Image
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = msdfAtlasFormat_;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = layers;
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
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);
    std::vector<VkBufferImageCopy> regions(layers);
    for (uint32_t l = 0; l < layers; l++) {
        regions[l] = {};
        regions[l].bufferOffset = pageBytes * l;
        regions[l].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
        regions[l].imageExtent = {w, h, 1};
    }
    vkCmdCopyBufferToImage(cmd, stagingBuf, msdfAtlasImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());
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
    // ALWAYS an array view, even for a single-page atlas. The fragment shader
    // declares Sampler2DArray, and a plain 2D view bound to it is a descriptor
    // dimensionality mismatch — undefined sampling, and a validation error
    // rather than a compile error. A one-layer array sampled at layer 0 is
    // spec-identical to a 2D sample, so there is no second pipeline for it.
    vc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vc.format = msdfAtlasFormat_;
    vc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, msdfAtlasLayers_};
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

