// Public Renderer forwarders. The state and the implementation live in
// Renderer::Impl (renderer_impl.hh); this file is the only place the public
// class touches it, which keeps renderer.hh free of overlay/image-layer/atlas
// types.

#include "renderer_impl.hh"

Renderer::Renderer(SurfaceProvider& surface, AssetReader& assets,
                   uint32_t desiredSwapchainImages,
                   OutputTarget requestedOutput,
                   PresentPolicy presentPolicy)
    : impl_(std::make_unique<Impl>(surface, assets, desiredSwapchainImages,
                                   requestedOutput, presentPolicy)) {}

Renderer::~Renderer() = default;

bool Renderer::hdrActive() const { return impl_->output_.hdr; }
OutputTarget Renderer::activeTarget() const { return impl_->output_.target; }
OutputEncode Renderer::activeEncode() const { return impl_->output_.encode; }

void Renderer::draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg,
                    const std::vector<ImageDraw>& images,
                    const std::vector<ImageDraw>& foregroundImages,
                    const std::vector<float>& msdfQuads,
                    const std::vector<float>& shapeVerts,
                    const std::vector<float>& frontShapeVerts) {
    impl_->draw(overlay_curves, overlay_rotation_deg, images, foregroundImages,
                msdfQuads, shapeVerts, frontShapeVerts);
}

bool Renderer::recreate_surface() { return impl_->recreate_surface(); }

bool Renderer::deviceLost() const { return impl_->device_lost_; }
bool Renderer::surfaceLost() const { return impl_->surface_lost_; }

void Renderer::initMsdf(const TextFont& font) { impl_->initMsdf(font); }

VkDevice         Renderer::vkDevice() const { return impl_->device_; }
VkPhysicalDevice Renderer::vkPhysicalDevice() const { return impl_->physical_dev_; }
VkCommandPool    Renderer::vkCommandPool() const { return impl_->cmd_pool_; }
VkQueue          Renderer::vkQueue() const { return impl_->queue_; }
VkImage          Renderer::msdfAtlasImage() const { return impl_->msdfAtlasImage_; }
uint32_t         Renderer::msdfAtlasLayers() const { return impl_->msdfAtlasLayers_; }
uint32_t         Renderer::msdfAtlasGeneration() const { return impl_->msdfAtlasGen_; }
bool             Renderer::msdfReady() const { return impl_->msdfReady(); }

TextureHandle Renderer::create_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                       bool mips, TextureFormat fmt) {
    return impl_->image_layer_.create_texture(rgba, w, h, mips, fmt);
}

void Renderer::destroy_texture(TextureHandle handle) {
    impl_->image_layer_.destroy_texture(handle);
}

void Renderer::setPresentEnabled(bool on) { impl_->present_enabled_ = on; }

uint32_t Renderer::width() const { return impl_->width_; }
uint32_t Renderer::height() const { return impl_->height_; }

const std::string& Renderer::deviceName() const { return impl_->device_name_; }
VkPhysicalDeviceType Renderer::deviceType() const { return impl_->device_type_; }
bool Renderer::isSoftwareDevice() const {
    return impl_->device_type_ == VK_PHYSICAL_DEVICE_TYPE_CPU;
}
uint32_t Renderer::queueFamily() const { return impl_->queue_family_; }

bool Renderer::readbackLastFrame(std::vector<uint8_t>& rgba_out,
                                 uint32_t& out_w, uint32_t& out_h) {
    return impl_->readbackLastFrame(rgba_out, out_w, out_h);
}

uint32_t Renderer::swapchainImageCount() const {
    return static_cast<uint32_t>(impl_->swapchain_images_.size());
}

const FrameStats& Renderer::lastFrameStats() const { return impl_->last_stats_; }

void Renderer::notifyResized() { impl_->recreate_swapchain(); }

void Renderer::set_frame_waker(FrameWaker* waker) { impl_->waker_ = waker; }
bool Renderer::consume_frame_ready() { return impl_->frame_ready_.exchange(false); }

const DeviceCaps& Renderer::caps() const { return impl_->caps_; }

void Renderer::update_external_frame(void* handle, std::function<void()> release_cb) {
    impl_->update_external_frame(handle, std::move(release_cb));
}

#if defined(__ANDROID__)
void Renderer::update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb) {
    impl_->update_camera_frame(hwb, std::move(release_cb));
}

void Renderer::clear_camera_frames() { impl_->clear_camera_frames(); }

void Renderer::set_camera_hlg(bool hlg) { impl_->camera_hlg_ = hlg ? 1.0f : 0.0f; }

void Renderer::set_camera_loupe(float zoom) { impl_->camera_loupe_zoom_ = zoom; }
float Renderer::camera_loupe_zoom() const { return impl_->camera_loupe_zoom_; }

void Renderer::set_camera_peaking(float threshold, bool green) {
    impl_->camera_peak_ = threshold;
    impl_->camera_peak_color_ = green ? 1.0f : 0.0f;
}

bool Renderer::camera_loupe_rect(float out[4]) const {
    if (impl_->camera_loupe_zoom_ <= 1.0f || impl_->loupe_rect_[2] <= 0.0f) return false;
    for (int i = 0; i < 4; ++i) out[i] = impl_->loupe_rect_[i];
    return true;
}
#endif

void Renderer::set_external_colour(VkSamplerYcbcrModelConversion model,
                                   VkSamplerYcbcrRange range,
                                   const VkChromaLocation* xChromaOffset,
                                   const VkChromaLocation* yChromaOffset) {
    impl_->set_external_colour(model, range, xChromaOffset, yChromaOffset);
}

void Renderer::set_external_rotation(int degrees) {
    int q = (degrees / 90) % 4;
    if (q < 0) q += 4;
    impl_->external_rotation_quadrant_ = q;
}

void Renderer::set_external_visible_size(uint32_t w, uint32_t h) {
    impl_->external_visible_w_ = w;
    impl_->external_visible_h_ = h;
}

void Renderer::set_external_transfer(ExternalTransfer t, float displayPeakNits) {
    impl_->external_transfer_ = t;
    impl_->external_peak_nits_ = displayPeakNits;
}

void Renderer::set_external_pixel_aspect(float par) {
    if (par > 0.0f) impl_->external_pixel_aspect_ = par;
}
