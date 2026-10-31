#include "widgets.hh"

namespace widgets {

// ── Group header ──────────────────────────────────────────────────────────────
void drawGroupHeader(Canvas& c, const Rect& row, std::string_view title, Color color) {
  float s = row.h * 0.5f;
  c.text(title, row.x, row.y + (row.h - s) * 0.5f, s, color);
}

}  // namespace widgets
