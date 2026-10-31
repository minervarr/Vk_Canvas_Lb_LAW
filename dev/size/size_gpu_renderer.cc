// Size probe: Renderer entry point. Taking the address of the draw forwarder
// pulls renderer_api.o and the full GPU archive behind it. (A trivial inline
// forwarder such as deviceLost() would be pruned to a field read and measure
// almost nothing.) Never executed by the size script.
#include "renderer.hh"

// External linkage plus `used` keeps the relocation in the object even at -O3;
// this is the only reference the probe needs.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((used))
#endif
void (Renderer::*g_renderer_entry)(
    const std::vector<float>&, int, const std::vector<ImageDraw>&,
    const std::vector<ImageDraw>&, const std::vector<float>&,
    const std::vector<float>&, const std::vector<float>&) = &Renderer::draw;

int main(int argc, char**) {
    return argc == 42 && g_renderer_entry ? 1 : 0;
}
