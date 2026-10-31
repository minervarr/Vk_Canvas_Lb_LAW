// Pure unit test for the VkResult -> recovery-action mapping in vk_check.hh.
// No device, no instance, no Vulkan loader: the classifier is a switch and the
// point of the test is that the swapchain protocol states stay distinct.
#undef NDEBUG
#include "vk_check.hh"

#include <cassert>
#include <cstdio>

int main() {
    assert(vce_vk_classify(VK_SUCCESS) == VceVkResult::Success);
    assert(vce_vk_classify(VK_SUBOPTIMAL_KHR) == VceVkResult::Suboptimal);
    assert(vce_vk_classify(VK_ERROR_OUT_OF_DATE_KHR) == VceVkResult::OutOfDate);
    assert(vce_vk_classify(VK_ERROR_SURFACE_LOST_KHR) == VceVkResult::SurfaceLost);
    assert(vce_vk_classify(VK_ERROR_DEVICE_LOST) == VceVkResult::DeviceLost);

    // Everything else is a plain error: it must not be mistaken for a state
    // draw() can recover from by rebuilding the swapchain.
    assert(vce_vk_classify(VK_ERROR_OUT_OF_HOST_MEMORY) == VceVkResult::Error);
    assert(vce_vk_classify(VK_ERROR_INITIALIZATION_FAILED) == VceVkResult::Error);
    assert(vce_vk_classify(VK_TIMEOUT) == VceVkResult::Error);

    // vce_vk_ok is the non-aborting predicate the macros build on.
    assert(vce_vk_ok(VK_SUCCESS, "success", __FILE__, __LINE__));
    assert(!vce_vk_ok(VK_ERROR_OUT_OF_HOST_MEMORY, "oom", __FILE__, __LINE__));

    std::printf("vk_result_test: ok\n");
    return 0;
}
