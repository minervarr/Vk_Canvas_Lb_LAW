#include "demo/settings_screen.hh"

#include <algorithm>

#include "widgets_context.hh"

namespace ui::demo {
using namespace ui::literals;

SettingsLayout layoutSettings(const Rect& area, const Theme& theme,
                              const TypeScale& type) {
    const Spacing sp = theme.scaledSpacing();
    const float scale = type.scale;
    const float pad = sp.lg * scale;
    const float gap = sp.xs * scale;
    const float rowH = std::max(sp.xl * 2.0f * scale, type.text.body * 2.4f);
    float y = area.y + pad;

    auto next = [&](float height) {
        Rect r{area.x + pad, y, area.w - pad * 2.0f, height};
        y += height + gap;
        return r;
    };

    SettingsLayout l;
    l.header        = next(type.text.header * 1.4f);
    l.caption       = next(type.text.caption * 1.6f);
    l.wifi          = next(rowH);
    l.notifications = next(rowH);
    l.modeLight     = next(rowH);
    l.modeDark      = next(rowH);
    l.volume        = next(rowH);
    l.name          = next(rowH);
    l.apply         = next(rowH * 1.1f);
    return l;
}

void drawSettingsScreen(Context& c, SettingsState& state, const Rect& area) {
    const SettingsLayout l = layoutSettings(area, c.theme(), c.type());

    label(c, l.header, "Settings", TypeRole::Header);
    label(c, l.caption, "Interaction state is owned by ui::Context", TypeRole::Caption);

    toggle(c, "demo.wifi"_id, l.wifi, "Wi-Fi", state.wifi);
    toggle(c, "demo.notifications"_id, l.notifications, "Notifications",
           state.notifications);

    radio(c, "demo.mode.light"_id, l.modeLight, "Light theme", state.mode, 0);
    radio(c, "demo.mode.dark"_id, l.modeDark, "Dark theme", state.mode, 1);

    slider(c, "demo.volume"_id, l.volume, "Volume", state.volume);
    textField(c, "demo.name"_id, l.name, state.name, "Your name");

    if (button(c, "demo.apply"_id, l.apply, "Apply")) {
        state.applies++;
    }
}

}  // namespace ui::demo
