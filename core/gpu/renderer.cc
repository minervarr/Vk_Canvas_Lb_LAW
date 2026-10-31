// Split from the original renderer.cc (2026-10-31). The public forwarders
// live in renderer_api.cc; this file owns one subsystem of the frame path.
#include "renderer_impl.hh"

Renderer::Impl::Impl(SurfaceProvider& surface, AssetReader& assets,
                   uint32_t desiredSwapchainImages,
                   OutputTarget requestedOutput,
                   PresentPolicy presentPolicy)
    : surface_provider_(surface), assets_(assets),
      desired_swapchain_images_(desiredSwapchainImages),
      requested_output_(requestedOutput),
      present_policy_(presentPolicy) {
    VkExtent2D ext = surface_provider_.extent();
    width_  = ext.width;
    height_ = ext.height;
    create_instance();
    create_surface();
    pick_physical_device();
    create_logical_device();
    resolve_output_target();
    resolve_text_composite();
    create_swapchain();
    create_render_pass();
    create_framebuffers();
    create_command_buffers();
    create_sync_objects();
    overlay_.init(device_, physical_dev_, assets_, render_pass_, width_, height_,
                  output_.encode);
    image_layer_.init(device_, physical_dev_, assets_, render_pass_, cmd_pool_, queue_,
                      output_.encode);
    initShapes();

    LOGI("Renderer ready (%ux%u)", width_, height_);
}

Renderer::Impl::~Impl() {
    cleanup();
}

void Renderer::Impl::draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg,
                    const std::vector<ImageDraw>& images,
                    const std::vector<ImageDraw>& foregroundImages,
                    const std::vector<float>& msdfQuads,
                    const std::vector<float>& shapeVerts,
                    const std::vector<float>& frontShapeVerts) {
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
        constexpr uint32_t kFloatsPerVert = TextFont::FLOATS_PER_VERT;
        uint32_t verts = static_cast<uint32_t>(msdfQuads.size() / kFloatsPerVert);

        // The buffer grows to fit rather than the text being cut off. If the
        // allocation genuinely fails, fall back to whatever capacity survives —
        // rounded down to a whole number of quads, so the tail is a missing
        // glyph and never half of one.
        if (msdfReady() && verts > 0) {
            if (!ensureMsdfVboCapacity(frame, verts))
                verts = msdfVboVerts_[frame] -
                        msdfVboVerts_[frame] % static_cast<uint32_t>(TextFont::VERTS_PER_GLYPH);
            if (msdfVboMapped_[frame] && verts > 0)
                std::memcpy(msdfVboMapped_[frame], msdfQuads.data(),
                            static_cast<size_t>(verts) * kFloatsPerVert * sizeof(float));
            else
                verts = 0;
        } else {
            verts = 0;
        }
        msdfVertCount_ = verts;
    }

    // Upload SDF shape quads into this frame slot's VBO. Foreground shapes
    // share the buffer and are drawn after images, so a photo cannot cover them.
    {
        uint32_t verts = static_cast<uint32_t>(shapeVerts.size() / kShapeFloatsPerVert);
        uint32_t front = static_cast<uint32_t>(frontShapeVerts.size() / kShapeFloatsPerVert);
        uint32_t total = verts + front;
        // Same contract as the text VBO: grow to fit, and if the allocation
        // genuinely fails, fall back to whatever capacity survives — rounded
        // down to a whole quad, so the tail is a missing shape and never
        // half of one (a Terminus glyph cut mid-run looks like a sliced
        // letter, which is how Settings lists used to die).
        if (shapePipeline_ != VK_NULL_HANDLE && total > 0) {
            if (!ensureShapeVboCapacity(frame, total)) {
                uint32_t cap = shapeVboVerts_[frame] -
                               shapeVboVerts_[frame] % kShapeVertsPerQuad;
                if (verts > cap) {
                    verts = cap;
                    front = 0;
                } else if (verts + front > cap) {
                    front = cap - verts;
                }
            }
            if (shapeVboMapped_[frame]) {
                auto* dst = static_cast<float*>(shapeVboMapped_[frame]);
                if (verts > 0)
                    std::memcpy(dst, shapeVerts.data(),
                                static_cast<size_t>(verts) * kShapeFloatsPerVert * sizeof(float));
                if (front > 0)
                    std::memcpy(dst + static_cast<size_t>(verts) * kShapeFloatsPerVert,
                                frontShapeVerts.data(),
                                static_cast<size_t>(front) * kShapeFloatsPerVert * sizeof(float));
            } else {
                verts = 0;
                front = 0;
            }
        } else {
            verts = 0;
            front = 0;
        }
        shapeVertCount_ = verts;
        shapeFrontFirst_ = verts;
        shapeFrontCount_ = front;
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
        if (hwb_cache_.find(hwb_to_bind) != hwb_cache_.end()) {
            if (current_hwb_) AHardwareBuffer_release(current_hwb_);
            current_hwb_ = hwb_to_bind;
        } else {
            // Binding failed and this buffer has no image to sample. Keeping
            // the previous frame on screen is strictly better than pointing
            // current_hwb_ at something draw() cannot find, which composites
            // nothing at all and reads as a dead player.
            AHardwareBuffer_release(hwb_to_bind);
        }
    }
