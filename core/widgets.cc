#include "widgets.hh"

#include "keys.hh"
#include "msdf.hh"       // FontStyle
#include "text_util.hh"  // fitTextSize / truncateToWidth / splitTwoLines

#include <algorithm>

namespace widgets {

// ── UTF-8 helpers for TextFieldState (byte-boundary-safe editing) ──────────
namespace {
size_t Utf8SeqLen(unsigned char b) {
  if ((b & 0x80) == 0x00) return 1;
  if ((b & 0xE0) == 0xC0) return 2;
  if ((b & 0xF0) == 0xE0) return 3;
  if ((b & 0xF8) == 0xF0) return 4;
  return 1;  // invalid lead byte — treat as one byte so we always make progress
}
size_t Utf8PrevBoundary(const std::string& s, size_t pos) {
  if (pos == 0) return 0;
  size_t i = pos - 1;
  while (i > 0 && (((unsigned char)s[i]) & 0xC0) == 0x80) --i;
  return i;
}
size_t Utf8NextBoundary(const std::string& s, size_t pos) {
  if (pos >= s.size()) return s.size();
  return pos + Utf8SeqLen((unsigned char)s[pos]);
}
std::string Utf8Encode(char32_t cp) {
  std::string out;
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
  return out;
}
}  // namespace

// ── Word-boundary + click-mapping helpers (selection, Ctrl+arrows, double-
// click-to-select-word) ─────────────────────────────────────────────────────
namespace {
// Anything alphanumeric/underscore, or any byte of a multi-byte UTF-8
// sequence (>=0x80), counts as "part of a word" — so accented/CJK/etc. text
// is one word, not many. Everything else (space, punctuation, symbols) is a
// "special character" and a word boundary.
bool IsWordChar(unsigned char b) {
  return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
         b == '_' || b >= 0x80;
}
size_t PrevWordBoundary(const std::string& s, size_t pos) {
  while (pos > 0 && !IsWordChar((unsigned char)s[pos - 1])) --pos;
  while (pos > 0 && IsWordChar((unsigned char)s[pos - 1])) --pos;
  return pos;
}
size_t NextWordBoundary(const std::string& s, size_t pos) {
  while (pos < s.size() && !IsWordChar((unsigned char)s[pos])) ++pos;
  while (pos < s.size() && IsWordChar((unsigned char)s[pos])) ++pos;
  return pos;
}
// [start,end) of the word containing `pos`; empty range if `pos` sits on a
// special character/space (a double-click there selects nothing).
std::pair<size_t, size_t> WordAt(const std::string& s, size_t pos) {
  if (pos >= s.size() || !IsWordChar((unsigned char)s[pos])) return {pos, pos};
  size_t start = pos, end = pos;
  while (start > 0 && IsWordChar((unsigned char)s[start - 1])) --start;
  while (end < s.size() && IsWordChar((unsigned char)s[end])) ++end;
  return {start, end};
}

// Left edge of the text run, after applying drawTextField's cursor-follow
// scroll — shared by drawTextField (render) and textFieldHandleClick (hit
// test) so a click maps to the same glyph the user sees under the pointer.
float ComputeTextX(Canvas& c, const Rect& row, const TextFieldState& state,
                   float pad, float maxW, float size) {
  float textX = row.x + pad;
  if (!state.text.empty()) {
    float prefixW = c.textWidth(std::string_view(state.text).substr(0, state.cursorByte), size);
    if (prefixW > maxW) textX -= (prefixW - maxW);
  }
  return textX;
}

// Byte offset whose glyph boundary lands closest to `targetX` pixels into
// the text run (0 = before the first codepoint).
size_t ByteOffsetAtX(Canvas& c, const std::string& text, float targetX, float size) {
  if (targetX <= 0.0f || text.empty()) return 0;
  size_t pos = 0;
  float prevW = 0.0f;
  while (pos < text.size()) {
    size_t next = Utf8NextBoundary(text, pos);
    float w = c.textWidth(std::string_view(text).substr(0, next), size);
    if (w >= targetX) return (targetX - prevW <= w - targetX) ? pos : next;
    prevW = w;
    pos = next;
  }
  return text.size();
}
}  // namespace

bool textFieldHandleInput(TextFieldState& state, const FrameInput& input,
                          ClipboardIo* clipboard) {
  bool changed = false;

  auto selStart = [&] { return std::min(state.selectionAnchor, state.cursorByte); };
  auto selEnd   = [&] { return std::max(state.selectionAnchor, state.cursorByte); };
  auto hasSelection = [&] { return state.selectionAnchor != state.cursorByte; };
  auto eraseSelection = [&] {
    size_t s = selStart(), e = selEnd();
    state.text.erase(s, e - s);
    state.cursorByte = s;
    state.selectionAnchor = s;
  };

  // An input method reported the field's contents outright (see TextEditEvent).
  // Adopt them and stop: the IME owns the buffer, so it has already applied
  // this frame's typing, deletions and composition. Running the delta path as
  // well would apply everything twice.
  if (input.textEdited) {
    if (state.text == input.editedText && state.cursorByte == input.editedCursorByte)
      return false;
    state.text = input.editedText;
    state.cursorByte = input.editedCursorByte;
    state.selectionAnchor = state.cursorByte;
    return true;
  }

  for (char32_t cp : input.typedCodepoints) {
    if (hasSelection()) eraseSelection();
    std::string enc = Utf8Encode(cp);
    state.text.insert(state.cursorByte, enc);
    state.cursorByte += enc.size();
    state.selectionAnchor = state.cursorByte;
    changed = true;
  }

  if (input.keyWentDown(key::Backspace)) {
    if (hasSelection()) { eraseSelection(); changed = true; }
    else if (state.cursorByte > 0) {
      size_t prev = Utf8PrevBoundary(state.text, state.cursorByte);
      state.text.erase(prev, state.cursorByte - prev);
      state.cursorByte = prev;
      state.selectionAnchor = prev;
      changed = true;
    }
  }
  if (input.keyWentDown(key::Delete)) {
    if (hasSelection()) { eraseSelection(); changed = true; }
    else if (state.cursorByte < state.text.size()) {
      size_t next = Utf8NextBoundary(state.text, state.cursorByte);
      state.text.erase(state.cursorByte, next - state.cursorByte);
      changed = true;
    }
  }
  if (input.keyWentDown(key::Left)) {
    size_t oldCursor = state.cursorByte, oldAnchor = state.selectionAnchor;
    if (hasSelection() && !input.shiftDown) state.cursorByte = selStart();
    else if (input.ctrlDown) state.cursorByte = PrevWordBoundary(state.text, state.cursorByte);
    else if (state.cursorByte > 0) state.cursorByte = Utf8PrevBoundary(state.text, state.cursorByte);
    if (!input.shiftDown) state.selectionAnchor = state.cursorByte;
    if (state.cursorByte != oldCursor || state.selectionAnchor != oldAnchor) changed = true;
  }
  if (input.keyWentDown(key::Right)) {
    size_t oldCursor = state.cursorByte, oldAnchor = state.selectionAnchor;
    if (hasSelection() && !input.shiftDown) state.cursorByte = selEnd();
    else if (input.ctrlDown) state.cursorByte = NextWordBoundary(state.text, state.cursorByte);
    else if (state.cursorByte < state.text.size()) state.cursorByte = Utf8NextBoundary(state.text, state.cursorByte);
    if (!input.shiftDown) state.selectionAnchor = state.cursorByte;
    if (state.cursorByte != oldCursor || state.selectionAnchor != oldAnchor) changed = true;
  }
  if (input.keyWentDown(key::Home)) {
    size_t oldCursor = state.cursorByte, oldAnchor = state.selectionAnchor;
    state.cursorByte = 0;
    if (!input.shiftDown) state.selectionAnchor = 0;
    if (state.cursorByte != oldCursor || state.selectionAnchor != oldAnchor) changed = true;
  }
  if (input.keyWentDown(key::End)) {
    size_t oldCursor = state.cursorByte, oldAnchor = state.selectionAnchor;
    state.cursorByte = state.text.size();
    if (!input.shiftDown) state.selectionAnchor = state.cursorByte;
    if (state.cursorByte != oldCursor || state.selectionAnchor != oldAnchor) changed = true;
  }
  if (input.ctrlDown && input.keyWentDown(key::A) &&
      (state.selectionAnchor != 0 || state.cursorByte != state.text.size())) {
    state.selectionAnchor = 0;
    state.cursorByte = state.text.size();
    changed = true;
  }
  if (input.ctrlDown && input.keyWentDown(key::C) && clipboard && hasSelection()) {
    clipboard->setText(state.text.substr(selStart(), selEnd() - selStart()));
  }
  if (input.ctrlDown && input.keyWentDown(key::V) && clipboard) {
    if (hasSelection()) eraseSelection();
    std::string paste = clipboard->getText();
    if (!paste.empty()) {
      state.text.insert(state.cursorByte, paste);
      state.cursorByte += paste.size();
      state.selectionAnchor = state.cursorByte;
      changed = true;
    }
  }

  return changed;
}

bool textFieldHandleClick(TextFieldState& state, Canvas& c, const Rect& fieldRect,
                          const FrameInput& input, double nowSeconds,
                          const TextFieldStyle& style) {
  (void)style;
  if (!input.pointerWentDown) return false;
  if (input.pointerX < fieldRect.x || input.pointerX > fieldRect.x + fieldRect.w ||
      input.pointerY < fieldRect.y || input.pointerY > fieldRect.y + fieldRect.h)
    return false;

  float size = fieldRect.h * 0.40f;
  float pad = fieldRect.h * 0.28f;
  float maxW = fieldRect.w - pad * 2.0f;
  float textX = ComputeTextX(c, fieldRect, state, pad, maxW, size);
  size_t clicked = ByteOffsetAtX(c, state.text, input.pointerX - textX, size);

  constexpr double kDoubleClickSeconds = 0.4;
  bool isDoubleClick = (nowSeconds - state.lastClickTimeSec) <= kDoubleClickSeconds &&
                       clicked == state.lastClickByte;

  bool changed = false;
  if (isDoubleClick) {
    auto [wordStart, wordEnd] = WordAt(state.text, clicked);
    size_t newAnchor = (wordEnd > wordStart) ? wordStart : clicked;
    size_t newCursor = (wordEnd > wordStart) ? wordEnd : clicked;
    changed = (newAnchor != state.selectionAnchor || newCursor != state.cursorByte);
    state.selectionAnchor = newAnchor;
    state.cursorByte = newCursor;
    // A third click starts a fresh single-click sequence instead of
    // re-triggering the double-click word-select immediately.
    state.lastClickTimeSec = -1e9;
  } else {
    changed = (state.cursorByte != clicked || state.selectionAnchor != clicked);
    state.cursorByte = clicked;
    state.selectionAnchor = clicked;
    state.lastClickTimeSec = nowSeconds;
    state.lastClickByte = clicked;
  }
  return changed;
}

void drawTextField(Canvas& c, const Rect& row, const TextFieldState& state,
                   bool focused, std::string_view placeholder,
                   const TextFieldStyle& style) {
  c.rect(row.x, row.y, row.w, row.h, style.bg, row.h * 0.22f);
  float s = row.h * 0.40f;
  float pad = row.h * 0.28f;
  float textY = row.y + (row.h - s) * 0.5f;
  float maxW = row.w - pad * 2.0f;

  // Always clip to the field — unclipped text (a long token/path) used to
  // spill out past the rounded box into whatever sat next to it.
  const auto savedClip = c.saveClip();
  c.setClip(row.x, row.y, row.w, row.h);
  if (state.text.empty()) {
    if (!placeholder.empty())
      c.text(placeholder, row.x + pad, textY, s, style.placeholder);
  } else {
    float textX = row.x + pad;
    if (focused) textX = ComputeTextX(c, row, state, pad, maxW, s);

    if (focused && state.selectionAnchor != state.cursorByte) {
      size_t s0 = std::min(state.selectionAnchor, state.cursorByte);
      size_t s1 = std::max(state.selectionAnchor, state.cursorByte);
      float x0 = textX + c.textWidth(std::string_view(state.text).substr(0, s0), s);
      float x1 = textX + c.textWidth(std::string_view(state.text).substr(0, s1), s);
      c.rect(x0, row.y + row.h * 0.15f, x1 - x0, row.h * 0.7f, style.selection);
    }

    c.text(state.text, textX, textY, s, style.text);
    if (focused && state.selectionAnchor == state.cursorByte) {
      float prefixW = c.textWidth(std::string_view(state.text).substr(0, state.cursorByte), s);
      float cursorX = textX + prefixW;
      c.rect(cursorX, row.y + row.h * 0.2f, row.h * 0.06f, row.h * 0.6f, style.cursor);
    }
  }
  c.restoreClip(savedClip);
}

namespace {
// Vertically-centred text top for size `s` within a row of height `h`.
inline float vcenter(const Rect& r, float s) { return r.y + (r.h - s) * 0.5f; }
inline float rowTextSize(const Rect& r) { return r.h * 0.40f; }
inline float labelPad(const Rect& r) { return r.h * 0.30f; }

// Draw a left-aligned label that shrinks to fit within [row.x, limitX] so it
// never runs into the control on the right. Floors at 60% of the row text size.
void drawLabelFit(Canvas& c, std::string_view label, const Rect& row, float limitX) {
  float maxW = limitX - row.x - labelPad(row);
  float s = rowTextSize(row);
  float floor = s * 0.6f;
  while (s > floor && c.textWidth(label, s) > maxW) s -= row.h * 0.03f;
  c.text(label, row.x, vcenter(row, s), s, col::text);
}
}  // namespace

// ── Toggle ───────────────────────────────────────────────────────────────────
Rect toggleSwitchRect(const Rect& row) {
  float h = row.h * 0.66f;
  float w = h * 1.8f;
  return {row.x + row.w - w, row.y + (row.h - h) * 0.5f, w, h};
}

void drawToggle(Canvas& c, const Rect& row, bool on, std::string_view label,
                const ToggleStyle& style) {
  Rect sw = toggleSwitchRect(row);
  drawLabelFit(c, label, row, sw.x);

  c.rect(sw.x, sw.y, sw.w, sw.h, on ? style.onColor : style.offColor, sw.h * 0.5f);
  float knob = sw.h * 0.82f;
  float ky = sw.y + (sw.h - knob) * 0.5f;
  float kx = on ? (sw.x + sw.w - knob - (sw.h - knob) * 0.5f)
                : (sw.x + (sw.h - knob) * 0.5f);
  c.rect(kx, ky, knob, knob, style.knobColor, knob * 0.5f);
}

// ── Radio row ─────────────────────────────────────────────────────────────────
Rect drawRadioRow(Canvas& c, const Rect& row, bool selected, bool hovered,
                  std::string_view label, const TextFit& fit, const RadioStyle& style) {
  float dotD = row.h * 0.40f;
  float pad  = row.h * 0.34f;   // anchor margin: left of the dot, right of the label
  float gap  = row.h * 0.32f;   // dot → label spacing
  Rect dot{row.x + pad, row.y + (row.h - dotD) * 0.5f, dotD, dotD};

  float labelX = dot.x + dot.w + gap;
  std::string text(label);
  float s = rowTextSize(row);
  float drawSize = applyTextFit(c, text, row.x + row.w - labelX - pad, s, fit);
  float textW = c.textWidth(text, drawSize);

  // Content-fitted hit/highlight rect: full row height (matches the row/button
  // height exactly — no vertical inset), only as wide as the dot + label + pad.
  Rect hit{row.x, row.y, (labelX + textW + pad) - row.x, row.h};
  {
    float rad = std::min(style.radius, hit.h * 0.5f);
    if (selected && style.selBg.a > 0.0f)
      c.rect(hit.x, hit.y, hit.w, hit.h, style.selBg, rad);
    else if (hovered)
      c.rect(hit.x, hit.y, hit.w, hit.h, style.hoverBg, rad);
    // Left accent bar marks the selected row (nav-style indicator).
    if (selected && style.selBar.a > 0.0f)
      c.rect(hit.x, hit.y, 3.0f, hit.h, style.selBar, rad);
  }

  // A rounded square with radius == half-size approximates a dot — Canvas has
  // no circle primitive (same technique as Pager::drawDots).
  c.rect(dot.x, dot.y, dot.w, dot.h, selected ? style.dotOn : style.dotOff, dot.w * 0.5f);
  c.text(text, labelX, vcenter(row, drawSize), drawSize,
         selected ? style.textOn : style.textOff);
  return hit;
}

// ── Stepper ──────────────────────────────────────────────────────────────────
StepperGeom stepperGeom(const Rect& row) {
  float bs = row.h * 0.92f;
  float vw = row.h * 2.1f;
  float gap = row.h * 0.12f;
  float clusterW = bs + gap + vw + gap + bs;
  float cx = row.x + row.w - clusterW;
  float y = row.y + (row.h - bs) * 0.5f;
  StepperGeom g;
  g.minus = {cx, y, bs, bs};
  g.value = {cx + bs + gap, y, vw, bs};
  g.plus  = {cx + bs + gap + vw + gap, y, bs, bs};
  return g;
}

void drawStepper(Canvas& c, const Rect& row, std::string_view label,
                 std::string_view valueText, const StepperStyle& style) {
  float s = rowTextSize(row);
  StepperGeom g = stepperGeom(row);
  drawLabelFit(c, label, row, g.minus.x);
  c.button(g.minus.x, g.minus.y, g.minus.w, g.minus.h, "-", style.buttonBg, style.buttonText, g.minus.h * 0.3f);
  c.button(g.plus.x,  g.plus.y,  g.plus.w,  g.plus.h,  "+", style.buttonBg, style.buttonText, g.plus.h * 0.3f);
  c.rect(g.value.x, g.value.y, g.value.w, g.value.h, style.valueBg, g.value.h * 0.2f);
  c.textCentered(valueText, g.value.x + g.value.w * 0.5f, vcenter(g.value, s), s, style.valueText);
}

// ── Slider ───────────────────────────────────────────────────────────────────
namespace {
// Bar horizontal extent: label on the left, value on the right.
void sliderBarX(const Rect& row, float& x0, float& x1) {
  float labelW = row.w * 0.34f;
  float valueW = row.w * 0.16f;
  x0 = row.x + labelW;
  x1 = row.x + row.w - valueW;
}
}  // namespace

SliderGeom sliderGeom(const Rect& row, float t01) {
  float x0, x1; sliderBarX(row, x0, x1);
  t01 = std::clamp(t01, 0.0f, 1.0f);
  float th = row.h * 0.22f;
  SliderGeom g;
  g.bar = {x0, row.y + (row.h - th) * 0.5f, x1 - x0, th};
  float knob = row.h * 0.55f;
  float kx = x0 + t01 * (x1 - x0) - knob * 0.5f;
  g.thumb = {kx, row.y + (row.h - knob) * 0.5f, knob, knob};
  return g;
}

float sliderValueAt(const Rect& row, float px) {
  float x0, x1; sliderBarX(row, x0, x1);
  if (x1 <= x0) return 0.0f;
  return std::clamp((px - x0) / (x1 - x0), 0.0f, 1.0f);
}

void drawSlider(Canvas& c, const Rect& row, float t01,
                std::string_view label, std::string_view valueText,
                const SliderStyle& style) {
  float s = rowTextSize(row);
  SliderGeom g = sliderGeom(row, t01);
  drawLabelFit(c, label, row, g.bar.x);
  c.rect(g.bar.x, g.bar.y, g.bar.w, g.bar.h, style.track, g.bar.h * 0.5f);
  // Filled portion up to the thumb.
  float fillW = g.thumb.x + g.thumb.w * 0.5f - g.bar.x;
  if (fillW > 0.0f) {
    // Drawn as a fast MSDF quad so drag doesn't trigger expensive curve compute
    c.quadMsdfRect(g.bar.x, g.bar.y + g.bar.h * 0.1f, fillW, g.bar.h * 0.8f, style.fill);
  }
  c.quadMsdfRect(g.thumb.x, g.thumb.y, g.thumb.w, g.thumb.h, style.thumb);
  c.textRight(valueText, row.x + row.w, vcenter(row, s), s, style.valueText);
}

// ── Segmented ─────────────────────────────────────────────────────────────────
Rect segmentRectAt(const Rect& row, int count, int i) {
  if (count <= 0) return row;
  float gap = row.h * 0.12f;
  float segW = (row.w - gap * (count - 1)) / count;
  return {row.x + i * (segW + gap), row.y, segW, row.h};
}

std::vector<Rect> segmentRects(const Rect& row, int count) {
  std::vector<Rect> out;
  out.reserve(count > 0 ? count : 0);
  for (int i = 0; i < count; i++) out.push_back(segmentRectAt(row, count, i));
  return out;
}

void drawSegmented(Canvas& c, const Rect& row,
                   const std::string_view* options, int count, int selected,
                   const SegmentedStyle& style) {
  float s = row.h * 0.36f;
  for (int i = 0; i < count; i++) {
    bool sel = i == selected;
    Rect r = segmentRectAt(row, count, i);
    c.rect(r.x, r.y, r.w, r.h, sel ? style.selectedBg : style.unselectedBg, r.h * 0.28f);
    c.textCentered(options[i], r.x + r.w * 0.5f, vcenter(r, s), s,
                   sel ? style.selectedText : style.unselectedText);
  }
}

// ── Dropdown field ────────────────────────────────────────────────────────────
float applyTextFit(Canvas& c, std::string& s, float maxW, float size,
                   const TextFit& fit) {
  float drawSize = size;
  if (fit.shrink)
    drawSize = fitTextSize(c, s, maxW, size, size * fit.minScale,
                           FontStyle::Roman);
  if (fit.ellipsis)
    s = truncateToWidth(c, s, maxW, drawSize, FontStyle::Roman);
  return drawSize;
}

void drawDropdownField(Canvas& c, const Rect& row,
                       std::string_view label, std::string_view value,
                       bool hovered, const TextFit& fit,
                       const DropdownStyle& style) {
  float s = rowTextSize(row);
  float fieldW = row.w * 0.52f;
  Rect f = {row.x + row.w - fieldW, row.y, fieldW, row.h};

  std::string labelS(label);
  float labelSize = applyTextFit(c, labelS, f.x - row.x - labelPad(row), s, fit);
  c.text(labelS, row.x, vcenter(row, labelSize), labelSize, style.labelText);

  c.rect(f.x, f.y, f.w, f.h, hovered ? style.fieldHoverBg : style.fieldBg, f.h * 0.22f);

  // The value's zone ends where the chevron's begins, so with a fitting
  // policy the two can never touch.
  float chevronZone = f.h * 1.0f;
  std::string valueS(value);
  float valueSize = applyTextFit(c, valueS, f.w - f.h * 0.4f - chevronZone, s, fit);
  c.text(valueS, f.x + f.h * 0.4f, vcenter(f, valueSize), valueSize, style.valueText);
  // Disclosure chevron: a real triangle (analytic vector primitive — one
  // 3-line contour in the winding pass), not a glyph.
  float cx = f.x + f.w - f.h * 0.5f;
  float cy = f.y + f.h * 0.5f;
  float aw = f.h * 0.13f;   // half-width
  float ah = f.h * 0.16f;   // total height
  c.triangle(cx - aw, cy - ah * 0.5f,
             cx + aw, cy - ah * 0.5f,
             cx,      cy + ah * 0.5f, style.chevron);
}

// ── ScrollList ────────────────────────────────────────────────────────────────
float listContentHeight(int n, float rowH) { return n * rowH; }

std::vector<ListRow> drawScrollList(Canvas& c, const Rect& area,
                                    const std::vector<std::string>& items,
                                    int selected, float scrollPx, float rowH,
                                    int hoverIndex, const TextFit& fit,
                                    const ScrollListStyle& style) {
  std::vector<ListRow> visible;
  c.rect(area.x, area.y, area.w, area.h, style.background, c.pad());
  const auto savedClip = c.saveClip();
  c.setClip(area.x, area.y, area.w, area.h);
  float s = rowH * 0.42f;
  for (int i = 0; i < (int)items.size(); i++) {
    float ry = area.y + i * rowH - scrollPx;
    if (ry + rowH < area.y || ry > area.y + area.h) continue;  // off-screen
    Rect r = {area.x, ry, area.w, rowH};
    bool sel = (i == selected);
    Color textColor = style.rowText;

    // Measure the row's text first — the Pill fill can hug it (fitWidth).
    float textX = r.x + c.pad();
    std::string item = items[(size_t)i];
    float itemSize = applyTextFit(c, item, r.w - c.pad() * 2.0f, s, fit);
    float textW = c.textWidth(item, itemSize);

    if (style.selection == ListSelectionStyle::Pill) {
      // Full row height (matches the action-button height exactly).
      float px, pw;
      if (style.fitWidth) {                       // hug the text
        px = textX - c.pad() * 0.5f;
        pw = textW + c.pad();
      } else {                                    // full-width row
        px = r.x + c.pad() * 0.3f;
        pw = r.w - c.pad() * 0.6f;
      }
      float rad = std::min(style.radius, rowH * 0.5f);
      if (sel)
        c.rect(px, r.y, pw, rowH, style.pillColor, rad);
      else if (i == hoverIndex)
        c.rect(px, r.y, pw, rowH, style.hoverBg, rad);
      // Left accent bar marks the selected row (nav-style indicator).
      if (sel && style.selectedBar.a > 0.0f)
        c.rect(px, r.y, 3.0f, rowH, style.selectedBar, rad);
      if (sel) textColor = style.pillText;
    } else {  // BottomBorder — flat hover fill + underline on every row
      if (i == hoverIndex)
        c.rect(r.x, r.y, r.w, r.h, style.hoverBg);
      float borderThick = sel ? 2.0f : 1.0f;
      c.rect(r.x, r.y + r.h - borderThick, r.w, borderThick,
             sel ? style.borderSelected : style.borderUnselected);
      if (sel) textColor = style.borderSelected;
    }
    c.text(item, textX, r.y + (rowH - itemSize) * 0.5f, itemSize, textColor);
    visible.push_back({r, i});
  }
  c.restoreClip(savedClip);
  return visible;
}

// ── Sortable table ────────────────────────────────────────────────────────────
namespace {
std::vector<Rect> columnRects(const Rect& row, const std::vector<TableColumn>& columns,
                              const std::vector<float>* widths = nullptr) {
  std::vector<Rect> out;
  out.reserve(columns.size());
  if (widths && widths->size() == columns.size()) {
    float x = row.x;
    for (float w : *widths) {
      out.push_back({x, row.y, w, row.h});
      x += w;
    }
    return out;
  }
  float totalWeight = 0.0f;
  for (auto& col : columns) totalWeight += col.weight;
  if (totalWeight <= 0.0f) totalWeight = 1.0f;
  float x = row.x;
  for (auto& col : columns) {
    float w = row.w * (col.weight / totalWeight);
    out.push_back({x, row.y, w, row.h});
    x += w;
  }
  return out;
}
}  // namespace

Rect tableHeaderRow(const Rect& area, float rowH) { return {area.x, area.y, area.w, rowH}; }

Rect tableHeaderColumnRect(const Rect& headerRow, const std::vector<TableColumn>& columns, int col,
                          const std::vector<float>* widths) {
  auto rects = columnRects(headerRow, columns, widths);
  if (col < 0 || col >= (int)rects.size()) return headerRow;
  return rects[(size_t)col];
}

std::vector<Rect> tableHeaderColumnRects(const Rect& headerRow, const std::vector<TableColumn>& columns,
                                        const std::vector<float>* widths) {
  return columnRects(headerRow, columns, widths);
}

std::vector<TableRow> drawSortableTable(Canvas& c, const Rect& area,
                                        const std::vector<TableColumn>& columns,
                                        const TableCellFn& cellText, int rowCount,
                                        int sortColumn, bool sortAscending,
                                        float scrollPx, float rowH,
                                        int hoverRow, int hoverHeaderCol,
                                        const TableStyle& style,
                                        const TextFit& cellFit,
                                        const std::vector<float>* columnWidthsPx) {
  Rect header = tableHeaderRow(area, rowH);
  auto headerCols = columnRects(header, columns, columnWidthsPx);
  Rect body = {area.x, area.y + rowH, area.w, area.h - rowH};
  float s = rowH * 0.36f;
  float pad = rowH * 0.25f;

  c.rect(header.x, header.y, header.w, header.h, style.headerBg, style.radius);
  for (size_t i = 0; i < headerCols.size(); i++) {
    const Rect& hc = headerCols[i];
    if ((int)i == hoverHeaderCol)
      c.rect(hc.x, hc.y, hc.w, hc.h, style.headerHover, 0.0f);
    // Same fit policy as the body cells (cellFit) so a header label is never
    // a different size than the data beneath it — previously this shrank
    // independently down to a 60%-floor while body cells used a fixed size,
    // which could make a narrow column's header visibly smaller than its data.
    std::string label = columns[i].label;
    float textW = hc.w - pad * 2.0f - ((int)i == sortColumn ? rowH * 0.5f : 0.0f);
    float labelSize = applyTextFit(c, label, textW, s, cellFit);
    c.text(label, hc.x + pad, hc.y + (hc.h - labelSize) * 0.5f, labelSize, style.headerText);

    if ((int)i == sortColumn) {
      // Small triangle: apex up for ascending, down for descending — same
      // "real triangle, not a glyph" technique as the dropdown chevron.
      float cx = hc.x + hc.w - pad - rowH * 0.18f;
      float cy = hc.y + hc.h * 0.5f;
      float aw = rowH * 0.11f, ah = rowH * 0.14f;
      if (sortAscending)
        c.triangle(cx - aw, cy + ah * 0.5f, cx + aw, cy + ah * 0.5f, cx, cy - ah * 0.5f, style.sortGlyph);
      else
        c.triangle(cx - aw, cy - ah * 0.5f, cx + aw, cy - ah * 0.5f, cx, cy + ah * 0.5f, style.sortGlyph);
    }
  }
  // Grid lines between columns — span the full header+body height in
  // fullGrid mode, header-only otherwise (the original look).
  float gridBottom = style.fullGrid ? (body.y + body.h) : (header.y + header.h);
  for (size_t i = 1; i < headerCols.size(); i++)
    c.rect(headerCols[i].x, header.y, 1.0f, gridBottom - header.y, style.gridLine);

  std::vector<TableRow> visible;
  c.rect(body.x, body.y, body.w, body.h, style.rowBg, style.radius);
  const auto savedClip = c.saveClip();
  c.setClip(body.x, body.y, body.w, body.h);
  for (int i = 0; i < rowCount; i++) {
    float ry = body.y + i * rowH - scrollPx;
    if (ry + rowH < body.y || ry > body.y + body.h) continue;  // off-screen
    Rect r = {body.x, ry, body.w, rowH};
    if (i == hoverRow) c.rect(r.x, r.y, r.w, r.h, style.hoverBg, 0.0f);

    auto cells = columnRects(r, columns, columnWidthsPx);
    for (size_t col = 0; col < cells.size(); col++) {
      std::string text = cellText(i, (int)col);
      const Rect& cr = cells[col];
      float maxW = cr.w - pad * 2.0f;
      float cellSize = applyTextFit(c, text, maxW, s, cellFit);
      c.text(text, cr.x + pad, cr.y + (cr.h - cellSize) * 0.5f, cellSize, style.rowText);
    }
    if (style.fullGrid)
      c.rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f, style.gridLine);
    visible.push_back({r, i});
  }
  c.restoreClip(savedClip);
  return visible;
}

