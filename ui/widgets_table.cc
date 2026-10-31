#include "widgets.hh"

#include <string>
#include <vector>

namespace widgets {

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

}  // namespace widgets
