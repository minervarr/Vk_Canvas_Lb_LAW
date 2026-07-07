#include "textarea.hh"

#include <algorithm>
#include <utility>

#include "font.hh"
#include "utf8.hh"

void TextArea::setRect(float x, float y, float w, float h) {
  if (w != rect_.w) layoutDirty_ = true;   // width drives wrapping
  rect_ = {x, y, w, h};
}

void TextArea::setText(std::string s) {
  if (s == text_) return;                  // no change → keep cached layout
  text_ = std::move(s);
  layoutDirty_ = true;
}

void TextArea::scrollBy(float dyPixels) {
  pinBottom_ = false;
  scroll_ += dyPixels;
  if (scroll_ < 0.0f) scroll_ = 0.0f;
  if (scroll_ > lastMaxScroll_) scroll_ = lastMaxScroll_;
}

void TextArea::scrollToBottom() { pinBottom_ = true; }
void TextArea::scrollToTop()    { pinBottom_ = false; scroll_ = 0.0f; }

// Greedy word wrap. Each word is measured once (O(words)); a word wider than
void TextArea::relayout_(const Canvas& c, float size, float maxW) {
  lines_.clear();
  layoutW_ = maxW;
  layoutSize_ = size;
  layoutDirty_ = false;
  if (text_.empty()) return;

  const float spaceW = c.textWidth(" ", size);

  size_t i = 0;
  while (i <= text_.size()) {
    size_t nl = text_.find('\n', i);
    size_t end = (nl == std::string::npos) ? text_.size() : nl;

    size_t lineStart = i;
    float lineW = 0.0f;
    size_t w = i;
    while (w < end) {
      size_t sp = text_.find(' ', w);
      if (sp == std::string::npos || sp > end) sp = end;
      std::string_view word(text_.data() + w, sp - w);
      float wordW = c.textWidth(word, size);

      if (lineStart < w && lineW + spaceW + wordW > maxW) {
        lines_.emplace_back(text_.data() + lineStart, w - 1 - lineStart);
        lineStart = w;
        lineW = 0.0f;
      }

      if (wordW > maxW) {
        // Word too long for any line — break it by characters.
        if (lineStart < w) {
            lines_.emplace_back(text_.data() + lineStart, w - 1 - lineStart);
            lineStart = w;
            lineW = 0.0f;
        }
        size_t chunkStart = w;
        float chunkW = 0.0f;
        std::string_view wordSv(text_.data() + w, sp - w);
        size_t rel = 0;  // codepoint-aligned offset within wordSv
        while (rel < wordSv.size()) {
          size_t prev = rel;
          utf8::nextCodepoint(wordSv, rel);   // advance one whole codepoint
          size_t ch = w + prev;
          std::string_view single(text_.data() + ch, rel - prev);
          float cw = c.textWidth(single, size);
          if (chunkStart < ch && chunkW + cw > maxW) {
            lines_.emplace_back(text_.data() + chunkStart, ch - chunkStart);
            chunkStart = ch;
            chunkW = 0.0f;
          }
          chunkW += cw;
        }
        lineStart = chunkStart;
        lineW = chunkW;
      } else {
        if (lineStart < w) {
          lineW += spaceW;
        }
        lineW += wordW;
      }
      w = sp + 1;
    }
    
    if (lineStart < end || (lineStart == end && i == end)) {
        lines_.emplace_back(text_.data() + lineStart, end - lineStart);
    }

    if (nl == std::string::npos) break;
    i = nl + 1;
  }
}

void TextArea::emitStatic(Canvas& c, const Font* font) {
  const float pad = (pad_ >= 0.0f) ? pad_ : c.pad();
  lastPad_ = pad;

  // Background panel with rounded corners
  c.rect(rect_.x, rect_.y, rect_.w, rect_.h, bg_, c.w() * 0.04f);
  if (text_.empty()) { lines_.clear(); contentH_ = 0.0f; return; }

  const float size  = (textSize_ > 0.0f) ? textSize_ : rect_.h * 0.06f;
  const float lineH = size * 1.4f;
  const float maxW  = rect_.w - 2.0f * pad;
  if (layoutDirty_ || maxW != layoutW_ || size != layoutSize_)
    relayout_(c, size, maxW);

  contentH_ = float(lines_.size()) * lineH;

  // Emit every line at its content position. No clip: the GPU scissor + scroll
  // offset handle visibility, so this geometry stays static while scrolling.
  const float x0  = rect_.x + pad;
  const float top = rect_.y + pad;
  for (size_t i = 0; i < lines_.size(); i++)
    c.text(lines_[i], x0, top + float(i) * lineH, size, fg_);
}

void TextArea::render(Canvas& c, const Font* font) {
  const float pad = (pad_ >= 0.0f) ? pad_ : c.pad();
  lastPad_ = pad;

  // Background panel with rounded corners
  c.rect(rect_.x, rect_.y, rect_.w, rect_.h, bg_, c.w() * 0.04f);
  if (text_.empty()) {
    lines_.clear();
    lastMaxScroll_ = 0.0f;
    return;
  }

  const float size  = (textSize_ > 0.0f) ? textSize_ : rect_.h * 0.06f;
  const float lineH = size * 1.4f;
  const float maxW  = rect_.w - 2.0f * pad;

  if (layoutDirty_ || maxW != layoutW_ || size != layoutSize_)
    relayout_(c, size, maxW);

  const float viewH    = rect_.h - 2.0f * pad;
  const float contentH = float(lines_.size()) * lineH;
  contentH_ = contentH;
  lastMaxScroll_ = std::max(0.0f, contentH - viewH);

  if (pinBottom_) scroll_ = lastMaxScroll_;
  scroll_ = std::max(0.0f, std::min(scroll_, lastMaxScroll_));

  c.setClip(rect_.x, rect_.y, rect_.w, rect_.h);

  const float x0   = rect_.x + pad;
  const float top  = rect_.y + pad;
  const float botY = rect_.y + rect_.h - pad;
  // Draw only the visible line range instead of iterating the whole document.
  int first = std::max(0, int((scroll_ - lineH) / lineH));
  int last  = std::min((int)lines_.size(),
                       int((scroll_ + (botY - top) + lineH) / lineH) + 1);
  for (int i = first; i < last; i++) {
    float ly = top - scroll_ + float(i) * lineH;
    c.text(lines_[i], x0, ly, size, fg_);
  }

  c.clearClip();

  // Scrollbar thumb on the right edge when content overflows.
  if (lastMaxScroll_ > 0.5f) {
    const float trackW = std::max(2.0f, rect_.w * 0.008f);
    const float trackX = rect_.x + rect_.w - trackW - pad * 0.25f;
    const float trackY = rect_.y + pad;
    const float trackH = rect_.h - 2.0f * pad;
    const float thumbH = std::max(trackH * 0.1f, trackH * viewH / contentH_);
    float thumbY = trackY + trackH * (scroll_ / contentH_);

    c.rect(trackX, thumbY, trackW, thumbH, col::dim, trackW * 0.5f); // Pill shape scroll thumb
  }
}