void drawFitButton(Canvas& c, const Rect& r, std::string_view label,
                   Color bg, Color fg, float radius,
                   const TextFit& fit, bool allowTwoLines) {
  c.rect(r.x, r.y, r.w, r.h, bg, radius);

  float s = r.h * 0.34f;                 // Canvas::button's label proportion
  float maxW = r.w - r.h * 0.35f;        // side padding
  std::string text(label);

  bool fitsOneLine = c.textWidth(text, s) <= maxW;
  float twoLineH = s * 2.35f;            // two lines + gap
  if (!fitsOneLine && allowTwoLines && r.h >= twoLineH * 1.15f) {
    // Prefer keeping the size and using the vertical slack: shrink a little
    // (to ~85%) only if that alone makes one line fit, else go two lines.
    float slightly = fitTextSize(c, text, maxW, s, s * 0.85f, FontStyle::Roman);
    if (c.textWidthStyled(text, slightly, FontStyle::Roman) <= maxW) {
      float w = c.textWidth(text, slightly);
      c.text(text, r.x + (r.w - w) * 0.5f, vcenter(r, slightly), slightly, fg);
      return;
    }
    std::string l1, l2;
    splitTwoLines(c, text, maxW, s, FontStyle::Roman, l1, l2);
    if (!l2.empty()) {
      float gap = s * 0.25f;
      float top = r.y + (r.h - (s * 2.0f + gap)) * 0.5f;
      float w1 = c.textWidth(l1, s), w2 = c.textWidth(l2, s);
      c.text(l1, r.x + (r.w - w1) * 0.5f, top, s, fg);
      c.text(l2, r.x + (r.w - w2) * 0.5f, top + s + gap, s, fg);
      return;
    }
    text = l1;   // splitTwoLines already fitted/truncated it to one line
  }

  float drawSize = applyTextFit(c, text, maxW, s, fit);
  float w = c.textWidth(text, drawSize);
  c.text(text, r.x + (r.w - w) * 0.5f, vcenter(r, drawSize), drawSize, fg);
}

// ── Group header ──────────────────────────────────────────────────────────────
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title, Color color) {
  float s = row.h * 0.5f;
  c.text(title, row.x, row.y + (row.h - s) * 0.5f, s, color);
}

}  // namespace widgets
