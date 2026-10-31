#pragma once
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#if defined(__ANDROID__)
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#endif
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "platform.hh"
#include "canvas.hh"
#include "texture.hh"
#include "output_target.hh"

class TextFont;

class Renderer {
public:
    // Owns Vulkan handles and their teardown. Copying one would duplicate the
    // ownership and double-destroy it; the compiler must reject it.
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // desiredSwapchainImages: how many swapchain images to request (clamped
    // to the surface's min/max). The default 4 exists for Android, where the
    // compositor can hold an image ~60ms mid-hitch (see create_swapchain());
    // desktop callers on MAILBOX can pass 3 and save one full-screen image.
    //
    // requestedOutput: what kind of surface to present into. The default
    // SdrSrgb is bit-identical to the pre-HDR behavior. An HDR request is
    // resolved against what the surface actually enumerates and silently
    // degrades to the SDR pin when unsupported — ask hdrActive() what you
    // got, never assume. See USAGE_hdr_output.md, and note the consumer-side
    // contract there: Android needs Window.setColorMode(COLOR_MODE_HDR) from
    // the app's Activity, desktop needs OS HDR enabled, or the HDR formats
    // are never advertised in the first place.
    // `presentPolicy` — Latency (MAILBOX) for a live preview, Vsync (FIFO) for
    // content presented on a schedule. See PresentPolicy in output_target.hh.
    Renderer(SurfaceProvider& surface, AssetReader& assets,
             uint32_t desiredSwapchainImages = 4,
             OutputTarget requestedOutput = OutputTarget::SdrSrgb,
             PresentPolicy presentPolicy = PresentPolicy::Latency);

    // What we actually got, after capability resolution — not what was asked.
    bool         hdrActive()    const;
    OutputTarget activeTarget() const;
    OutputEncode activeEncode() const;
    ~Renderer();

