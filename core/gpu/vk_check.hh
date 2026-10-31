#pragma once
// VCE_VK_CHECK(expr) — checked Vulkan call for paths where a failure cannot be
// recovered from. It evaluates the expression exactly once, prints the call and
// source location, and aborts instead of letting a half-initialized object run.
//
// Do NOT wrap calls whose VkResult is part of the protocol (acquire/present
// return VK_ERROR_OUT_OF_DATE_KHR and friends); those need their own handling.
// Phase 3 is the pass that applies this across every init/teardown call; it is
// introduced here so new code has one obvious checked form.

#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>

inline void vce_vk_check(VkResult result, const char* expr,
                         const char* file, int line) {
    if (result == VK_SUCCESS) return;
    std::fprintf(stderr,
                 "vk_canvas: %s failed with VkResult %d at %s:%d\n",
                 expr, static_cast<int>(result), file, line);
    std::abort();
}

#define VCE_VK_CHECK(expr) vce_vk_check((expr), #expr, __FILE__, __LINE__)
