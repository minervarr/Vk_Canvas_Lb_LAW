#pragma once
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#endif
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include "platform.hh"
#include "canvas.hh"
#include "overlay.hh"
#include "image_layer.hh"
#include "texture.hh"

class MsdfFont;

class Renderer {
public:
    // desiredSwapchainImages: how many swapchain images to request (clamped
    // to the surface's min/max). The default 4 exists for Android, where the
    // compositor can hold an image ~60ms mid-hitch (see create_swapchain());
    // desktop callers on MAILBOX can pass 3 and save one full-screen image.
    Renderer(SurfaceProvider& surface, AssetReader& assets,
             uint32_t desiredSwapchainImages = 4);
    ~Renderer();

    // Composite, in order: the camera frame (if any); background textured
    // quads (album art — behind UI chrome); SDF shape quads (shapeVerts, the
    // fast primitive path — see Canvas::useShapes()); the UI overlay
    // described by overlay_curves (Canvas curve records), rotated by
    // overlay_rotation_deg (0/90/180/270) to follow device orientation;
    // foreground textured quads (icons/buttons — on top of UI chrome); then
    // MSDF text quads (drawn last so text is always on top).
    //
    // Frames in flight: two, EXCEPT when overlay_curves is non-empty — the
    // compute rasterizer's curve/tile buffers and output image are single-
    // buffered, so a frame that uses them serializes against the previous
    // one (exactly the old behavior). Hosts on the shape path (empty curves)
    // get full CPU/GPU overlap.
    void draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg,
              const std::vector<ImageDraw>& images = {},
              const std::vector<ImageDraw>& foregroundImages = {},
              const std::vector<float>& msdfQuads = {},
              const std::vector<float>& shapeVerts = {});

    // Create MSDF pipeline, atlas texture and vertex buffer from an MsdfFont.
    // Must be called once after the Renderer is constructed, before any draw()
    // that passes non-empty msdfQuads.
    void initMsdf(const MsdfFont& font);
    bool msdfReady() const { return msdfPipeline_ != VK_NULL_HANDLE; }

    // Uploads `rgba` (w*h*4 bytes, straight alpha) as a sampled texture for
    // Canvas::image() draws. See ImageLayer::create_texture for details
    // (incl. the mips flag — pass false for textures never minified).
    TextureHandle create_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                 bool mips = true) {
        return image_layer_.create_texture(rgba, w, h, mips);
    }
    void destroy_texture(TextureHandle handle) { image_layer_.destroy_texture(handle); }

    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }

    // Copy the most recently draw()n swapchain image back to host memory as
    // tightly-packed RGBA8 (width*height*4 bytes, straight from the
    // VK_FORMAT_R8G8B8A8_UNORM target — no swizzle, PNG-ready). Intended for
    // headless capture (a HeadlessSurfaceProvider); works on any swapchain
    // since images now carry TRANSFER_SRC usage. Waits for the device to be
    // idle. Returns false if nothing has been drawn yet. NOT on a hot path.
    bool readbackLastFrame(std::vector<uint8_t>& rgba_out,
                           uint32_t& out_w, uint32_t& out_h);

    // Number of images in the current swapchain. Callers that gate rendering
    // on a dirty flag should keep rendering for this many frames (+1) after a
    // change, so every image in the swapchain's rotation receives it — else
    // the images skipped in between would present stale content.
    uint32_t swapchainImageCount() const { return (uint32_t)swapchain_images_.size(); }

    // Force an immediate swapchain rebuild at the surface's current extent.
    // draw() otherwise only discovers a resize lazily, via VK_SUBOPTIMAL_KHR
    // from vkAcquireNextImageKHR — fine for a window resized gradually via
    // live WM_SIZE drags, but a caller that jumps a hidden window straight to
    // a large size in one step (e.g. showing a fullscreen window) needs the
    // swapchain to already match before the next draw(), or that first frame
    // renders at the stale (smaller) extent and appears corrupted/skewed.
    void notifyResized() { recreate_swapchain(); }

    // Frame pacing: update_camera_frame() (camera callback thread) sets a flag and
    // wakes the render loop, so the loop can present exactly once per camera frame
    // instead of busy-presenting every iteration. Matching the preview's present
    // rate to the 30fps content (vs ~60-120 half-duplicate presents/sec) stops
    // SurfaceFlinger hunting the LTPO refresh rate — the ~once-per-second ~90ms
    // vkWaitForFences stall seen only while recording.
    void set_frame_waker(FrameWaker* waker) { waker_ = waker; }
    bool consume_frame_ready() { return frame_ready_.exchange(false); }

    const DeviceCaps& caps() const { return caps_; }

