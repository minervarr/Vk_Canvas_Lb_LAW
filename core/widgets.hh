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
void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value);

// ── Scrollable list (language picker, dropdown popup, generic rows) ──────────
// Caller owns scrollPx. Draws an opaque panel + visible rows clipped to `area`;
// returns the visible rows (rect + item index) for hit-testing. `selected` is
// highlighted.
struct ListRow { Rect rect; int index; };
std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH);
float listContentHeight(int n, float rowH);

// ── Group header label (section divider in a settings form) ─────────────────
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title);

}  // namespace widgets
