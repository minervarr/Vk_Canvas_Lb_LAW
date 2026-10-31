#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "context.hh"
#include "id.hh"
#include "theme.hh"
#include "type.hh"

// Context-based toolkit widgets.
//
// Same drawing code as the legacy `widgets::` functions, but interaction state
// (hot/active/focus/text-edit) and styling come from ui::Context and ui::Theme.
// The legacy API stays untouched for existing consumers.
namespace ui {

// Left-aligned (or centred) text at a type role's size and overflow policy.
void label(Context& c, const Rect& row, std::string_view text,
           TypeRole role = TypeRole::Body, bool centered = false);

// Section divider: Header role, accent colour.
void groupHeader(Context& c, const Rect& row, std::string_view title);

// Clickable / keyboard-activatable controls. Return true when the value or
// selection changed this frame.
bool toggle(Context& c, Id id, const Rect& row, std::string_view label, bool& value);
bool radio(Context& c, Id id, const Rect& row, std::string_view label,
           int& selected, int value);
bool slider(Context& c, Id id, const Rect& row, std::string_view label,
            float& value, std::string_view valueText = {});

// A button press. Returns true on the frame it is clicked or activated.
bool button(Context& c, Id id, const Rect& rect, std::string_view label);

// Single-line text field. `text` is adopted on first use; afterwards the
// toolkit-owned TextFieldState is authoritative and `text` is refreshed from it.
// Returns true when the text changed this frame.
bool textField(Context& c, Id id, const Rect& row, std::string& text,
               std::string_view placeholder = {});

// Windowed list. The toolkit owns scrollPx (wheel + keyboard reveal) and the
// click target; `selected` is app value. Returns the clicked row index, or -1.
int scrollList(Context& c, Id id, const Rect& area,
               const std::vector<std::string>& items, int& selected,
               float rowHeight,
               const widgets::TextFit& fit = widgets::kTextFree);

}  // namespace ui
