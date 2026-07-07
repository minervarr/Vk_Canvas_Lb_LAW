#include "vulkan_state.hh"
#include "font_weight.hh"
#include <android/log.h>
#include <set>
#include <stdexcept>
#include <cstring>

#define TAG  "VulkanState"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

VkShaderModule VulkanState::loadShader(const char* path) {
    AAsset* a = AAssetManager_open(mgr_, path, AASSET_MODE_BUFFER);
    if (!a) { LOGE("Shader not found: %s", path); return VK_NULL_HANDLE; }
    size_t sz = AAsset_getLength(a);
    std::vector<uint32_t> code(sz / sizeof(uint32_t));
    AAsset_read(a, code.data(), sz);
    AAsset_close(a);
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = sz; ci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device_, &ci, nullptr, &m);
    return m;
}

void VulkanState::init(ANativeWindow* window, AAssetManager* mgr) {
    mgr_ = mgr;

    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.apiVersion = VK_API_VERSION_1_0;
    const char* exts[] = { VK_KHR_SURFACE_EXTENSION_NAME,
                            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME };
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = 2; ici.ppEnabledExtensionNames = exts;
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");

    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    if (vkCreateAndroidSurfaceKHR(instance_, &sci, nullptr, &surface_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateAndroidSurfaceKHR failed");

    uint32_t cnt = 0;
    vkEnumeratePhysicalDevices(instance_, &cnt, nullptr);
    std::vector<VkPhysicalDevice> devs(cnt);
    vkEnumeratePhysicalDevices(instance_, &cnt, devs.data());
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { physDev_ = d; break; }
        if (physDev_ == VK_NULL_HANDLE) physDev_ = d;
    }

    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physDev_, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> fams(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(physDev_, &qc, fams.data());
    for (uint32_t i = 0; i < qc; i++) {
        if (fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfxFamily_ = i;
        VkBool32 ok = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physDev_, i, surface_, &ok);
        if (ok) presFamily_ = i;
    }

    float prio = 1.f;
    std::set<uint32_t> uniq = {gfxFamily_, presFamily_};
    std::vector<VkDeviceQueueCreateInfo> qinfos;
    for (uint32_t f : uniq) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = f; qi.queueCount = 1; qi.pQueuePriorities = &prio;
        qinfos.push_back(qi);
    }
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)qinfos.size(); dci.pQueueCreateInfos = qinfos.data();
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = devExts;
    if (vkCreateDevice(physDev_, &dci, nullptr, &device_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDevice failed");
    vkGetDeviceQueue(device_, gfxFamily_,  0, &gfxQueue_);
    vkGetDeviceQueue(device_, presFamily_, 0, &presQueue_);

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev_, surface_, &caps);
    uint32_t fmtCnt = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev_, surface_, &fmtCnt, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCnt);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev_, surface_, &fmtCnt, fmts.data());
    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    swapFmt_ = chosen.format;
    extent_  = caps.currentExtent;
    uint32_t imgCnt = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCnt > caps.maxImageCount) imgCnt = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swci{};
    swci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swci.surface = surface_; swci.minImageCount = imgCnt;
    swci.imageFormat = chosen.format; swci.imageColorSpace = chosen.colorSpace;
    swci.imageExtent = extent_; swci.imageArrayLayers = 1;
    swci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swci.preTransform = caps.currentTransform;
    swci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swci.presentMode = VK_PRESENT_MODE_FIFO_KHR; swci.clipped = VK_TRUE;
    swci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateSwapchainKHR(device_, &swci, nullptr, &swapchain_) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &ic, nullptr);
    swapImages_.resize(ic);
    vkGetSwapchainImagesKHR(device_, swapchain_, &ic, swapImages_.data());

    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = gfxFamily_;
    vkCreateCommandPool(device_, &cpci, nullptr, &cmdPool_);

    renderer_.init(device_, physDev_, mgr_, extent_.width, extent_.height);

    {
        VkCommandBufferAllocateInfo a{};
        a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        a.commandPool = cmdPool_; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        VkCommandBuffer cb; vkAllocateCommandBuffers(device_, &a, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        renderer_.transitionOutputImageInitial(cb);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(gfxQueue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(gfxQueue_);
        vkFreeCommandBuffers(device_, cmdPool_, 1, &cb);
    }

    swapViews_.resize(swapImages_.size());
    for (uint32_t i = 0; i < swapImages_.size(); i++) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = swapImages_[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFmt_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vi, nullptr, &swapViews_[i]);
    }

    VkAttachmentDescription att{};
    att.format = swapFmt_; att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1; rpci.pAttachments = &att;
    rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    vkCreateRenderPass(device_, &rpci, nullptr, &renderPass_);

    framebuffers_.resize(swapViews_.size());
    for (uint32_t i = 0; i < swapViews_.size(); i++) {
        VkFramebufferCreateInfo fbi{};
        fbi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbi.renderPass = renderPass_; fbi.attachmentCount = 1;
        fbi.pAttachments = &swapViews_[i];
        fbi.width = extent_.width; fbi.height = extent_.height; fbi.layers = 1;
        vkCreateFramebuffer(device_, &fbi, nullptr, &framebuffers_[i]);
    }

    {
        VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                        1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        li.bindingCount = 1; li.pBindings = &b;
        vkCreateDescriptorSetLayout(device_, &li, nullptr, &compSetLayout_);

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.poolSizeCount = 1; pi.pPoolSizes = &ps; pi.maxSets = 1;
        vkCreateDescriptorPool(device_, &pi, nullptr, &compPool_);

        VkDescriptorSetAllocateInfo sa{};
        sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        sa.descriptorPool = compPool_; sa.descriptorSetCount = 1;
        sa.pSetLayouts = &compSetLayout_;
        vkAllocateDescriptorSets(device_, &sa, &compSet_);

        VkDescriptorImageInfo ii{renderer_.outputSampler, renderer_.outputImageView,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{};
        wr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr.dstSet = compSet_; wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &ii;
        vkUpdateDescriptorSets(device_, 1, &wr, 0, nullptr);
    }

    {
        VkPipelineLayoutCreateInfo pli{};
        pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1; pli.pSetLayouts = &compSetLayout_;
        vkCreatePipelineLayout(device_, &pli, nullptr, &compLayout_);

        VkShaderModule vm = loadShader("shaders/composite_vert.spv");
        VkShaderModule fm = loadShader("shaders/composite_frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_VERTEX_BIT,   vm, "main", nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                     VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main", nullptr};

        VkPipelineVertexInputStateCreateInfo   vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo      vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;
        VkPipelineMultisampleStateCreateInfo   ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState    ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                            VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo    cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1; cb.pAttachments = &ba;
        VkDynamicState dynSt[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo       dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dy.dynamicStateCount = 2; dy.pDynamicStates = dynSt;

        VkGraphicsPipelineCreateInfo gpci{};
        gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gpci.stageCount = 2; gpci.pStages = stages;
        gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dy; gpci.layout = compLayout_;
        gpci.renderPass = renderPass_;
        vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gpci, nullptr, &compPipeline_);
        vkDestroyShaderModule(device_, vm, nullptr);
        vkDestroyShaderModule(device_, fm, nullptr);
    }

    // Double-buffered command buffers, semaphores, fences (kFrames in flight)
    VkCommandBufferAllocateInfo cba{};
    cba.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cba.commandPool = cmdPool_; cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cba.commandBufferCount = kFrames;
    vkAllocateCommandBuffers(device_, &cba, cmdBuf_);

    VkSemaphoreCreateInfo semi{}; semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFrames; i++) {
        vkCreateSemaphore(device_, &semi, nullptr, &imgReady_[i]);
        vkCreateSemaphore(device_, &semi, nullptr, &rendDone_[i]);
        vkCreateFence    (device_, &fi,   nullptr, &fence_[i]);
    }
    LOGI("VulkanState ready %ux%u (%u frames in flight)", extent_.width, extent_.height, kFrames);
}

