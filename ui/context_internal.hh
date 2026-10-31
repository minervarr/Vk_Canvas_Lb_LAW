#pragma once
// Internal style adaptation from ui::Theme to the legacy widgets:: style
// structs. Not part of the public header surface.
#include "theme.hh"
#include "type.hh"
#include "widgets.hh"

namespace ui::detail {

inline widgets::ToggleStyle toggleStyle(const Theme& t) {
    widgets::ToggleStyle s;
    s.onColor = t.colors.accent;
    s.offColor = t.colors.track;
    s.knobColor = t.colors.thumb;
    return s;
}

inline widgets::RadioStyle radioStyle(const Theme& t) {
    widgets::RadioStyle s;
    s.dotOn = t.colors.accent;
    s.dotOff = t.colors.track;
    s.textOn = t.colors.accent;
    s.textOff = t.colors.primary;
    s.hoverBg = t.colors.track;
    s.selBg = {0, 0, 0, 0};
    s.selBar = {0, 0, 0, 0};
    s.radius = t.radius;
    return s;
}

inline widgets::SliderStyle sliderStyle(const Theme& t) {
    widgets::SliderStyle s;
    s.track = t.colors.track;
    s.fill = t.colors.accent;
    s.thumb = t.colors.thumb;
    s.valueText = t.colors.secondary;
    return s;
}

inline widgets::TextFieldStyle textFieldStyle(const Theme& t) {
    widgets::TextFieldStyle s;
    s.bg = t.colors.track;
    s.text = t.colors.primary;
    s.placeholder = t.colors.dim;
    s.cursor = t.colors.accent;
    s.selection = {t.colors.accent.r, t.colors.accent.g, t.colors.accent.b, 0.35f};
    return s;
}

inline widgets::ScrollListStyle scrollListStyle(const Theme& t) {
    widgets::ScrollListStyle s;
    s.background = t.colors.panel;
    s.rowText = t.colors.primary;
    s.hoverBg = t.colors.track;
    s.hoverText = t.colors.primary;
    s.selection = widgets::ListSelectionStyle::Pill;
    s.pillColor = t.colors.accent;
    s.pillText = t.colors.onAccent;
    s.selectedBar = {0, 0, 0, 0};
    s.radius = t.radius;
    return s;
}

inline widgets::TextFit bodyFit(const TypeScale& type) {
    return typeStyle(type, TypeRole::Body).fit;
}

}  // namespace ui::detail