    // Composite, in order: the camera frame (if any); background textured
    // quads (album art — behind UI chrome); SDF shape quads (shapeVerts, the
    // fast primitive path — see Canvas::useShapes()); the UI overlay
    // described by overlay_curves (Canvas curve records), rotated by
    // overlay_rotation_deg (0/90/180/270) to follow device orientation;
    // foreground textured quads (icons/buttons — on top of UI chrome); then
    // MSDF text quads (drawn last so text is always on top). When the sRGB
    // text pass is active that draw is a second render pass, so the blend
    // reads the page in linear light.
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
              const std::vector<float>& shapeVerts = {},
              // Drawn after foreground images, so marks sit on top of a photo
              // that would otherwise cover the shape pass.
              const std::vector<float>& frontShapeVerts = {});

    // ── Surface lost and regained, WITHOUT losing the device ─────────────────
    //
    // For a platform whose window can die and come back under a living process
    // — Android's APP_CMD_TERM_WINDOW / APP_CMD_INIT_WINDOW. The old
    // VkSurfaceKHR is destroyed and a new one made from whatever the
    // SurfaceProvider now points at; everything that does NOT depend on the
    // surface survives: the instance, the device, the render pass, every
    // pipeline, the command pool, the glyph atlas, and every texture the
    // caller has uploaded (album art included).
    //
    // This exists because the alternative was destroying the Renderer whole
    // and building it again — instance, device, pipelines, atlas re-upload —
    // measured at ~370 ms on a moto g06 between the window arriving and the
    // first frame being presented, every time the listener came back from the
    // notification. It is the same subset recreate_swapchain() already
    // rebuilds, plus the surface itself; the split it draws is "the surface
    // changed" versus "the device is gone", which are genuinely different
    // events that were being treated as one.
    //
    // Returns FALSE when the new surface is not compatible with the pipelines
    // already built for the old one — a different format or colour space, which
    // the render pass is baked against. The caller must then fall back to
    // destroying and recreating the Renderer. That is a real possibility (an
    // HDR mode change, an external display) and not a theoretical one, which is
    // why this reports rather than asserts.
    bool recreate_surface();

    // Create the text pipeline, atlas texture and vertex buffer from a TextFont
    // (MTSDF or per-size raster — see text_font.hh).
    // Must be called once after the Renderer is constructed, before any draw()
    // that passes non-empty msdfQuads.
    void initMsdf(const TextFont& font);
    // Handles a GPU-side glyph baker needs to write the atlas itself.
    VkDevice         vkDevice() const;
    VkPhysicalDevice vkPhysicalDevice() const;
    VkCommandPool    vkCommandPool() const;
    VkQueue          vkQueue() const;
    VkImage          msdfAtlasImage() const;
    uint32_t         msdfAtlasLayers() const;
    // Bumped every time the atlas IMAGE is (re)created, and therefore emptied.
    // A consumer that wrote pixels into it has to notice that and write them
    // again — and it cannot do so by comparing VkImage handles, because Vulkan
    // recycles them: destroying a 2-page atlas and creating a 3-page one hands
    // back the same handle value, the comparison says "unchanged", and every
    // glyph baked into the old image is silently gone.
    uint32_t         msdfAtlasGeneration() const;

    bool msdfReady() const;

    // Uploads `rgba` (w*h*4 bytes, straight alpha) as a sampled texture for
    // Canvas::image() draws. See ImageLayer::create_texture for details
    // (incl. the mips flag — pass false for textures never minified).
    TextureHandle create_texture(const uint8_t* rgba, uint32_t w, uint32_t h,
                                 bool mips = true,
                                 TextureFormat fmt = TextureFormat::RGBA8_UNORM);
    void destroy_texture(TextureHandle handle);

    // Capture seam: skip vkQueuePresentKHR entirely. A headless surface has
    // no compositor to hand the image to, and on current Mesa a headless
    // present recycles the image - readbackLastFrame() then copies recycled,
    // empty memory even though the frame rendered fine. Suppressed present
    // leaves the just-rendered image in its final layout (PRESENT_SRC), which
    // is exactly what the readback's barrier expects. The swapchain-side
    // recreate that a failed present would trigger is skipped with it.
    // Off by default; capture hosts turn this on before the first draw().
    void setPresentEnabled(bool on);

    uint32_t width()  const;
    uint32_t height() const;

    // What was actually chosen, for a log line, a diagnostics panel or a
    // consumer that must warn when it fell back to a CPU rasterizer. A software
    // device is legitimate (CI, capture on a machine with no GPU) but never a
    // silent accident.
    const std::string&   deviceName()    const;
    VkPhysicalDeviceType deviceType()    const;
    bool                 isSoftwareDevice() const;
    uint32_t             queueFamily()   const;

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
    uint32_t swapchainImageCount() const;

    // Force an immediate swapchain rebuild at the surface's current extent.
    // draw() otherwise only discovers a resize lazily, via VK_SUBOPTIMAL_KHR
    // from vkAcquireNextImageKHR — fine for a window resized gradually via
    // live WM_SIZE drags, but a caller that jumps a hidden window straight to
    // a large size in one step (e.g. showing a fullscreen window) needs the
    // swapchain to already match before the next draw(), or that first frame
    // renders at the stale (smaller) extent and appears corrupted/skewed.
    void notifyResized();

    // Frame pacing: update_camera_frame() (camera callback thread) sets a flag and
    // wakes the render loop, so the loop can present exactly once per camera frame
    // instead of busy-presenting every iteration. Matching the preview's present
    // rate to the 30fps content (vs ~60-120 half-duplicate presents/sec) stops
    // SurfaceFlinger hunting the LTPO refresh rate — the ~once-per-second ~90ms
    // vkWaitForFences stall seen only while recording.
    void set_frame_waker(FrameWaker* waker);
    bool consume_frame_ready();

    const DeviceCaps& caps() const;

    // ── The platform-neutral way in for a decoded frame ────────────────────
    //
    // Same thing as update_camera_frame() below, with the platform's type
    // erased. A consumer that carries frames around as opaque handles — a
    // video player whose decode/render seam is a void* on purpose — can hand
    // one straight over without naming AHardwareBuffer, which on a portable
    // source file it has no way to name.
    //
    // The identity of `handle` is the platform's business and stays inside the
    // engine: AHardwareBuffer* on Android, and on a desktop host it will be
    // the decoder's VkImage, which needs no import at all. That asymmetry is
    // exactly the sort of thing an engine should absorb rather than export.
    void update_external_frame(void* handle, std::function<void()> release_cb = nullptr);

