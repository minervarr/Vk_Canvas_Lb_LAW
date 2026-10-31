#include "widgets.hh"

#include "keys.hh"

#include <algorithm>
#include <string>
#include <string_view>

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

}  // namespace widgets
