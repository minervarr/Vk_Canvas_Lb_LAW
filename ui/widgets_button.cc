#include "widgets.hh"

#include "msdf.hh"       // FontStyle
#include "text_util.hh"  // splitTwoLines
#include "widgets_internal.hh"

#include <string>

namespace widgets {
using detail::vcenter;

void drawFitButton(Canvas& c, const Rect& r, std::string_view label,
                   Color bg, Color fg, float radius,
                   const TextFit& fit, bool allowTwoLines) {
  c.rect(r.x, r.y, r.w, r.h, bg, radius);

  float s = r.h * 0.34f;                 // Canvas::button's label proportion
  float maxW = r.w - r.h * 0.35f;        // side padding
  float measured = c.textWidth(label, s);
  bool fitsOneLine = measured <= maxW;
  float twoLineH = s * 2.35f;            // two lines + gap
  std::string scratch;
  std::string_view text = label;

  if (!fitsOneLine && allowTwoLines && r.h >= twoLineH * 1.15f) {
    // Prefer keeping the size and using the vertical slack: shrink a little
    // (to ~85%) only if that alone makes one line fit, else go two lines.
    float slightly = measured > 0.0f
                         ? std::max(s * 0.85f, s * (maxW / measured))
                         : s;
    if (c.textWidthStyled(label, slightly, FontStyle::Roman) <= maxW) {
      float w = c.textWidth(label, slightly);
      c.text(label, r.x + (r.w - w) * 0.5f, vcenter(r, slightly), slightly, fg);
      return;
    }
    std::string l1, l2;
    splitTwoLines(c, label, maxW, s, FontStyle::Roman, l1, l2);
    if (!l2.empty()) {
      float gap = s * 0.25f;
      float top = r.y + (r.h - (s * 2.0f + gap)) * 0.5f;
      float w1 = c.textWidth(l1, s), w2 = c.textWidth(l2, s);
      c.text(l1, r.x + (r.w - w1) * 0.5f, top, s, fg);
      c.text(l2, r.x + (r.w - w2) * 0.5f, top + s + gap, s, fg);
      return;
    }
    text = l1;   // splitTwoLines already fitted/truncated it to one line
    measured = c.textWidth(text, s);
  }

  const FittedText fitted = fitText(c, text, scratch, maxW, s, fit, measured);
  c.text(fitted.text, r.x + (r.w - fitted.width) * 0.5f,
         vcenter(r, fitted.size), fitted.size, fg);
}

}  // namespace widgets
