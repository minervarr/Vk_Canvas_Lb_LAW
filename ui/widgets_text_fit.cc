#include "widgets.hh"

#include <algorithm>

#include "msdf.hh"       // FontStyle
#include "text_util.hh"  // truncateToWidth
#include "widgets_internal.hh"

namespace widgets {

FittedText fitText(Canvas& c, std::string_view s, std::string& scratch,
                   float maxW, float size, const TextFit& fit,
                   float knownWidth) {
  const float w = knownWidth >= 0.0f
                      ? knownWidth
                      : c.textWidthStyled(s, size, FontStyle::Roman);
  float drawSize = size;

  // Width is linear in size, so the exact fitted size is one division. The
  // old path called fitTextSize() (one walk) and then truncateToWidth()
  // (another walk) even when the first step had already made the text fit.
  if (fit.shrink && maxW > 0.0f && w > maxW && w > 0.0f)
    drawSize = std::max(size * fit.minScale, size * (maxW / w));

  // Measure the shrunken size instead of assuming linearity: RasterFont's
  // advances come from a cell baked at a quantised integer size, so the
  // continuous scale is not exact for it (MSDF is). This only costs a walk in
  // the overflow case; the common no-fit path returns `w`.
  const float drawnW = drawSize == size
                           ? w
                           : c.textWidthStyled(s, drawSize, FontStyle::Roman);
  if (fit.ellipsis && !s.empty() && (maxW <= 0.0f || drawnW > maxW)) {
    scratch = truncateToWidth(c, s, maxW, drawSize, FontStyle::Roman);
    return {scratch, drawSize,
            c.textWidthStyled(scratch, drawSize, FontStyle::Roman)};
  }
  return {s, drawSize, drawnW};
}

float applyTextFit(Canvas& c, std::string& s, float maxW, float size,
                   const TextFit& fit) {
  std::string scratch;
  const FittedText fitted = fitText(c, s, scratch, maxW, size, fit);
  if (fitted.text.data() != s.data()) s.assign(fitted.text);
  return fitted.size;
}

}  // namespace widgets