#endif  // __ANDROID__
    int64_t t_afterbind = now_ns();

    // Capture seam: with present disabled the acquired image is never handed
    // back to the compositor, so acquiring again would exhaust the swapchain
    // after imageCount frames. Re-render into the image already acquired —
    // readbackLastFrame() wants exactly that image, and its contents are
    // overwritten every frame.
    const bool reuse_acquired_image =
        !present_enabled_ && last_drawn_image_index_ != UINT32_MAX &&
        last_drawn_image_index_ < swapchain_images_.size();
    uint32_t image_index;
    VkResult result = VK_SUCCESS;
    if (reuse_acquired_image) {
        image_index = last_drawn_image_index_;
    } else {
        result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
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
        }
        // SURFACE_LOST and DEVICE_LOST leave image_index undefined. Submitting
        // that index paints a black frame and does not come back. Skip the
        // frame; a new window rebuilds the swapchain.
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            LOGI("acquire skipped (VkResult %d)", (int)result);
            return;
        }
        // VK_SUBOPTIMAL_KHR from acquire: the image is still perfectly usable —
        // render and present it, and let the present side's SUBOPTIMAL trigger
        // the recreate afterward. The old code bailed here, which both dropped
        // the frame AND left the acquire semaphore signaled (invalid to reuse
        // on the next acquire).
    }
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
        
        // The VISIBLE size, not the allocated one: letterboxing a 2048x1536
        // buffer that holds a 2040x1530 picture gets the aspect ratio slightly
        // wrong on top of showing the padding.
        const uint32_t vis_w = external_visible_w_ > 0 ? external_visible_w_ : desc.width;
        const uint32_t vis_h = external_visible_h_ > 0 ? external_visible_h_ : desc.height;

        // A quarter turn exchanges the picture's width and height, so the
        // letterbox must be computed on the ROTATED extent. These were
        // unconditionally swapped, which is right for the camera preview this
        // path was written for and wrong for a video that needs no rotation at
        // all: it letterboxed a landscape file into a portrait box.
        //
        // The pixel aspect is applied to the picture's own width FIRST, and the
        // rotation swaps the axes afterwards. The other order stretches a
        // portrait video along the wrong axis, which is the same bug the
        // unconditional swap above was.
        const bool quarter_turn = (external_rotation_quadrant_ & 1) != 0;
        const float pic_w = static_cast<float>(vis_w) * external_pixel_aspect_;
        const float pic_h = static_cast<float>(vis_h);
        float raw_w = quarter_turn ? pic_h : pic_w;
        float raw_h = quarter_turn ? pic_w : pic_h;
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
        // Texel steps of the bound camera image, so peaking measures real
        // image detail rather than screen pixels.
        const float tx = desc.width  > 0 ? 1.0f / (float)desc.width  : 0.0f;
        const float ty = desc.height > 0 ? 1.0f / (float)desc.height : 0.0f;

        // The visible fraction of the decoder's buffer. desc is the ALLOCATED
        // size, aligned up by the hardware; external_visible_* is what the
        // producer said is actually picture. Equal, or unset, means 1.0.
        float uScale = 1.0f, vScale = 1.0f;
        if (external_visible_w_ > 0 && external_visible_w_ < desc.width)
            uScale = static_cast<float>(external_visible_w_) / static_cast<float>(desc.width);
        if (external_visible_h_ > 0 && external_visible_h_ < desc.height)
            vScale = static_cast<float>(external_visible_h_) / static_cast<float>(desc.height);

        // Zero-initialized first, so every field this renderer does not drive
        // is definitively OFF rather than whatever was on the stack.
        // zoom <= 1 is the wide view; peaking still runs on it.
        CompositePush pc{};
        pc.hlg         = camera_hlg_;
        pc.transfer    = static_cast<int>(external_transfer_);
        pc.peakNits    = external_peak_nits_;
        pc.rotQuadrant = external_rotation_quadrant_;
        pc.uvScaleX    = uScale;
        pc.uvScaleY    = vScale;
        pc.zoom        = 1.0f;
        pc.cx          = 0.5f;
        pc.cy          = 0.5f;
        pc.peak        = camera_peak_;
        pc.texelX      = tx;
        pc.texelY      = ty;
        pc.peakColor   = camera_peak_color_;
        vkCmdPushConstants(cmd_buffers_[image_index], pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cmd_buffers_[image_index], 3, 1, 0, 0);

        // The loupe: the same frame again, magnified about its centre, into a
        // square inset. Drawn with the viewport confined to the inset, so the
        // fullscreen triangle lands inside it and the wide view underneath
        // survives everywhere else.
        if (camera_loupe_zoom_ > 1.0f) {
            // The inset MUST keep the frame's aspect ratio. The draw is the
            // same fullscreen triangle, so whatever rectangle the viewport
            // describes is what the whole frame gets mapped onto — a square
            // inset would show a squashed image, which is useless for judging
            // focus. Height is the free dimension; width follows the frame.
            const float ih = std::min((float)width_, (float)height_) * 0.52f;
            const float iw = ih * (draw_w / draw_h);
            const float ix = (width_ - iw) * 0.5f;
            const float iy = y_offset + draw_h * 0.08f;

            VkViewport lv{};
            lv.x = ix; lv.y = iy;
            lv.width = iw; lv.height = ih;
            lv.minDepth = 0.0f; lv.maxDepth = 1.0f;
            vkCmdSetViewport(cmd_buffers_[image_index], 0, 1, &lv);

            VkRect2D ls{};
            ls.offset = { (int32_t)ix, (int32_t)iy };
            ls.extent = { (uint32_t)iw, (uint32_t)ih };
            vkCmdSetScissor(cmd_buffers_[image_index], 0, 1, &ls);

            // The shader zooms in UV space — a fraction of the FRAME — but the
            // inset is drawn much smaller than the main preview, so a UV zoom
            // of N lands on screen as N * (iw / draw_w), which is well under
            // 1.0 for a small inset: the "magnifier" showed the scene SMALLER
            // than the preview it sits on. The zoom the caller asks for is
            // relative to what the preview already shows, so convert it.
            // Published so the overlay can outline the window: an inset with no
            // border is hard to even find in a busy frame, and impossible to
            // read as "this is a magnified view of the centre".
            loupe_rect_[0] = ix; loupe_rect_[1] = iy;
            loupe_rect_[2] = iw; loupe_rect_[3] = ih;

            const float uv_zoom = camera_loupe_zoom_ * (draw_w / iw);
            CompositePush lpc = pc;
            lpc.zoom = uv_zoom;
            lpc.cx = 0.5f;
            lpc.cy = 0.5f;
            vkCmdPushConstants(cmd_buffers_[image_index], pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(lpc), &lpc);
            vkCmdDraw(cmd_buffers_[image_index], 3, 1, 0, 0);

            // Hand the scissor back to the full surface: every later layer
            // (images, shapes, the UI overlay) inherits this command buffer's
            // state, and a stale inset scissor would clip the whole UI away.
            VkRect2D full{};
            full.offset = {0, 0};
            full.extent = {width_, height_};
            vkCmdSetScissor(cmd_buffers_[image_index], 0, 1, &full);
        }
    }
