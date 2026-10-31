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
        if (!vce_vk_ok(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle(MSDF rebuild)",
                       __FILE__, __LINE__))
            return;
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

    if (!uploadMsdfAtlas(font.atlas().data(), font.atlasW(), font.atlasH(),
                         pages,
                         font.distanceRange(), font.atlasChannels())) {
        LOGE("initMsdf: atlas upload failed; text stays disabled");
        return;
    }
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
    if (sa.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device_, &sa, nullptr, &stagingMem) != VK_SUCCESS) {
        LOGE("MSDF atlas: cannot allocate staging memory");
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        return;
    }
    VCE_VK_CHECK_OR(vkBindBufferMemory(device_, stagingBuf, stagingMem, 0),
                    { vkDestroyBuffer(device_, stagingBuf, nullptr);
                      vkFreeMemory(device_, stagingMem, nullptr); return; });
    void* mapped = nullptr;
    if (vkMapMemory(device_, stagingMem, 0, total, 0, &mapped) != VK_SUCCESS) {
        LOGE("MSDF atlas: cannot map staging memory");
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return;
    }
    for (size_t i = 0; i < dirty.size(); i++)
        std::memcpy(static_cast<uint8_t*>(mapped) + pageBytes * i,
                    font.atlas().data() + pageBytes * dirty[i],
                    static_cast<size_t>(pageBytes));
    vkUnmapMemory(device_, stagingMem);

    // Nothing in flight may still be sampling the image while it is written.
    if (!vce_vk_ok(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle", __FILE__, __LINE__)) {
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return;
    }

    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmd_pool_;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &cba, &cmd) != VK_SUCCESS) {
        LOGE("MSDF atlas: cannot allocate the update command buffer");
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return;
    }
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return;
    }

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

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (!vce_vk_ok(vkEndCommandBuffer(cmd), "vkEndCommandBuffer(MSDF update)", __FILE__, __LINE__)
        || !vce_vk_ok(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE),
                      "vkQueueSubmit(MSDF update)", __FILE__, __LINE__)
        || !vce_vk_ok(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(MSDF update)", __FILE__, __LINE__)) {
        // The buffer/memory are freed below regardless; the atlas keeps its
        // previous contents (or a partial upload, which the next full rebuild
        // overwrites).
    }
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
    if (va.memoryTypeIndex == UINT32_MAX) {
        LOGE("text VBO: no host-visible memory type for %u vertices", want);
        vkDestroyBuffer(device_, msdfVbo_[frame], nullptr);
        msdfVbo_[frame] = VK_NULL_HANDLE;
        return false;
    }
    if (vkAllocateMemory(device_, &va, nullptr, &msdfVboMemory_[frame]) != VK_SUCCESS) {
        LOGE("text VBO: cannot allocate %llu bytes for %u vertices",
             (unsigned long long)bytes, want);
        vkDestroyBuffer(device_, msdfVbo_[frame], nullptr);
        msdfVbo_[frame] = VK_NULL_HANDLE;
        return false;
    }
    if (!vce_vk_ok(vkBindBufferMemory(device_, msdfVbo_[frame], msdfVboMemory_[frame], 0),
                   "vkBindBufferMemory(text VBO)", __FILE__, __LINE__)
        || !vce_vk_ok(vkMapMemory(device_, msdfVboMemory_[frame], 0, bytes, 0, &msdfVboMapped_[frame]),
                      "vkMapMemory(text VBO)", __FILE__, __LINE__)) {
        vkDestroyBuffer(device_, msdfVbo_[frame], nullptr);
        msdfVbo_[frame] = VK_NULL_HANDLE;
        vkFreeMemory(device_, msdfVboMemory_[frame], nullptr);
        msdfVboMemory_[frame] = VK_NULL_HANDLE;
        return false;
    }
    msdfVboVerts_[frame] = want;
    return true;
}