#if defined(__ANDROID__)
    void update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb = nullptr);
    void clear_camera_frames();

    // While recording the camera delivers HLG frames; this enables the shader's
    // HLG->SDR tone-map so the preview isn't washed out.
    void set_camera_hlg(bool hlg) { camera_hlg_ = hlg ? 1.0f : 0.0f; }
#endif

private:
    SurfaceProvider&  surface_provider_;
    AssetReader&      assets_;
    FrameWaker*       waker_ = nullptr;
    std::atomic<bool> frame_ready_{false};

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    uint32_t desired_swapchain_images_ = 4;  // constructor arg; see create_swapchain()
    float          camera_hlg_ = 0.0f;

    DeviceCaps caps_{};

    // Perf instrumentation (logged once per second).
    int64_t pv_prev_ns_ = 0, pv_window_ns_ = 0, pv_max_gap_ns_ = 0; int pv_frames_ = 0;
    int64_t dr_window_ns_ = 0, dr_max_ns_ = 0; int dr_frames_ = 0;

    VkInstance       instance_       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice physical_dev_   = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain_      = VK_NULL_HANDLE;

    // Swapchain pixel format/colorspace — resolved at create_swapchain(): 10-bit
    // HDR (A2B10G10R10 + an HDR colorspace) when the surface supports it, else
    // the 8-bit SDR default. Shared by the render pass and framebuffer views.
    VkFormat         swapchain_format_     = VK_FORMAT_R8G8B8A8_UNORM;
    VkColorSpaceKHR  swapchain_colorspace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool             swapchain_hdr_        = false;
    bool             ext_swapchain_colorspace_ = false;  // instance ext enabled

    VkRenderPass                 render_pass_ = VK_NULL_HANDLE;
    std::vector<VkImage>         swapchain_images_;
    std::vector<VkImageView>     swapchain_image_views_;
    std::vector<VkFramebuffer>   framebuffers_;

    VkCommandPool                cmd_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers_;

    // Two frames in flight: per-frame sync objects + per-frame copies of
    // every persistently-mapped dynamic buffer the CPU writes each frame
    // (MSDF text VBO, shape VBO). The overlay compute path stays single-
    // buffered — draw() serializes when it's in use (see draw()'s comment).
    static constexpr uint32_t kFramesInFlight = 2;
    VkSemaphore image_available_sems_[kFramesInFlight] = {};
    VkSemaphore render_finished_sems_[kFramesInFlight] = {};
    VkFence     in_flight_fences_[kFramesInFlight]     = {};
    uint32_t    frame_index_ = 0;
    // Which frame-fence last submitted to each swapchain image — guards
    // re-recording cmd_buffers_[image] while that image's prior frame runs.
    std::vector<VkFence> image_fences_;
    // The swapchain image index that draw() last rendered into, for
    // readbackLastFrame(). UINT32_MAX until the first draw().
    uint32_t    last_drawn_image_index_ = UINT32_MAX;

#if defined(__ANDROID__)
    // AHardwareBuffer camera import (Android-only external images).
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID_ = nullptr;
    PFN_vkCreateSamplerYcbcrConversion vkCreateSamplerYcbcrConversion_ = nullptr;
    PFN_vkDestroySamplerYcbcrConversion vkDestroySamplerYcbcrConversion_ = nullptr;

    std::mutex hwb_mutex_;
    AHardwareBuffer* pending_hwb_ = nullptr;
    std::function<void()> current_release_cb_;
    std::function<void()> pending_release_cb_;

    struct HwbCache {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet desc_set = VK_NULL_HANDLE;
    };
    std::unordered_map<AHardwareBuffer*, HwbCache> hwb_cache_;

    AHardwareBuffer* current_hwb_ = nullptr;
    VkSamplerYcbcrConversion ycbcr_conversion_ = VK_NULL_HANDLE;
    VkSampler hwb_sampler_ = VK_NULL_HANDLE;
    VkImage hwb_image_ = VK_NULL_HANDLE;
    VkDeviceMemory hwb_memory_ = VK_NULL_HANDLE;
    VkImageView hwb_view_ = VK_NULL_HANDLE;

    uint64_t last_external_format_ = 0;

    // Camera composite pipeline
    VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    void setup_hwb_resources(AHardwareBuffer* hwb);
    void cleanup_hwb_resources();
    void bind_hwb(AHardwareBuffer* hwb);
