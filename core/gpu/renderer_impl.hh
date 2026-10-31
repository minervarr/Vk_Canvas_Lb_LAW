#pragma once
// Private state and helpers for Renderer. Nothing here is part of the public
// API: renderer.hh exposes only the opaque Impl pointer, which is what keeps
// the Vulkan overlay, image layer and font atlas out of consumers' includes.

#include "renderer.hh"
#include "overlay.hh"
#include "image_layer.hh"
#include "msdf.hh"
#include "log.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

static inline int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ── composite_frag.slang's push block, mirrored ────────────────────────────
//
// COMPOSITE_PC_BEGIN
struct CompositePush {
    float hlg;          // legacy: HLG when > 0.5, the camera's original field
    int   transfer;     // 0 SDR, 1 HLG, 2 PQ
    float peakNits;     // display peak for the PQ tone map
    int   rotQuadrant;  // clockwise quarter-turns: 0..3
    float uvScaleX;     // the picture's fraction of the allocated buffer
    float uvScaleY;
    float zoom;         // camera loupe; <= 1 is off
    float cx;
    float cy;
    float peak;         // camera focus peaking; <= 0 is off
    float texelX;
    float texelY;
    float peakColor;
};
// COMPOSITE_PC_END
//
// The field names and their ORDER must match the cbuffer in
// first_party/vulkan_font_engine/shaders_src/composite_frag.slang. Nothing at
// runtime checks this — a push block is a memcpy into a struct no validation
// layer inspects — so cmake/check_composite_pc.cmake compares the two lists
// between the COMPOSITE_PC_BEGIN/END markers here and the shader's own, and
// fails the build when they drift.
//
// They did drift once, and the result was a video player that rotated every
// frame the wrong way, skipped its PQ decode entirely, and ran the camera's
// focus-peaking filter over the picture, all without one line of diagnostic.
//
// std430: every member is 4 bytes and the shader's float2 uvScale is 8-byte
// aligned at offset 16, which this flat layout already satisfies. Append at
// the end; never reorder.
static_assert(sizeof(CompositePush) == 52, "composite_frag PC block is 52 bytes");

#define LOG_TAG "vk_canvas"
#define LOGI(...) VCE_LOGI(LOG_TAG, __VA_ARGS__)
#define LOGE(...) VCE_LOGE(LOG_TAG, __VA_ARGS__)

struct Renderer::Impl {
    Impl(SurfaceProvider& surface, AssetReader& assets,
         uint32_t desiredSwapchainImages,
         OutputTarget requestedOutput,
         PresentPolicy presentPolicy);
    ~Impl();

    void draw(const std::vector<float>& overlay_curves, int overlay_rotation_deg,
              const std::vector<ImageDraw>& images,
              const std::vector<ImageDraw>& foregroundImages,
              const std::vector<float>& msdfQuads,
              const std::vector<float>& shapeVerts,
              const std::vector<float>& frontShapeVerts);
    bool readbackLastFrame(std::vector<uint8_t>& rgba_out,
                           uint32_t& out_w, uint32_t& out_h);
    bool recreate_surface();
    void initMsdf(const TextFont& font);
    void set_external_colour(VkSamplerYcbcrModelConversion model,
                             VkSamplerYcbcrRange range,
                             const VkChromaLocation* xChromaOffset,
                             const VkChromaLocation* yChromaOffset);
    void update_external_frame(void* handle, std::function<void()> release_cb = nullptr);

    bool msdfReady() const { return msdfPipeline_ != VK_NULL_HANDLE; }
    bool isSoftwareDevice() const {
        return device_type_ == VK_PHYSICAL_DEVICE_TYPE_CPU;
    }

#if defined(__ANDROID__)
    void update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb = nullptr);
    void clear_camera_frames();