bool Renderer::Impl::uploadMsdfAtlas(const uint8_t* rgba, uint32_t w, uint32_t h,
                               uint32_t layers,
                              float pxRange, uint32_t channels) {
    msdfAtlasW_ = w;
    msdfAtlasH_ = h;
    msdfPxRange_ = pxRange;
    auto abandon = [this]() {
        cleanupMsdf();
        return false;
    };

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
    VCE_VK_CHECK_OR(vkCreateBuffer(device_, &sb, nullptr, &stagingBuf), return abandon());
    VkMemoryRequirements smr{};
    vkGetBufferMemoryRequirements(device_, stagingBuf, &smr);
    VkMemoryAllocateInfo sa{};
    sa.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    sa.allocationSize = smr.size;
    sa.memoryTypeIndex = find_memory_type(smr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (sa.memoryTypeIndex == UINT32_MAX) {
        LOGE("MSDF atlas: no host-visible memory type for the staging buffer");
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        return abandon();
    }
    VCE_VK_CHECK_OR(vkAllocateMemory(device_, &sa, nullptr, &stagingMem),
                    { vkDestroyBuffer(device_, stagingBuf, nullptr); return abandon(); });
    VCE_VK_CHECK_OR(vkBindBufferMemory(device_, stagingBuf, stagingMem, 0),
                    { vkDestroyBuffer(device_, stagingBuf, nullptr);
                      vkFreeMemory(device_, stagingMem, nullptr); return abandon(); });
    void* mapped = nullptr;
    VCE_VK_CHECK_OR(vkMapMemory(device_, stagingMem, 0, imageSize, 0, &mapped),
                    { vkDestroyBuffer(device_, stagingBuf, nullptr);
                      vkFreeMemory(device_, stagingMem, nullptr); return abandon(); });
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
    VCE_VK_CHECK_OR(vkCreateImage(device_, &ci, nullptr, &msdfAtlasImage_), return abandon());
    VkMemoryRequirements ir{};
    vkGetImageMemoryRequirements(device_, msdfAtlasImage_, &ir);
    VkMemoryAllocateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ia.allocationSize = ir.size;
    ia.memoryTypeIndex = find_memory_type(ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ia.memoryTypeIndex == UINT32_MAX) {
        LOGE("MSDF atlas: no device-local memory type");
        return abandon();
    }
    VCE_VK_CHECK_OR(vkAllocateMemory(device_, &ia, nullptr, &msdfAtlasMemory_), return abandon());
    VCE_VK_CHECK_OR(vkBindImageMemory(device_, msdfAtlasImage_, msdfAtlasMemory_, 0), return abandon());

    // Transition + copy via one-shot command buffer
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmd_pool_;
    cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VCE_VK_CHECK_OR(vkAllocateCommandBuffers(device_, &cba, &cmd), return abandon());
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VCE_VK_CHECK_OR(vkBeginCommandBuffer(cmd, &begin), return abandon());

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
    VCE_VK_CHECK_OR(vkEndCommandBuffer(cmd), {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return abandon();
    });
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (!vce_vk_ok(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE),
                   "vkQueueSubmit(MSDF atlas)", __FILE__, __LINE__)
        || !vce_vk_ok(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(MSDF atlas)", __FILE__, __LINE__)) {
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
        vkDestroyBuffer(device_, stagingBuf, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
        return abandon();
    }
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
    VCE_VK_CHECK_OR(vkCreateImageView(device_, &vc, nullptr, &msdfAtlasView_), return abandon());

    // Sampler
    VkSamplerCreateInfo sc{};
    sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sc.magFilter = VK_FILTER_LINEAR;
    sc.minFilter = VK_FILTER_LINEAR;
    sc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.maxAnisotropy = 1.0f;
    VCE_VK_CHECK_OR(vkCreateSampler(device_, &sc, nullptr, &msdfAtlasSampler_), return abandon());

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
    VCE_VK_CHECK_OR(vkCreateDescriptorSetLayout(device_, &lci, nullptr, &msdfSetLayout_),
                    return abandon());

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpc{};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    dpc.maxSets = 1;
    VCE_VK_CHECK_OR(vkCreateDescriptorPool(device_, &dpc, nullptr, &msdfDescPool_),
                    return abandon());

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = msdfDescPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &msdfSetLayout_;
    VCE_VK_CHECK_OR(vkAllocateDescriptorSets(device_, &dsai, &msdfDescSet_), return abandon());

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
    return true;
}

