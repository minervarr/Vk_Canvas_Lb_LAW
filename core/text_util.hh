#pragma once
#include <string>
#include <vector>
#include "canvas.hh"

// Text layout utilities on top of Canvas — measure-based truncation,
// wrapping, and centering that every vk_canvas app ends up needing (first
// proven out in the Matrix Player Windows app, extracted here so the next
// app doesn't rewrite them). All measuring goes through
// Canvas::textWidthStyled(), i.e. the MSDF metrics when an MSDF font is
// attached — unlike Canvas::textCentered(), which measures the curve font
// and therefore mis-centers MSDF-rendered text (kept for compatibility).
//
// All strings are UTF-8; truncation/wrap points never split a multi-byte
// codepoint.

// Longest prefix of `s` that, with a trailing "...", still fits maxW.
// Returns `s` unchanged if it already fits.
std::string truncateToWidth(Canvas& c, const std::string& s, float maxW,
                            float size, FontStyle style);

// Split `s` into up to two lines within maxW: line 1 breaks at the last
// word boundary that fits (falling back to a hard character break for
// unbroken strings), line 2 gets the rest ellipsis-truncated.
void splitTwoLines(Canvas& c, const std::string& s, float maxW,
                   float size, FontStyle style,
                   std::string& l1, std::string& l2);

// Greedy word-wrap of multi-paragraph text into render lines. '\n' splits
// paragraphs; blank entries in `out` mark paragraph gaps. Appends to `out`.
void wrapText(Canvas& c, const std::string& s, float maxW, float size,
              FontStyle style, std::vector<std::string>& out);

// Draw `s` horizontally centered in [x, x+w] at baseline-top y, measured
// with the same styled metrics it renders with.
void textCenteredStyled(Canvas& c, const std::string& s, float x, float w,
                        float y, float size, Color color, FontStyle style);

// Minimal HTML -> plain text: tags dropped (block closers become newlines,
// <li> becomes a "· " bullet), <script>/<style> bodies skipped, common
// entities decoded, newline runs collapsed. Not a general HTML parser —
// enough to render sidecar description/bio files as readable paragraphs.
std::string stripHtmlToPlain(const std::string& in);
