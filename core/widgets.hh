#pragma once
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "canvas.hh"
#include "frame_input.hh"

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

// Reproduces drawSortableTable's original cell-text behavior exactly (shrink
// down to 60% before overflowing, never ellipsis) — the default for its new
// `cellFit` parameter so existing callers see no change unless they opt in
// to a different TextFit (e.g. ellipsis truncation instead of shrinking).
inline constexpr TextFit kTableLegacyFit{true, 0.6f, false};

// Applies a TextFit to a single line: returns the size to draw at and
// rewrites `s` (ellipsis) when the policy calls for it. Public so custom
// draw code can use the same escalation as the stock widgets.
float applyTextFit(Canvas& c, std::string& s, float maxW, float size,
                   const TextFit& fit);

// ── Toggle:  label .......................... ( ●) ───────────────────────────
// The whole row toggles; `switchRect` is only for an optional tighter hit-test.
struct ToggleStyle {
  Color onColor   = col::accent;
  Color offColor  = col::track;
  Color knobColor = col::thumb;
};
inline constexpr ToggleStyle kToggleDefault{};

Rect toggleSwitchRect(const Rect& row);
void drawToggle(Canvas& c, const Rect& row, bool on, std::string_view label,
                const ToggleStyle& style = kToggleDefault);

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
struct StepperStyle {
  Color buttonBg   = col::btnIdle;
  Color buttonText = col::text;
  Color valueBg    = col::track;
  Color valueText  = col::text;
};
inline constexpr StepperStyle kStepperDefault{};

struct StepperGeom { Rect minus, value, plus; };
StepperGeom stepperGeom(const Rect& row);
void drawStepper(Canvas& c, const Rect& row, std::string_view label,
                 std::string_view valueText,
                 const StepperStyle& style = kStepperDefault);

// ── Slider:  label ......... [══●────]  value ────────────────────────────────
struct SliderStyle {
  Color track     = col::track;
  Color fill      = col::accent;
  Color thumb     = col::thumb;
  Color valueText = col::dim;
};
inline constexpr SliderStyle kSliderDefault{};

struct SliderGeom { Rect bar; Rect thumb; };
SliderGeom sliderGeom(const Rect& row, float t01);
// Pointer x → t in [0,1] across the bar (clamped).
float sliderValueAt(const Rect& row, float px);
void drawSlider(Canvas& c, const Rect& row, float t01,
                std::string_view label, std::string_view valueText,
                const SliderStyle& style = kSliderDefault);

// ── Segmented control (N exclusive options; also used as tabs) ───────────────
struct SegmentedStyle {
  Color selectedBg     = col::accent;
  Color unselectedBg   = col::btnIdle;
  Color selectedText   = col::text;
  Color unselectedText = col::dim;
};
inline constexpr SegmentedStyle kSegmentedDefault{};

// Allocation-free: the i-th segment rect by formula (use this for hit-testing).
Rect segmentRectAt(const Rect& row, int count, int i);
std::vector<Rect> segmentRects(const Rect& row, int count);  // convenience
// Core: contiguous options. The vector/initializer_list overloads forward here.
void drawSegmented(Canvas& c, const Rect& row,
                   const std::string_view* options, int count, int selected,
                   const SegmentedStyle& style = kSegmentedDefault);
inline void drawSegmented(Canvas& c, const Rect& row,
                          const std::vector<std::string_view>& options, int selected,
                          const SegmentedStyle& style = kSegmentedDefault) {
  drawSegmented(c, row, options.data(), (int)options.size(), selected, style);
}
inline void drawSegmented(Canvas& c, const Rect& row,
                          std::initializer_list<std::string_view> options, int selected,
                          const SegmentedStyle& style = kSegmentedDefault) {
  drawSegmented(c, row, options.begin(), (int)options.size(), selected, style);
}

// ── Dropdown field (closed state) — opening shows a ScrollList overlay ───────
struct DropdownStyle {
  Color labelText    = col::text;
  Color fieldBg      = col::track;
  Color fieldHoverBg = {0.28f, 0.28f, 0.35f, 1.0f};  // track, lifted
  Color valueText    = col::text;
  Color chevron      = col::dim;
};
inline constexpr DropdownStyle kDropdownDefault{};

// `hovered` tints the field fill for pointer feedback (desktop hosts pass
// row.contains(pointer); touch hosts leave the default). With a TextFit,
// label and value are fitted to their zones and the value's zone excludes
// the chevron, so contact is geometrically impossible.
void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value,
                       bool hovered = false,
                       const TextFit& fit = kTextFree,
                       const DropdownStyle& style = kDropdownDefault);

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
  bool  fitWidth         = false;        // Pill: hug each row's text instead of full width
};
inline constexpr ScrollListStyle kScrollListDefault{};

struct ListRow { Rect rect; int index; };
std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex = -1,
                                    const TextFit& fit = kTextFree,
                                    const ScrollListStyle& style = kScrollListDefault);

// ── Text field (single-line editable UTF-8 text input) ──────────────────────
// Minimal single-line editor: insertion/deletion/cursor movement all operate
// on whole Unicode codepoints, never splitting a multi-byte UTF-8 sequence —
// the entire point being that a search/settings text box must accept
// accented/CJK/etc. text correctly (see FrameInput::typedCodepoints).
// Stateless like every widget here except for the state the caller must
// persist across frames: TextFieldState is just data, own it wherever you
// own the rest of your screen's UI state.
struct TextFieldState {
  std::string text;
  size_t cursorByte = 0;        // byte offset into `text`, always on a codepoint boundary
  size_t selectionAnchor = 0;   // == cursorByte means no active selection
  double lastClickTimeSec = -1e9;  // for double-click-to-select-word detection
  size_t lastClickByte = 0;
};