#endif

    SurfaceProvider&  surface_provider_;
    AssetReader&      assets_;
    FrameWaker*       waker_ = nullptr;
    std::atomic<bool> frame_ready_{false};

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    uint32_t desired_swapchain_images_ = 4;  // constructor arg; see create_swapchain()
    float          camera_hlg_ = 0.0f;
    float          camera_loupe_zoom_ = 1.0f;
    float          loupe_rect_[4] = {0, 0, 0, 0};
    float          camera_peak_ = 0.0f;
    float          camera_peak_color_ = 0.0f;

    DeviceCaps caps_{};

    // Perf instrumentation (logged once per second).
    int64_t pv_prev_ns_ = 0, pv_window_ns_ = 0, pv_max_gap_ns_ = 0; int pv_frames_ = 0;
    int64_t dr_window_ns_ = 0, dr_max_ns_ = 0; int dr_frames_ = 0;
    FrameStats last_stats_{};
    uint64_t   draw_count_ = 0;

    VkInstance       instance_       = VK_NULL_HANDLE;
    VkSurfaceKHR     surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice physical_dev_   = VK_NULL_HANDLE;
    VkDevice         device_         = VK_NULL_HANDLE;
    VkQueue          queue_          = VK_NULL_HANDLE;
    VkSwapchainKHR   swapchain_      = VK_NULL_HANDLE;

    // Chosen together with physical_dev_: a graphics family that also supports
    // presenting to THIS surface. 0 is the historical value and the fallback.
    uint32_t             queue_family_ = 0;
    std::string          device_name_;
    VkPhysicalDeviceType device_type_ = VK_PHYSICAL_DEVICE_TYPE_OTHER;

    // Swapchain pixel format/colorspace — resolved at create_swapchain() by
    // pickTarget() (output_target.hh) from the ctor's requested target and the
    // surface's own format enumeration. Falls back to the 8-bit SDR pin
    // whenever the request can't be honored. Shared by the render pass and
    // framebuffer views; output_.encode is also baked into every pipeline as
    // the OUTPUT_ENCODE specialization constant.
    OutputTarget     requested_output_ = OutputTarget::SdrSrgb;
    PresentPolicy    present_policy_   = PresentPolicy::Latency;
    OutputSelection  output_{};
    bool             output_resolved_ = false;  // resolve_output_target() ran
    VkFormat         swapchain_format_     = VK_FORMAT_R8G8B8A8_UNORM;
    VkColorSpaceKHR  swapchain_colorspace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool             ext_swapchain_colorspace_ = false;  // instance ext enabled

    // Linear text composite. text_srgb_pass_ means the swapchain was created
    // mutable and text is drawn in a second pass through an sRGB view, so the
    // hardware decodes the page, blends, and encodes. text_linear_surface_
    // is that pass, or a float linear swapchain in the same pass. Both false
    // is the legacy UNORM encode (exact only on black). Decided once, before
    // the first swapchain: the UI render pass's finalLayout and the text
    // pipeline bake it in, and neither is rebuilt on resize. A resize must
    // keep the mutable swapchain for as long as text_srgb_pass_ stays set.
    // text_mutable_refused_ stops a failed first create from being retried
    // on every resize.
    bool             device_mutable_swapchain_ = false;
    bool             text_srgb_pass_           = false;
    bool             text_linear_surface_      = false;
    bool             text_mutable_refused_     = false;
    VkFormat         text_srgb_format_         = VK_FORMAT_UNDEFINED;
    VkRenderPass     text_render_pass_         = VK_NULL_HANDLE;
    std::vector<VkImageView>   text_image_views_;
    std::vector<VkFramebuffer> text_framebuffers_;

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
    bool        present_enabled_ = true;   // capture seam, see setPresentEnabled
    // Which frame-fence last submitted to each swapchain image — guards
    // re-recording cmd_buffers_[image] while that image's prior frame runs.
    std::vector<VkFence> image_fences_;
    // The swapchain image index that draw() last rendered into, for
    // readbackLastFrame(). UINT32_MAX until the first draw().
    uint32_t    last_drawn_image_index_ = UINT32_MAX;

    // The sampler conversion built for the external image. A plain Vulkan
    // object, and the thing set_external_colour() guards against being created
    // before it is told the truth — so it belongs with the description above
    // rather than with the Android import below.
    VkSamplerYcbcrConversion        ycbcr_conversion_ = VK_NULL_HANDLE;

    // ── What the external image IS ─────────────────────────────────────────
    //
    // Outside the platform guard below, because every one of these describes
    // the CONTENT rather than how it got here: its Y'CbCr matrix and range,
    // its transfer function, its orientation, and how much of the buffer is
    // actually picture. All of that is the same fact on any host. Only the
    // import beneath it is per-platform.
    // camera_hlg_ lives with the loupe state above: it is the preview's HLG
    // flag, not a property of an imported video frame.
    // Empty until set_external_colour() overrides the driver's suggestion.
    bool                            external_colour_set_ = false;
    VkSamplerYcbcrModelConversion   external_model_ = VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY;
    VkSamplerYcbcrRange             external_range_ = VK_SAMPLER_YCBCR_RANGE_ITU_FULL;
    // Unset means "the driver's suggestion stands", which is why these are
    // flags beside the values rather than a sentinel inside them: every
    // VkChromaLocation is a legitimate answer.
    bool                            external_siting_set_ = false;
    VkChromaLocation                external_x_siting_ = VK_CHROMA_LOCATION_COSITED_EVEN;
    VkChromaLocation                external_y_siting_ = VK_CHROMA_LOCATION_MIDPOINT;
    ExternalTransfer                external_transfer_ = ExternalTransfer::Sdr;
    float                           external_peak_nits_ = 1000.0f;
    int                             external_rotation_quadrant_ = 1;
    uint32_t                        external_visible_w_ = 0;
    uint32_t                        external_visible_h_ = 0;
    float                           external_pixel_aspect_ = 1.0f;

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
    // Bind order, oldest first — the eviction queue for the above. A plain
    // vector: it holds at most kMaxCachedHwb entries and is walked, not
    // searched, so a map would cost more than it saved.
    std::vector<AHardwareBuffer*> hwb_lru_;
    // How many external images may be cached at once, and therefore the size
    // of the descriptor pool backing them. A camera needs a handful; a video
    // decoder's AImageReader was measured handing out 10 distinct buffers in
    // the first second, so this is sized for a stream and bounded by eviction.
    static constexpr size_t kMaxCachedHwb = 32;

    AHardwareBuffer* current_hwb_ = nullptr;
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
    void touch_hwb(AHardwareBuffer* hwb);
