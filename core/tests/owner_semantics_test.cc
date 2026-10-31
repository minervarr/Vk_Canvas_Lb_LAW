// Compile-time ownership contract. These types own Vulkan handles and expose
// cleanup()/destructors; copying one would duplicate ownership and double-free.
// A static_assert is the cheapest possible regression test for that rule.
#undef NDEBUG
#include <cstdio>
#include <type_traits>

#include "image_layer.hh"
#include "overlay.hh"
#include "renderer.hh"

static_assert(!std::is_copy_constructible_v<Renderer>);
static_assert(!std::is_copy_assignable_v<Renderer>);
static_assert(!std::is_copy_constructible_v<OverlayRasterizer>);
static_assert(!std::is_copy_assignable_v<OverlayRasterizer>);
static_assert(!std::is_copy_constructible_v<ImageLayer>);
static_assert(!std::is_copy_assignable_v<ImageLayer>);

int main() {
    std::printf("owner_semantics_test: all assertions passed\n");
    return 0;
}
