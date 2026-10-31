#pragma once
// Checked Vulkan calls.
//
// Three failure policies, chosen by the caller:
//
//   VCE_VK_CHECK(expr)          report and abort. For invariants and teardown,
//                               where continuing would run a half-built object.
//   VCE_VK_CHECK_THROW(expr)    report and throw std::runtime_error. For
//                               constructors and other paths that may fail
//                               recoverably; the caller unwinds and cleans up.
//   VCE_VK_CHECK_OR(expr, act)  report and run `act` (usually `return false`).
//                               For runtime paths that degrade: an allocation
//                               that fails is a smaller frame, not a dead app.
//
// calls whose VkResult is part of the protocol (acquire/present return
// VK_ERROR_OUT_OF_DATE_KHR and friends) are NOT wrapped; classify those with
// vce_vk_classify() and react. See docs/RELIABILITY.md and
// docs/decisions/0003-vulkan-error-and-recovery-policy.md.

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

// How a caller should react to a VkResult. Success and Error are the only two
// that are not swapchain protocol states; Suboptimal still produced a usable
// image, OutOfDate/SurfaceLost need a rebuild, DeviceLost needs the renderer
// torn down.
enum class VceVkResult {
    Success,
    Suboptimal,
    OutOfDate,
    SurfaceLost,
    DeviceLost,
    Error,
};

inline VceVkResult vce_vk_classify(VkResult result) {
    switch (result) {
    case VK_SUCCESS:                 return VceVkResult::Success;
    case VK_SUBOPTIMAL_KHR:          return VceVkResult::Suboptimal;
    case VK_ERROR_OUT_OF_DATE_KHR:   return VceVkResult::OutOfDate;
    case VK_ERROR_SURFACE_LOST_KHR:  return VceVkResult::SurfaceLost;
    case VK_ERROR_DEVICE_LOST:       return VceVkResult::DeviceLost;
    default:                         return VceVkResult::Error;
    }
}

inline bool vce_vk_ok(VkResult result, const char* expr,
                      const char* file, int line) {
    if (result == VK_SUCCESS) return true;
    std::fprintf(stderr,
                 "vk_canvas: %s failed with VkResult %d at %s:%d\n",
                 expr, static_cast<int>(result), file, line);
    return false;
}

#define VCE_VK_CHECK(expr)                                                   \
    do {                                                                     \
        if (!vce_vk_ok((expr), #expr, __FILE__, __LINE__)) std::abort();      \
    } while (0)

#define VCE_VK_CHECK_THROW(expr)                                             \
    do {                                                                     \
        if (!vce_vk_ok((expr), #expr, __FILE__, __LINE__))                    \
            throw std::runtime_error(std::string("Vulkan call failed: ") +    \
                                     #expr);                                  \
    } while (0)

#define VCE_VK_CHECK_OR(expr, action)                                        \
    do {                                                                     \
        if (!vce_vk_ok((expr), #expr, __FILE__, __LINE__)) { action; }        \
    } while (0)