#endif

    // UI overlay (canvas curve records rasterised + composited over the camera).
    OverlayRasterizer overlay_;
    // Textured quads (album art, icons) composited as a background layer.
    ImageLayer image_layer_;

    // ── MSDF text pipeline ──────────────────────────────────────────────────
    //
    // Where the per-frame text VBO STARTS. It used to be a hard ceiling, and
    // the overflow path was `if (verts > kMaxMsdfVerts) verts = kMaxMsdfVerts`
    // — a silent truncation that is not even a clean one: 16384 is not a
    // multiple of the 6 vertices in a glyph quad, so the clamp left four
    // orphan vertices behind, i.e. one malformed triangle at whatever UV the
    // glyph it cut in half happened to carry. A dense screen at a high
    // resolution reaches 2730 glyphs easily, so this was a real ceiling, and
    // "the text stops halfway down the page" is not a failure mode worth
    // keeping. The buffer grows instead; see ensureMsdfVboCapacity().
    static constexpr uint32_t kInitialMsdfVerts = 16384;

    VkImage             msdfAtlasImage_    = VK_NULL_HANDLE;
    VkDeviceMemory      msdfAtlasMemory_   = VK_NULL_HANDLE;
    VkImageView         msdfAtlasView_     = VK_NULL_HANDLE;
    VkSampler           msdfAtlasSampler_  = VK_NULL_HANDLE;
    uint32_t            msdfAtlasW_        = 0;
    uint32_t            msdfAtlasH_        = 0;
    uint32_t            msdfAtlasLayers_   = 1;   // atlas pages, as array layers
    uint32_t            msdfAtlasGen_      = 0;   // ++ on every image (re)creation
    float               msdfPxRange_      = 4.0f;
    float               msdfIsMtsdf_      = 0.0f;  // 0 = MSDF, 1 = MTSDF, 2 = raster
    VkFormat            msdfAtlasFormat_  = VK_FORMAT_R8G8B8A8_UNORM;

    VkBuffer            msdfVbo_[kFramesInFlight]       = {};
    VkDeviceMemory      msdfVboMemory_[kFramesInFlight] = {};
    void*               msdfVboMapped_[kFramesInFlight] = {};
    uint32_t            msdfVboVerts_[kFramesInFlight]  = {};  // capacity, in vertices
    uint32_t            msdfVertCount_     = 0;  // for the frame being recorded

    // Make frame slot `frame`'s text VBO hold at least `verts` vertices,
    // reallocating it if it does not. Only legal once that slot's fence has
    // been waited on — draw() does exactly that before it writes the buffer.
    bool ensureMsdfVboCapacity(uint32_t frame, uint32_t verts);

    VkDescriptorSetLayout msdfSetLayout_   = VK_NULL_HANDLE;
    VkDescriptorPool    msdfDescPool_      = VK_NULL_HANDLE;
    VkDescriptorSet     msdfDescSet_       = VK_NULL_HANDLE;
    VkPipelineLayout    msdfPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline          msdfPipeline_      = VK_NULL_HANDLE;

    void createMsdfPipeline();
    // `pixels` holds `layers` pages of w*h*channels, one after another, and
    // they become the layers of one 2D array image.
    void uploadMsdfAtlas(const uint8_t* pixels, uint32_t w, uint32_t h,
                         uint32_t layers, float pxRange, uint32_t channels);

    // Re-upload only the pages the font reports as changed, keeping the image,
    // view, sampler, descriptor set and pipeline. Only valid when the atlas's
    // shape is unchanged — initMsdf() decides that.
    void updateMsdfAtlasPages(const TextFont& font);
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
    static constexpr uint32_t kShapeVertsPerQuad  = 6;
    // Where the per-frame shape VBO STARTS. It used to be a hard ceiling
    // (`if (verts > kMaxShapeVerts) verts = kMaxShapeVerts`), and that
    // silent truncation is how a bitmap UI vanished mid-glyph: Terminus is
    // one rect per pixel-run, so 2048 quads is a few dozen letters. The
    // text VBO already grows; this one does too. See ensureShapeVboCapacity().
    static constexpr uint32_t kInitialShapeVerts = kShapeVertsPerQuad * 2048;

    VkBuffer         shapeVbo_[kFramesInFlight]       = {};
    VkDeviceMemory   shapeVboMemory_[kFramesInFlight] = {};
    void*            shapeVboMapped_[kFramesInFlight] = {};
    uint32_t         shapeVboVerts_[kFramesInFlight]  = {};  // capacity, in vertices
    uint32_t         shapeVertCount_ = 0;  // for the frame being recorded
    uint32_t         shapeFrontFirst_ = 0;
    uint32_t         shapeFrontCount_ = 0;
    VkPipelineLayout shapePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       shapePipeline_       = VK_NULL_HANDLE;

    void initShapes();
    void recordShapeDraw(VkCommandBuffer cmd, uint32_t frame,
                         uint32_t firstVert, uint32_t vertCount);
    void cleanupShapes();
    bool ensureShapeVboCapacity(uint32_t frame, uint32_t verts);

    void create_instance();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    // Resolves the output target once, from the constructor, before the first
    // create_swapchain(). See the definition for why it must not re-run.
    void resolve_output_target();
    void resolve_text_composite();
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
