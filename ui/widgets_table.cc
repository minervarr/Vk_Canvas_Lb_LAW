#include "widgets.hh"

#include <string>
#include <vector>

#include "widgets_internal.hh"

namespace widgets {

namespace {
bool hasExplicitWidths(const std::vector<TableColumn>& columns,
                       const std::vector<float>* widths) {
  return widths && widths->size() == columns.size();
}

float totalColumnWeight(const std::vector<TableColumn>& columns) {
  float total = 0.0f;
  for (const auto& col : columns) total += col.weight;
  return total > 0.0f ? total : 1.0f;
}

// Width of one column without materialising the whole rect vector. The table
// draw calls this per cell; the old columnRects() allocated a vector for every
// visible row, every frame.
float columnWidth(const Rect& row, const std::vector<TableColumn>& columns,
                  const std::vector<float>* widths, size_t index,
                  bool explicitWidths, float totalWeight) {
  if (explicitWidths) return (*widths)[index];
  return row.w * (columns[index].weight / totalWeight);
}

std::vector<Rect> columnRects(const Rect& row, const std::vector<TableColumn>& columns,
                              const std::vector<float>* widths = nullptr) {
  std::vector<Rect> out;
  out.reserve(columns.size());
  const bool explicitWidths = hasExplicitWidths(columns, widths);
  const float totalWeight = totalColumnWeight(columns);
  float x = row.x;
  for (size_t i = 0; i < columns.size(); ++i) {
    const float w = columnWidth(row, columns, widths, i, explicitWidths, totalWeight);
    out.push_back({x, row.y, w, row.h});
    x += w;
  }
  return out;
}
}  // namespace

Rect tableHeaderRow(const Rect& area, float rowH) { return {area.x, area.y, area.w, rowH}; }

Rect tableHeaderColumnRect(const Rect& headerRow, const std::vector<TableColumn>& columns, int col,
                           const std::vector<float>* widths) {
  if (col < 0 || col >= static_cast<int>(columns.size())) return headerRow;
  const bool explicitWidths = hasExplicitWidths(columns, widths);
  const float totalWeight = totalColumnWeight(columns);
  float x = headerRow.x;
  for (int i = 0; i < col; ++i)
    x += columnWidth(headerRow, columns, widths, static_cast<size_t>(i),
                     explicitWidths, totalWeight);
  return {x, headerRow.y,
          columnWidth(headerRow, columns, widths, static_cast<size_t>(col),
                      explicitWidths, totalWeight),
          headerRow.h};
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
  Rect body = {area.x, area.y + rowH, area.w, area.h - rowH};
  float s = rowH * 0.36f;
  float pad = rowH * 0.25f;
  const bool explicitWidths = hasExplicitWidths(columns, columnWidthsPx);
  const float totalWeight = totalColumnWeight(columns);
  std::string scratch;

  c.rect(header.x, header.y, header.w, header.h, style.headerBg, style.radius);
  float hx = header.x;
  for (size_t i = 0; i < columns.size(); i++) {
    const float cw = columnWidth(header, columns, columnWidthsPx, i,
                                 explicitWidths, totalWeight);
    const Rect hc{hx, header.y, cw, header.h};
    if ((int)i == hoverHeaderCol)
      c.rect(hc.x, hc.y, hc.w, hc.h, style.headerHover, 0.0f);
    // Same fit policy as the body cells (cellFit) so a header label is never
    // a different size than the data beneath it — previously this shrank
    // independently down to a 60%-floor while body cells used a fixed size,
    // which could make a narrow column's header visibly smaller than its data.
    float textW = hc.w - pad * 2.0f - ((int)i == sortColumn ? rowH * 0.5f : 0.0f);
    const FittedText label = fitText(c, columns[i].label, scratch, textW, s, cellFit);
    c.text(label.text, hc.x + pad, hc.y + (hc.h - label.size) * 0.5f,
           label.size, style.headerText);

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
    hx += cw;
  }
  // Grid lines between columns — span the full header+body height in
  // fullGrid mode, header-only otherwise (the original look).
  float gridBottom = style.fullGrid ? (body.y + body.h) : (header.y + header.h);
  hx = header.x;
  for (size_t i = 0; i < columns.size(); i++) {
    hx += columnWidth(header, columns, columnWidthsPx, i,
                      explicitWidths, totalWeight);
    if (i + 1 < columns.size())
      c.rect(hx, header.y, 1.0f, gridBottom - header.y, style.gridLine);
  }

  std::vector<TableRow> visible;
  c.rect(body.x, body.y, body.w, body.h, style.rowBg, style.radius);
  const auto savedClip = c.saveClip();
  c.setClip(body.x, body.y, body.w, body.h);
  for (int i = 0; i < rowCount; i++) {
    float ry = body.y + i * rowH - scrollPx;
    if (ry + rowH < body.y || ry > body.y + body.h) continue;  // off-screen
    Rect r = {body.x, ry, body.w, rowH};
    if (i == hoverRow) c.rect(r.x, r.y, r.w, r.h, style.hoverBg, 0.0f);

    float cx = r.x;
    for (size_t col = 0; col < columns.size(); ++col) {
      const float cw = columnWidth(r, columns, columnWidthsPx, col,
                                   explicitWidths, totalWeight);
      const Rect cr{cx, r.y, cw, r.h};
      std::string text = cellText(i, (int)col);
      float maxW = cr.w - pad * 2.0f;
      const FittedText fitted = fitText(c, text, scratch, maxW, s, cellFit);
      c.text(fitted.text, cr.x + pad,
             cr.y + (cr.h - fitted.size) * 0.5f, fitted.size, style.rowText);
      cx += cw;
    }
    if (style.fullGrid)
      c.rect(r.x, r.y + r.h - 1.0f, r.w, 1.0f, style.gridLine);
    visible.push_back({r, i});
  }
  c.restoreClip(savedClip);
  return visible;
}

}  // namespace widgets
