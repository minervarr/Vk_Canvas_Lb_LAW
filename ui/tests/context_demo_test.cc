// CPU-only integration test for the demo settings screen. It drives the same
// public screen function an app would call, through simulated frames, and
// asserts app values change without the app owning hot/active/focus state.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "context.hh"
#include "demo/settings_screen.hh"
#include "id.hh"
#include "keys.hh"
#include "state.hh"
#include "theme.hh"

namespace {
using namespace ui::literals;

bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }
}  // namespace

int main() {
    std::vector<float> curves;
    Canvas canvas(curves, 800, 600, nullptr, 0, 0, 0, 0);
    FrameInput input;
    ui::StateStore store;
    ui::Theme theme = ui::kDarkTheme;
    ui::TypeScale type = ui::typeScale(1080.0f, 16.0f);
    ui::demo::SettingsState state;
    const Rect area{0, 0, 800, 600};
    const ui::demo::SettingsLayout layout =
        ui::demo::layoutSettings(area, theme, type);

    auto draw = [&](double now) {
        ui::Theme frameTheme = ui::demo::themeFor(state);
        ui::Context c(canvas, input, store, frameTheme, type, now);
        ui::demo::drawSettingsScreen(c, state, area);
    };

    // First frame: everything draws and nothing is focused yet.
    input.beginFrame();
    draw(0.0);
    assert(!curves.empty());
    assert(store.focused() == 0);
    assert(!state.wifi);

    // Pointer click on Wi-Fi: press, then release.
    input.beginFrame();
    input.pointerX = layout.wifi.x + 20.0f;
    input.pointerY = layout.wifi.y + layout.wifi.h * 0.5f;
    input.pointerDown = true;
    input.pointerWentDown = true;
    draw(0.016);
    assert(store.active() == "demo.wifi"_id);

    input.beginFrame();
    input.pointerX = layout.wifi.x + 20.0f;
    input.pointerY = layout.wifi.y + layout.wifi.h * 0.5f;
    input.pointerWentUp = true;
    draw(0.032);
    assert(state.wifi);
    assert(store.focused() == "demo.wifi"_id);

    // Tab moves to the next widget (Notifications); Enter toggles it.
    input.beginFrame();
    input.keysWentDown.push_back(key::Tab);
    draw(0.048);
    assert(store.focused() == "demo.notifications"_id);

    input.beginFrame();
    input.keysWentDown.push_back(key::Enter);
    draw(0.064);
    assert(!state.notifications);

    // Radio click selects the light theme; the next frame's Context is built
    // from that app value by themeFor().
    input.beginFrame();
    input.pointerX = layout.modeLight.x + 20.0f;
    input.pointerY = layout.modeLight.y + layout.modeLight.h * 0.5f;
    input.pointerDown = true;
    input.pointerWentDown = true;
    draw(0.072);
    input.beginFrame();
    input.pointerX = layout.modeLight.x + 20.0f;
    input.pointerY = layout.modeLight.y + layout.modeLight.h * 0.5f;
    input.pointerWentUp = true;
    draw(0.076);
    assert(state.mode == 0);
    assert(ui::demo::themeFor(state).colors.background.r > 0.9f);

    // Slider press at 75% of the bar.
    const float barX0 = layout.volume.x + layout.volume.w * 0.34f;
    const float barX1 = layout.volume.x + layout.volume.w * 0.84f;
    input.beginFrame();
    input.pointerX = barX0 + (barX1 - barX0) * 0.75f;
    input.pointerY = layout.volume.y + layout.volume.h * 0.5f;
    input.pointerDown = true;
    input.pointerWentDown = true;
    draw(0.080);
    assert(near(state.volume, 0.75f));
    input.beginFrame();
    input.pointerX = barX0 + (barX1 - barX0) * 0.75f;
    input.pointerY = layout.volume.y + layout.volume.h * 0.5f;
    input.pointerWentUp = true;
    draw(0.096);

    // Text field: click at the end of the row, then type.
    input.beginFrame();
    input.pointerX = layout.name.x + layout.name.w - 4.0f;
    input.pointerY = layout.name.y + layout.name.h * 0.5f;
    input.pointerDown = true;
    input.pointerWentDown = true;
    draw(0.112);
    assert(store.focused() == "demo.name"_id);
    input.beginFrame();
    input.pointerX = layout.name.x + layout.name.w - 4.0f;
    input.pointerY = layout.name.y + layout.name.h * 0.5f;
    input.pointerWentUp = true;
    draw(0.128);

    input.beginFrame();
    input.typedCodepoints = U"!";
    draw(0.144);
    assert(state.name == "Ada!");

    // Apply button click increments the app value once.
    input.beginFrame();
    input.pointerX = layout.apply.x + layout.apply.w * 0.5f;
    input.pointerY = layout.apply.y + layout.apply.h * 0.5f;
    input.pointerDown = true;
    input.pointerWentDown = true;
    draw(0.160);
    input.beginFrame();
    input.pointerX = layout.apply.x + layout.apply.w * 0.5f;
    input.pointerY = layout.apply.y + layout.apply.h * 0.5f;
    input.pointerWentUp = true;
    draw(0.176);
    assert(state.applies == 1);

    std::printf("context_demo_test: OK\n");
    return 0;
}
