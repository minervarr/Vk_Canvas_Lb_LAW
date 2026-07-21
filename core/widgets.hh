#pragma once
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "canvas.hh"

// Reusable immediate-mode, touch-first widgets for the canvas engine. Each
// widget separates PURE GEOMETRY (sub-rects derived from a row Rect, so drawing
// and hit-testing share one source of truth) from a DRAW call on Canvas. State
// (values, selection, scroll) is owned by the caller — these helpers are
// stateless. No Android/whisper dependencies.
namespace widgets {

// ── Text-overflow policy (OPT-IN) ────────────────────────────────────────────
// The default everywhere is kTextFree: text draws at its nominal size and may
// overflow its widget — exactly the historical behavior, full caller control.
// Pass kTextFit (or your own TextFit) to widgets that accept one when you
// want the shortcut: shrink down to minScale first, then ellipsis-truncate.
// Each escalation step is individually optional, so any mix is expressible.
struct TextFit {
  bool  shrink   = false;   // scale the size down until the text fits...
  float minScale = 0.75f;   // ...but never below minScale * nominal size
  bool  ellipsis = false;   // past the floor, truncate with "..."
};
inline constexpr TextFit kTextFree{};                       // overflow allowed
inline constexpr TextFit kTextFit{true, 0.75f, true};       // the shortcut

// Applies a TextFit to a single line: returns the size to draw at and
// rewrites `s` (ellipsis) when the policy calls for it. Public so custom
// draw code can use the same escalation as the stock widgets.
float applyTextFit(Canvas& c, std::string& s, float maxW, float size,
                   const TextFit& fit);

// ── Toggle:  label .......................... ( ●) ───────────────────────────
// The whole row toggles; `switchRect` is only for an optional tighter hit-test.
Rect toggleSwitchRect(const Rect& row);
void drawToggle(Canvas& c, const Rect& row, bool on, std::string_view label);

// ── Stepper:  label ............... [ − ]  value  [ + ] ──────────────────────
struct StepperGeom { Rect minus, value, plus; };
StepperGeom stepperGeom(const Rect& row);
void drawStepper(Canvas& c, const Rect& row, std::string_view label,
                 std::string_view valueText);

// ── Slider:  label ......... [══●────]  value ────────────────────────────────
struct SliderGeom { Rect bar; Rect thumb; };
SliderGeom sliderGeom(const Rect& row, float t01);
// Pointer x → t in [0,1] across the bar (clamped).
float sliderValueAt(const Rect& row, float px);
void drawSlider(Canvas& c, const Rect& row, float t01,
                std::string_view label, std::string_view valueText);

// ── Segmented control (N exclusive options; also used as tabs) ───────────────
// Allocation-free: the i-th segment rect by formula (use this for hit-testing).
Rect segmentRectAt(const Rect& row, int count, int i);
std::vector<Rect> segmentRects(const Rect& row, int count);  // convenience
// Core: contiguous options. The vector/initializer_list overloads forward here.
void drawSegmented(Canvas& c, const Rect& row,
                   const std::string_view* options, int count, int selected);
inline void drawSegmented(Canvas& c, const Rect& row,
                          const std::vector<std::string_view>& options, int selected) {
  drawSegmented(c, row, options.data(), (int)options.size(), selected);
}
inline void drawSegmented(Canvas& c, const Rect& row,
                          std::initializer_list<std::string_view> options, int selected) {
  drawSegmented(c, row, options.begin(), (int)options.size(), selected);
}

// ── Dropdown field (closed state) — opening shows a ScrollList overlay ───────
// `hovered` tints the field fill for pointer feedback (desktop hosts pass
// row.contains(pointer); touch hosts leave the default). With a TextFit,
// label and value are fitted to their zones and the value's zone excludes
// the chevron, so contact is geometrically impossible.
void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value,
                       bool hovered = false,
                       const TextFit& fit = kTextFree);

// ── Scrollable list (language picker, dropdown popup, generic rows) ──────────
// Caller owns scrollPx. Draws an opaque panel + visible rows clipped to `area`;
// returns the visible rows (rect + item index) for hit-testing. `selected` is
// highlighted; `hoverIndex` (item index, -1 = none) gets a subtle pointer
// highlight behind its text.
struct ListRow { Rect rect; int index; };
std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex = -1,
                                    const TextFit& fit = kTextFree);

// ── Fit-aware button ─────────────────────────────────────────────────────────
// Canvas::button with an overflow strategy: fits the label per `fit`, and —
// when `allowTwoLines` and the rect has the vertical room — word-splits an
// overlong label onto two centered lines before shrinking below ~85%.
// Canvas::button itself is untouched; use whichever fits the situation.
void drawFitButton(Canvas& c, const Rect& r, std::string_view label,
                   Color bg, Color fg, float radius = 0.0f,
                   const TextFit& fit = kTextFit, bool allowTwoLines = true);
float listContentHeight(int n, float rowH);

// ── Group header label (section divider in a settings form) ─────────────────
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title);

}  // namespace widgets
