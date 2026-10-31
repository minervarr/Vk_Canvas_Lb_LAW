// Regression test for the "zero steady-state heap allocations" goal in the UI
// frame build. It is deliberately a counting global operator new rather than a
// timing test: the old per-frame copies in ctx_label/ctx_slider/scroll list and
// the discarded drawScrollList() vector are deterministic, and a wall-clock
// assertion would be flaky on a shared machine.
//
// The test warms the persistent stores (unordered_map nodes, focus-order
// vector capacity, Canvas buffers), snapshots the allocation count, then runs
// several complete frames. Any allocation after warm-up fails the test.
#undef NDEBUG
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "canvas.hh"
#include "frame_input.hh"
#include "widgets_context.hh"

namespace {
std::atomic<size_t> g_allocations{0};

void* countedAlloc(std::size_t n) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t a) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    const std::size_t align = static_cast<std::size_t>(a);
    const std::size_t rounded = (n + align - 1) / align * align;
    if (void* p = std::aligned_alloc(align, rounded ? rounded : align)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t a) noexcept { operator delete(p, a); }

int main() {
    std::vector<float> curves, shapes;
    curves.reserve(1u << 20);
    shapes.reserve(1u << 20);

    Canvas canvas(curves, 1000, 800, nullptr, 0, 0, 0, 0);
    canvas.useShapes(&shapes);

    using namespace ui::literals;
    FrameInput input;
    ui::StateStore store;
    ui::Theme theme = ui::kDarkTheme;
    ui::TypeScale type = ui::typeScale(800.0f, 14.0f);

    std::string name =
        "A name long enough to exercise the text field's non-SSO path, "
        "which used to be copied back into the caller every frame";
    int selected = 3;
    bool wifi = true;
    float volume = 0.4f;
    const std::vector<std::string> items = {
        "Alpha item with a reasonably long label", "Bravo item with a label",
        "Charlie item with a label", "Delta item with a label",
        "Echo item with a label", "Foxtrot item with a label",
        "Golf item with a label", "Hotel item with a label",
        "India item with a label", "Juliet item with a label"};

    auto frame = [&] {
        curves.clear();
        shapes.clear();
        ui::Context ctx(canvas, input, store, theme, type);
        ui::label(ctx, {20, 20, 500, 30},
                  "A body label that is longer than the small-string buffer",
                  ui::TypeRole::Body);
        ui::toggle(ctx, "frame.wifi"_id, {20, 60, 500, 40}, "Wi-Fi", wifi);
        ui::radio(ctx, "frame.radio"_id, {20, 110, 500, 40}, "Radio label", selected, 3);
        ui::slider(ctx, "frame.slider"_id, {20, 160, 500, 40}, "Volume", volume);
        ui::textField(ctx, "frame.name"_id, {20, 210, 500, 40}, name, "Placeholder");
        ui::scrollList(ctx, "frame.list"_id, {20, 270, 500, 400}, items, selected,
                       36.0f, widgets::kTextFree);
    };

    // Warm up persistent state and reserve every buffer the frame needs.
    for (int i = 0; i < 4; ++i) frame();
    g_allocations.store(0, std::memory_order_relaxed);

    for (int i = 0; i < 10; ++i) frame();

    const size_t allocs = g_allocations.load(std::memory_order_relaxed);
    std::printf("steady-state frame allocations: %zu\n", allocs);
    assert(allocs == 0);
    return 0;
}