void VulkanState::initMsdf(const MsdfFont& msdf, int weightIdx) {
    renderer_.createMsdfResources(renderPass_, msdf, weightIdx);
    // Upload atlas to GPU in a one-shot command buffer
    VkCommandBufferAllocateInfo a{};
    a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    a.commandPool = cmdPool_; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    a.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(device_, &a, &cb);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    renderer_.recordAtlasUpload(cb, weightIdx);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(gfxQueue_, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(gfxQueue_);
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cb);
    LOGI("MSDF atlas (weight %d) uploaded to GPU (%ux%u)", weightIdx, msdf.atlasW(), msdf.atlasH());
}

void VulkanState::draw(const std::vector<float>* quads, const uint32_t* quadVerts,
                       bool content_changed, float scroll_x, float scroll_y) {
    uint32_t fi = frameIdx_;

    vkWaitForFences(device_, 1, &fence_[fi], VK_TRUE, UINT64_MAX);
    vkResetFences  (device_, 1, &fence_[fi]);

    // Upload new quads for each weight when content changed.
    if (content_changed) {
        for (int w = 0; w < kFontWeightCount; w++) {
            if (quadVerts[w] > 0)
                renderer_.uploadGlyphQuads(quads[w].data(), quadVerts[w], w);
        }
    }

    uint32_t imgIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                          imgReady_[fi], VK_NULL_HANDLE, &imgIdx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR || acq == VK_SUBOPTIMAL_KHR) return;

    vkResetCommandBuffer(cmdBuf_[fi], 0);
    VkCommandBufferBeginInfo bi{}; bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmdBuf_[fi], &bi);

    VkClearValue clr = {{{0.12f, 0.12f, 0.14f, 1.f}}};
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = renderPass_; rpbi.framebuffer = framebuffers_[imgIdx];
    rpbi.renderArea.extent = extent_;
    rpbi.clearValueCount = 1; rpbi.pClearValues = &clr;
    vkCmdBeginRenderPass(cmdBuf_[fi], &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    // Draw each weight that has a ready atlas and non-zero verts.
    for (int w = 0; w < kFontWeightCount; w++) {
        if (renderer_.msdfReady(w) && renderer_.msdfVerts(w) > 0) {
            renderer_.drawMsdfRange(cmdBuf_[fi],
                                    renderer_.msdfVertOffset(w), renderer_.msdfVerts(w),
                                    scroll_x, scroll_y,
                                    0, 0, extent_.width, extent_.height, w);
        }
    }

    vkCmdEndRenderPass(cmdBuf_[fi]);
    vkEndCommandBuffer(cmdBuf_[fi]);

    VkPipelineStageFlags wst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1; si.pWaitSemaphores   = &imgReady_[fi];
    si.pWaitDstStageMask    = &wst;
    si.commandBufferCount   = 1; si.pCommandBuffers   = &cmdBuf_[fi];
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &rendDone_[fi];
    vkQueueSubmit(gfxQueue_, 1, &si, fence_[fi]);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &rendDone_[fi];
    pi.swapchainCount = 1; pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imgIdx;
    vkQueuePresentKHR(presQueue_, &pi);

    frameIdx_ = (frameIdx_ + 1) % kFrames;
}

void VulkanState::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    for (uint32_t i = 0; i < kFrames; i++) {
        vkDestroyFence    (device_, fence_[i],    nullptr);
        vkDestroySemaphore(device_, rendDone_[i], nullptr);
        vkDestroySemaphore(device_, imgReady_[i], nullptr);
    }
    vkDestroyCommandPool(device_, cmdPool_,    nullptr);
    vkDestroyPipeline      (device_, compPipeline_, nullptr);
    vkDestroyPipelineLayout(device_, compLayout_,   nullptr);
    vkDestroyDescriptorPool(device_, compPool_,     nullptr);
    vkDestroyDescriptorSetLayout(device_, compSetLayout_, nullptr);
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    for (auto iv : swapViews_) vkDestroyImageView(device_, iv, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    renderer_.cleanup();
    vkDestroyDevice  (device_,   nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE;
}
