#include "widgets.hh"

#include "msdf.hh"       // FontStyle
#include "text_util.hh"  // fitTextSize / truncateToWidth

namespace widgets {

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

}  // namespace widgets