// Core-only clipboard seam: textFieldHandleInput lives in the
// platform-agnostic engine and must never call into Wayland/Win32 directly.
// Each host implements this (forwarding to its native clipboard) and passes
// an instance in; omit it (nullptr) on a host that hasn't wired one up yet —
// Ctrl+C/Ctrl+V simply become no-ops.
struct ClipboardIo {
  virtual void setText(const std::string& utf8) = 0;
  virtual std::string getText() = 0;
  virtual ~ClipboardIo() = default;
};

struct TextFieldStyle {
  Color bg          = col::track;
  Color text        = col::text;
  Color placeholder = col::dim;
  Color cursor      = col::accent;
  Color selection   = {col::accent.r, col::accent.g, col::accent.b, 0.35f};
};
inline constexpr TextFieldStyle kTextFieldDefault{};

// Applies this frame's typed codepoints plus editing/navigation/selection
// shortcuts from `input` to `state`: Backspace/Delete/Left/Right/Home/End
// (unchanged from before), Shift+<those> to extend/shrink the selection,
// Ctrl+Left/Right to jump by word (Ctrl+Shift+ to extend by word), Ctrl+A to
// select all, Ctrl+C/Ctrl+V to copy/paste through `clipboard` (no-op if
// null). Call only when the field has focus. Returns true if `state` changed.
bool textFieldHandleInput(TextFieldState& state, const FrameInput& input,
                          ClipboardIo* clipboard = nullptr);

// Single click positions the cursor at the byte offset nearest `input`'s
// pointer and collapses the selection; a second click within ~0.4s and a few
// pixels of the first (double-click) selects the whole word under the point
// instead. Needs `c` to measure text the same way drawTextField does. Call
// once per frame such a click should be processed against this field (i.e.
// when input.pointerWentDown and the field owns focus). Returns true if
// `state` changed.
bool textFieldHandleClick(TextFieldState& state, Canvas& c, const Rect& fieldRect,
                          const FrameInput& input, double nowSeconds,
                          const TextFieldStyle& style = kTextFieldDefault);

void drawTextField(Canvas& c, const Rect& row, const TextFieldState& state,
                   bool focused, std::string_view placeholder = {},
                   const TextFieldStyle& style = kTextFieldDefault);

// ── Sortable table (multi-column list with a clickable/sortable header) ─────
// A generalization of drawScrollList to several named columns with a sort
// indicator. Stateless like every widget here: sortColumn/sortAscending are
// caller-owned — this widget only draws and reports geometry, it never
// re-sorts data itself. Header column hit-testing follows the same
// "compute geometry separately, draw reads it back" split as
// segmentRectAt()/segmentRects(): call tableHeaderColumnRects() to get the
// rects to hit-test a click against, same as you would for drawSegmented().
struct TableColumn {
  std::string label;
  float weight = 1.0f;   // relative width (2.0f = twice as wide as 1.0f)
};

struct TableStyle {
  Color headerBg    = col::panel2;
  Color headerText  = col::text;
  Color headerHover = col::track;
  Color sortGlyph   = col::accent;
  Color rowBg       = col::panel2;
  Color rowText     = col::text;
  Color hoverBg     = col::track;
  Color gridLine    = col::dim;
  float radius      = 8.0f;
  // Off by default (reproduces the original look): column separators stop
  // at the header and rows have no separator at all. Set true for a denser,
  // more literally grid-like table — separators run the full header+body
  // height and every row gets a bottom separator line.
  bool  fullGrid    = false;
};
inline constexpr TableStyle kTableDefault{};

struct TableRow { Rect rect; int index; };

// Cell text for (row, col); caller owns the underlying data (e.g. indexes
// into its own results vector) — this keeps the widget generic across any
// tabular data, not just one app's result rows.
using TableCellFn = std::function<std::string(int row, int col)>;

// Header row geometry, at rowH tall, docked to the top of `area`. `widths`,
// when non-null and sized to `columns`, gives each column's pixel width
// directly instead of splitting `columns[i].weight` proportionally — for a
// caller that lets its user drag columns to an explicit size. Null (the
// default) preserves the original weight-based behavior.
Rect tableHeaderRow(const Rect& area, float rowH);
Rect tableHeaderColumnRect(const Rect& headerRow, const std::vector<TableColumn>& columns, int col,
                          const std::vector<float>* widths = nullptr);
std::vector<Rect> tableHeaderColumnRects(const Rect& headerRow, const std::vector<TableColumn>& columns,
                                        const std::vector<float>* widths = nullptr);

// Draws the header (with a sort-direction glyph on the active column) and a
// vertically scrolling body below it, windowed the same way as
// drawScrollList (caller owns scrollPx/rowH). Returns the visible body rows
// for hit-testing (rect + row index), same convention as drawScrollList's
// ListRow. `hoverRow`/`hoverHeaderCol` are draw-time-only visual feedback
// (-1 = none); actual click handling is the caller's job against the
// returned/precomputed rects. `cellFit` controls cell-text overflow (default
// reproduces the original shrink-only behavior, see kTableLegacyFit);
// `columnWidthsPx` overrides weight-based column sizing the same way as
// tableHeaderColumnRects' `widths` above.
std::vector<TableRow> drawSortableTable(Canvas& c, const Rect& area,
                                        const std::vector<TableColumn>& columns,
                                        const TableCellFn& cellText, int rowCount,
                                        int sortColumn, bool sortAscending,
                                        float scrollPx, float rowH,
                                        int hoverRow = -1, int hoverHeaderCol = -1,
                                        const TableStyle& style = kTableDefault,
                                        const TextFit& cellFit = kTableLegacyFit,
                                        const std::vector<float>* columnWidthsPx = nullptr);

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
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title,
                     Color color = col::accent);

}  // namespace widgets
