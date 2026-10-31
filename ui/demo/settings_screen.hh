#pragma once
#include <string>

#include "context.hh"
#include "theme.hh"

// Example screen for the context API. It is deliberately a plain function:
// the app owns values and layout, the toolkit owns interaction state.
namespace ui::demo {

struct SettingsState {
    // Application values only. Hot/active/focus/scroll/text-edit state lives in
    // ui::StateStore, which is why none of those fields appear here.
    bool  wifi = false;
    bool  notifications = true;
    int   mode = 1;          // 0 = light, 1 = dark
    float volume = 0.4f;
    std::string name = "Ada";
    int   applies = 0;
};

// The app builds its Context with this theme so the mode radio takes effect on
// the next frame. Theme selection is app policy, not widget state.
inline Theme themeFor(const SettingsState& state) {
    return state.mode == 0 ? kLightTheme : kDarkTheme;
}

// Row geometry shared by drawSettingsScreen and tests, so a test clicks the
// same rect the screen drew without duplicating the layout maths.
struct SettingsLayout {
    Rect header{}, caption{};
    Rect wifi{}, notifications{};
    Rect modeLight{}, modeDark{};
    Rect volume{}, name{}, apply{};
};

SettingsLayout layoutSettings(const Rect& area, const Theme& theme,
                              const TypeScale& type);

void drawSettingsScreen(Context& c, SettingsState& state, const Rect& area);

}  // namespace ui::demo
