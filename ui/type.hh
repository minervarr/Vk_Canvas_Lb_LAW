#pragma once
#include "theme.hh"
#include "widgets.hh"   // widgets::TextFit

// Type-role policy: one place that turns the numeric ladder into size + overflow
// behaviour. A caller asks for a role, not a percentage.
namespace ui {

struct TypeStyle {
    float size = 0.0f;
    widgets::TextFit fit{};
};

inline TypeStyle typeStyle(const TypeScale& scale, TypeRole role) {
    switch (role) {
        case TypeRole::Caption:
            // Small labels are the most likely to collide with their neighbours;
            // truncate before a caption spills over a control.
            return {scale.text.caption, {true, 0.85f, true}};
        case TypeRole::Secondary:
            return {scale.text.secondary, {true, 0.85f, true}};
        case TypeRole::Body:
            // Body copy is caller-controlled: wrapping is a layout decision, not
            // a per-widget one, so leave it uncut unless the caller asks.
            return {scale.text.body, widgets::kTextFree};
        case TypeRole::Title:
            return {scale.text.title, {true, 0.90f, true}};
        case TypeRole::Header:
            return {scale.text.header, {true, 0.90f, true}};
    }
    return {scale.text.body, widgets::kTextFree};
}

inline float typeSize(const TypeScale& scale, TypeRole role) {
    return typeStyle(scale, role).size;
}

inline Color typeColor(const Theme& theme, TypeRole role) {
    switch (role) {
        case TypeRole::Caption:
        case TypeRole::Secondary:
            return theme.colors.secondary;
        case TypeRole::Body:
        case TypeRole::Title:
            return theme.colors.primary;
        case TypeRole::Header:
            return theme.colors.accent;
    }
    return theme.colors.primary;
}

}  // namespace ui
