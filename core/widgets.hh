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

// ── Radio row:  ( •) label ───────────────────────────────────────────────────
// One row of an exclusive-choice group (dot precedes the label, matching
// conventional radio layout — unlike drawToggle's trailing switch). The
// hover/selection highlight is a rounded pill sized to the DOT+LABEL content
// (plus a small anchor margin), never the full `row` width — so a short label
// gets a short highlight, not an edge-to-edge bar. Returns that content rect:
// cache it and hit-test against it (it is both what's lit and what's clicked),
// the same "compute during draw, hit-test reads it back" split the other
// widgets use. Colors are parameterized (defaults reproduce the col:: look) so
// a themed consumer can pass its own palette; see the TextFit pattern above.
struct RadioStyle {
  Color dotOn   = col::accent;   // filled dot when selected
  Color dotOff  = col::track;    // filled dot when not selected
  Color textOn  = col::accent;   // label color when selected
  Color textOff = col::text;     // label color when not selected
  Color hoverBg = col::track;    // content-fitted pill behind a hovered row
  Color selBg   = {0, 0, 0, 0};  // pill behind the selected row (transparent = none)
  Color selBar  = {0, 0, 0, 0};  // thin left accent bar on the selected row (transparent = none)
  float radius  = 8.0f;          // corner radius of the pill
};
inline constexpr RadioStyle kRadioDefault{};

Rect drawRadioRow(Canvas& c, const Rect& row, bool selected, bool hovered,
                  std::string_view label, const TextFit& fit = kTextFree,
                  const RadioStyle& style = kRadioDefault);

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
// Caller owns scrollPx. Draws a backing panel + visible rows clipped to `area`;
// returns the visible rows (rect + item index) for hit-testing. `selected` is
// highlighted; `hoverIndex` (item index, -1 = none) gets a subtle pointer
// highlight behind its text.
//
// The look is parameterized via ScrollListStyle (defaults reproduce the
// original col:: appearance, so present/future framework-native callers are
// unaffected). `selection` picks the highlight language: `Pill` is the
// original inset rounded-fill; `BottomBorder` underlines every row (thin when
// unselected, thick+accent when selected) and tints the selected row's text —
// for consumers whose visual language is an underline, not a fill. Pass a
// `background` matching the caller's own page background to make the backing
// box invisible.
enum class ListSelectionStyle { Pill, BottomBorder };
struct ScrollListStyle {
  Color background       = col::panel2;  // backing box behind the whole list
  Color rowText          = col::text;    // unselected row text
  Color hoverBg          = col::track;   // hover highlight fill
  ListSelectionStyle selection = ListSelectionStyle::Pill;
  Color pillColor        = col::accent;  // Pill: selected-row fill
  Color pillText         = col::text;    // Pill: selected-row text
  Color borderSelected   = col::accent;  // BottomBorder: selected underline + text
  Color borderUnselected = col::dim;     // BottomBorder: every row's thin separator
  Color selectedBar      = {0, 0, 0, 0}; // Pill: thin left accent bar on the selected row
  float radius           = 8.0f;         // Pill: selection/hover fill corner radius
};
inline constexpr ScrollListStyle kScrollListDefault{};

struct ListRow { Rect rect; int index; };
std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex = -1,
                                    const TextFit& fit = kTextFree,
                                    const ScrollListStyle& style = kScrollListDefault);

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