#if defined(__ANDROID__)
    void update_camera_frame(AHardwareBuffer* hwb, std::function<void()> release_cb = nullptr);
    void clear_camera_frames();

    // While recording the camera delivers HLG frames; this enables the shader's
    // HLG->SDR tone-map so the preview isn't washed out.
    void set_camera_hlg(bool hlg);

    // Focus-check loupe: draws the camera frame a second time, magnified about
    // the frame's centre, into a square inset. The wide view underneath is left
    // intact — the point is to judge focus without losing the framing. `zoom`
    // <= 1 turns it off. The caller is responsible for feeding a preview stream
    // with enough resolution to make the magnification worth looking at.
    void set_camera_loupe(float zoom);
    float camera_loupe_zoom() const;
    // Where the inset was last drawn (x, y, w, h in surface pixels), so an
    // overlay can frame it. False when the loupe is off or no camera frame has
    // been composited yet. The value is one frame old, which does not matter:
    // it only changes when the loupe or the frame size does.
    // Focus peaking: paints hard luminance edges a flat colour so critical
    // focus is unmistakable. `threshold` is the per-texel luma gradient above
    // which a pixel counts as an edge — smaller is more sensitive; <= 0 is off.
    // Peaking is only as truthful as the image it runs on, so it is at its most
    // accurate inside the loupe, where the bound stream is full-sensor.
    void set_camera_peaking(float threshold, bool green = false);

    bool camera_loupe_rect(float out[4]) const;
#endif

    // ── Telling the engine what colour an external image actually is ────────
    //
    // Not Android-only, though it lived inside that guard until a second host
    // needed it. Every one of these describes the CONTENT — its matrix, its
    // transfer, its orientation, how much of the buffer is picture — and the
    // content is the same wherever it is decoded. The platform-specific part
    // is the import, which is update_external_frame() above.
    //
    // The Y'CbCr conversion built for an imported AHardwareBuffer takes the
    // driver's suggestedYcbcrModel/Range by default, and for a camera buffer
    // that is right: the producer and the gralloc format agree.
    //
    // For a VIDEO frame it is wrong. A P010 buffer out of a decoder carries no
    // colourspace, so the driver suggests BT.709 for content that is BT.2020 —
    // and the resulting matrix error is a consistent hue shift, which reads as
    // a grading choice rather than as a bug. The consumer knows the truth (it
    // came out of the container) and states it here.
    //
    // Must be called BEFORE the first update_camera_frame(), because the
    // conversion object is created on the first buffer and every cached image
    // view references it. Calling it later is ignored, loudly.
    //
    // ExternalTransfer selects the fragment stage's transfer function, which a
    // sampler cannot apply. Sdr is what every existing consumer already got.
    enum class ExternalTransfer : int { Sdr = 0, Hlg = 1, Pq = 2 };
    //
    // The chroma LOCATIONS are the same argument one step further in. 4:2:0
    // has one chroma sample per four luma, and where it sits in that quad is a
    // property of the encode that only the container states. The driver
    // suggests here too, and its suggestion is about the gralloc buffer's
    // layout rather than about the video — so a consumer that knows says so,
    // exactly as with the matrix. Both default to the driver's suggestion,
    // which is what every existing consumer already gets.
    void set_external_colour(VkSamplerYcbcrModelConversion model,
                             VkSamplerYcbcrRange range,
                             const VkChromaLocation* xChromaOffset = nullptr,
                             const VkChromaLocation* yChromaOffset = nullptr);
    // How much of a decoder's buffer is actually picture.
    //
    // Hardware decoders allocate aligned up to what their hardware wants: a
    // 2040x1530 video arrives in a 2048x1536 AHardwareBuffer, and the extra 8
    // columns and 6 rows hold whatever was already in memory. Sampling the
    // whole buffer smears that across the right and bottom edges — a band a
    // millimetre wide, which is exactly what it looks like on a phone.
    //
    // The producer knows the real size (AImage_getWidth/Height, or the codec's
    // crop rectangle) and states it here. 0 means "the whole buffer", which is
    // what a camera wants and what every existing consumer gets by default.
    // Quarter turns CLOCKWISE applied to the external image, given in degrees
    // and snapped to a quadrant. Defaults to 90 because that is what the
    // Android camera preview has always needed and what this path used to do
    // unconditionally; a video consumer sets it from its container, where 0 is
    // the common answer.
    void set_external_rotation(int degrees);

    void set_external_visible_size(uint32_t w, uint32_t h);

    void set_external_transfer(ExternalTransfer t, float displayPeakNits = 1000.0f);

    // Pixel aspect ratio: how wide a pixel is relative to its height.
    //
    // 1.0 for everything a camera produces and for most video, which is why
    // the letterbox assumed it. Not everything: a container can say its pixels
    // are not square (Matroska's DisplayWidth/DisplayHeight against
    // PixelWidth/PixelHeight), and such a file is stored narrow and meant to
    // be shown wide. Drawn at its pixel aspect it is simply the wrong shape —
    // visibly so, and in a way that looks like a bad encode rather than a bad
    // player.
    //
    // Applied to the picture's own width, before any rotation quadrant swaps
    // the axes. Ignored unless positive.
    void set_external_pixel_aspect(float par);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
