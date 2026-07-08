#pragma once
// Platform seam: everything the engine core needs from the host platform.
// Android implements these over AAssetManager/ANativeWindow/ALooper
// (android_platform.hh); desktop backends implement them over the filesystem
// and their native window system. Core code must depend only on this header,
// never on platform SDK headers.

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

// AssetReader — reads a bundled asset (APK assets on Android, exe-relative dir
// on desktop) by relative path, e.g. "shaders/tiling.spv". The definition is
// owned by the font engine submodule (resolved via vk_font_core's public
// include dir) so both libraries share one interface and implementations pass
// straight through.
#include "asset_reader.hh"

// Owns the native window handle; the renderer never sees it.
// instance_extensions() is queried before vkCreateInstance; create() may be
// called again after the platform recreates its window (Android
// APP_CMD_TERM_WINDOW / APP_CMD_INIT_WINDOW cycle).
struct SurfaceProvider {
    virtual std::vector<const char*> instance_extensions() const = 0;
    virtual VkSurfaceKHR create(VkInstance instance) = 0;
    virtual VkExtent2D   extent() const = 0;   // current drawable size in px
    virtual ~SurfaceProvider() = default;
};

// Wakes the platform's render loop from another thread (ALooper_wake on
// Android; PostMessage/SetEvent on Win32) so a pushed frame presents promptly.
struct FrameWaker {
    virtual void wake() = 0;
    virtual ~FrameWaker() = default;
};

// Filled once at physical-device selection. The renderer gates optional
// techniques on THIS, never on the platform: a capable phone gets the full
// path, a weak desktop GPU gets the fallback. Extend per-feature as needed.
struct DeviceCaps {
    uint32_t     api_version                       = 0;
    uint32_t     max_image_dim_2d                  = 0;
    uint32_t     max_compute_workgroup_invocations = 0;
    VkDeviceSize max_storage_buffer_range          = 0;
    bool         has_sampler_ycbcr_conversion      = false;  // gates camera path
    bool         has_external_memory_ahb           = false;  // Android HWB import
};