#endif  // __ANDROID__

    if (!images.empty()) {
        image_layer_.recordComposite(cmd_buffers_[image_index], images, width_, height_);
    }

    // SDF shape quads — the vector UI layer's fast path; same layer slot as
    // the overlay composite below (a host uses one or the other per frame).
    if (shapeVertCount_ > 0) {
        recordShapeDraw(cmd_buffers_[image_index], frame, 0, shapeVertCount_);
    }

    if (!overlay_curves.empty()) {
        overlay_.recordComposite(cmd_buffers_[image_index], overlay_rotation_deg);
    }

    if (!foregroundImages.empty()) {
        image_layer_.recordComposite(cmd_buffers_[image_index], foregroundImages, width_, height_);
    }

    if (shapeFrontCount_ > 0) {
        recordShapeDraw(cmd_buffers_[image_index], frame, shapeFrontFirst_, shapeFrontCount_);
    }

    // Text is either the last draw of this pass (legacy UNORM encode, or a
    // float linear swapchain) or its own pass through an sRGB view. The view
    // is what makes the destination's share of a partial-coverage pixel
    // happen in linear light. Ending the UI pass first is required: its
    // finalLayout is COLOR_ATTACHMENT_OPTIMAL for the whole life of the pass,
    // and present needs PRESENT_SRC, which the text pass or the barrier below
    // is what produces.
    if (text_srgb_pass_) {
        vkCmdEndRenderPass(cmd_buffers_[image_index]);
        const bool haveText = msdfVertCount_ > 0
            && image_index < text_framebuffers_.size()
            && text_framebuffers_[image_index] != VK_NULL_HANDLE;
        if (haveText) {
            VkRenderPassBeginInfo trp{};
            trp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            trp.renderPass = text_render_pass_;
            trp.framebuffer = text_framebuffers_[image_index];
            trp.renderArea.offset = {0, 0};
            trp.renderArea.extent = {width_, height_};
            vkCmdBeginRenderPass(cmd_buffers_[image_index], &trp, VK_SUBPASS_CONTENTS_INLINE);
            recordMsdfDraw(cmd_buffers_[image_index], frame);
            vkCmdEndRenderPass(cmd_buffers_[image_index]);
        } else {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = swapchain_images_[image_index];
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = 0;
            vkCmdPipelineBarrier(cmd_buffers_[image_index],
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
    } else {
        if (msdfVertCount_ > 0)
            recordMsdfDraw(cmd_buffers_[image_index], frame);
        vkCmdEndRenderPass(cmd_buffers_[image_index]);
    }
    vkEndCommandBuffer(cmd_buffers_[image_index]);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore wait_sems[] = {image_available_sems_[frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = reuse_acquired_image ? 0 : 1;
    submit_info.pWaitSemaphores = wait_sems;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffers_[image_index];

    // The render-finished semaphore exists only for vkQueuePresentKHR. With
    // present suppressed nobody waits on it, and re-signaling a binary
    // semaphore that is still signaled is invalid (and stalls the next
    // submit). Leave it alone on the capture path.
    VkSemaphore signal_sems[] = {render_finished_sems_[frame]};
    submit_info.signalSemaphoreCount = present_enabled_ ? 1 : 0;
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

    VkResult present_result = VK_SUCCESS;
    if (present_enabled_) {
        present_result = vkQueuePresentKHR(queue_, &present_info);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
            present_result == VK_SUBOPTIMAL_KHR) {
            recreate_swapchain();
        }
    }

    // ── Instrumentation: per-frame phase breakdown on slow frames ───────────
    int64_t t_end = now_ns();
    int64_t total = t_end - draw_t0;
    last_stats_.draw_count = ++draw_count_;
    last_stats_.cpu_total_ms = (double)total / 1'000'000.0;
    last_stats_.cpu_wait_ms = (double)(t_afterwait - draw_t0) / 1'000'000.0;
    last_stats_.cpu_record_ms = (double)(t_afteracquire - t_afterwait) / 1'000'000.0;
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

void Renderer::Impl::cleanup() {
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
    if (text_render_pass_) vkDestroyRenderPass(device_, text_render_pass_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (device_)    vkDestroyDevice(device_, nullptr);
    if (surface_)   vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)  vkDestroyInstance(instance_, nullptr);
}