#endif

    // UI overlay (canvas curve records rasterised + composited over the camera).
    OverlayRasterizer overlay_;
    // Textured quads (album art, icons) composited as a background layer.
    ImageLayer image_layer_;

    // ── MSDF text pipeline ──────────────────────────────────────────────────
    static constexpr uint32_t kMaxMsdfVerts = 16384;

    VkImage             msdfAtlasImage_    = VK_NULL_HANDLE;
    VkDeviceMemory      msdfAtlasMemory_   = VK_NULL_HANDLE;
    VkImageView         msdfAtlasView_     = VK_NULL_HANDLE;
    VkSampler           msdfAtlasSampler_  = VK_NULL_HANDLE;
    uint32_t            msdfAtlasW_        = 0;
    uint32_t            msdfAtlasH_        = 0;
    float               msdfPxRange_      = 4.0f;
    float               msdfIsMtsdf_      = 0.0f;  // 1 when atlas alpha = true SDF

    VkBuffer            msdfVbo_[kFramesInFlight]       = {};
    VkDeviceMemory      msdfVboMemory_[kFramesInFlight] = {};
    void*               msdfVboMapped_[kFramesInFlight] = {};
    uint32_t            msdfVertCount_     = 0;  // for the frame being recorded

    VkDescriptorSetLayout msdfSetLayout_   = VK_NULL_HANDLE;
    VkDescriptorPool    msdfDescPool_      = VK_NULL_HANDLE;
    VkDescriptorSet     msdfDescSet_       = VK_NULL_HANDLE;
    VkPipelineLayout    msdfPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline          msdfPipeline_      = VK_NULL_HANDLE;

    void createMsdfPipeline();
    void uploadMsdfAtlas(const uint8_t* rgba, uint32_t w, uint32_t h, float pxRange);
    void recordMsdfDraw(VkCommandBuffer cmd, uint32_t frame);
    void cleanupMsdf();

    // ── SDF shape pipeline ─────────────────────────────────────────────────
    // Primitives (rounded rects / capsules / triangles) drawn as per-shape
    // quads whose fragment shader evaluates the analytic SDF — the "MSDF for
    // shapes" fast path. Skips the compute rasterizer's tiling/coverage
    // passes and their screen-size buffers entirely; each shape touches only
    // its own pixels. Vertex layout (14 floats, see Canvas::useShapes()):
    //   pos.xy  rgba  data0.xyzw  data1.xyzw
    static constexpr uint32_t kShapeFloatsPerVert = 14;
    static constexpr uint32_t kMaxShapeVerts = 6 * 2048;  // 2048 shapes

    VkBuffer         shapeVbo_[kFramesInFlight]       = {};
    VkDeviceMemory   shapeVboMemory_[kFramesInFlight] = {};
    void*            shapeVboMapped_[kFramesInFlight] = {};
    uint32_t         shapeVertCount_ = 0;  // for the frame being recorded
    VkPipelineLayout shapePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       shapePipeline_       = VK_NULL_HANDLE;

    void initShapes();
    void recordShapeDraw(VkCommandBuffer cmd, uint32_t frame);
    void cleanupShapes();

    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void create_swapchain();
    void create_render_pass();
    void create_framebuffers();
    void create_sync_objects();
    void create_command_buffers();

    // Destroys framebuffers/image-views/swapchain and recreates them (plus
    // overlay_'s size-dependent resources) at the surface's current extent.
    // Called on VK_ERROR_OUT_OF_DATE_KHR/VK_SUBOPTIMAL_KHR from draw(). No-op
    // if the surface is currently 0x0 (minimized) — retried next draw() call.
    void recreate_swapchain();
    void destroy_swapchain_resources();

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    void cleanup();
};
