#include "text_util.hh"
#include <algorithm>
#include <cctype>

std::string truncateToWidth(Canvas& c, const std::string& s, float maxW,
                            float size, FontStyle style) {
    if (c.textWidthStyled(s, size, style) <= maxW) return s;
    const std::string ellipsis = "...";
    size_t lo = 0, hi = s.size();
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        // Snap back off UTF-8 continuation bytes so we never split a codepoint.
        while (mid > 0 && (s[mid] & 0xC0) == 0x80) mid--;
        std::string cand = s.substr(0, mid) + ellipsis;
        if (c.textWidthStyled(cand, size, style) <= maxW) {
            // No narrower split point than lo is reachable (can happen once
            // multi-byte codepoints are involved, e.g. Cyrillic/CJK, where
            // the continuation-byte snap-back above can repeatedly land on
            // the same byte offset) — stop instead of spinning forever.
            if (mid == lo) break;
            lo = mid;
        } else if (mid == 0) {
            // Not even the bare ellipsis fits at the very first codepoint
            // boundary. `hi = mid - 1` would underflow (size_t) and the next
            // iteration's `s[mid]` read would be out-of-bounds.
            break;
        } else {
            hi = mid - 1;
        }
    }
    return lo == 0 ? ellipsis : s.substr(0, lo) + ellipsis;
}

float fitTextSize(Canvas& c, const std::string& s, float maxW,
                  float size, float minSize, FontStyle style) {
    if (maxW <= 0.0f || s.empty()) return size;
    float w = c.textWidthStyled(s, size, style);
    if (w <= maxW) return size;
    // Text width is linear in size, so the exact fit is a single division.
    float fitted = size * (maxW / w);
    return std::max(fitted, minSize);
}

void splitTwoLines(Canvas& c, const std::string& s, float maxW,
                   float size, FontStyle style,
                   std::string& l1, std::string& l2) {
    l2.clear();
    if (c.textWidthStyled(s, size, style) <= maxW) { l1 = s; return; }

    // Longest prefix that fits, preferring a space break.
    size_t fit = 0, lastSpace = std::string::npos;
    for (size_t i = 1; i <= s.size(); i++) {
        if (i < s.size() && (s[i] & 0xC0) == 0x80) continue;  // inside a codepoint
        if (c.textWidthStyled(s.substr(0, i), size, style) > maxW) break;
        fit = i;
        if (i < s.size() && s[i] == ' ') lastSpace = i;
    }
    size_t breakAt = (lastSpace != std::string::npos) ? lastSpace : fit;
    if (breakAt == 0) { l1 = truncateToWidth(c, s, maxW, size, style); return; }
    l1 = s.substr(0, breakAt);
    std::string rest = s.substr(breakAt);
    while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
    l2 = truncateToWidth(c, rest, maxW, size, style);
}

void wrapText(Canvas& c, const std::string& s, float maxW, float size,
              FontStyle style, std::vector<std::string>& out) {
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos
                                                                   : nl - start);
        if (para.empty()) {
            out.push_back("");
        } else {
            size_t pos = 0;
            while (pos < para.size()) {
                std::string rest = para.substr(pos);
                if (c.textWidthStyled(rest, size, style) <= maxW) { out.push_back(rest); break; }
                size_t fit = 0, lastSpace = std::string::npos;
                for (size_t i = 1; i <= rest.size(); i++) {
                    if (i < rest.size() && (rest[i] & 0xC0) == 0x80) continue;
                    if (c.textWidthStyled(rest.substr(0, i), size, style) > maxW) break;
                    fit = i;
                    if (i < rest.size() && rest[i] == ' ') lastSpace = i;
                }
                size_t brk = (lastSpace != std::string::npos) ? lastSpace
                            : std::max<size_t>(fit, 1);
                out.push_back(rest.substr(0, brk));
                pos += brk;
                while (pos < para.size() && para[pos] == ' ') pos++;
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
}

void textCenteredStyled(Canvas& c, const std::string& s, float x, float w,
                        float y, float size, Color color, FontStyle style) {
    float tw = c.textWidthStyled(s, size, style);
    c.textStyled(s, x + std::max(0.0f, (w - tw) * 0.5f), y, size, color, style);
}

std::string stripHtmlToPlain(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    auto appendNewline = [&](int count) {
        int have = 0;
        for (auto it = out.rbegin(); it != out.rend() && *it == '\n'; ++it) have++;
        for (; have < count; have++) out += '\n';
    };
    while (i < in.size()) {
        char ch = in[i];
        if (ch == '\r') { i++; continue; }
        if (ch == '<') {
            size_t end = in.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = in.substr(i + 1, end - i - 1);
            for (auto& tc : tag) tc = (char)tolower((unsigned char)tc);
            // Tag name only: attributes dropped, a closing tag's leading
            // '/' kept (the match table below uses "/p"-style keys).
            size_t scanFrom = (!tag.empty() && tag[0] == '/') ? 1 : 0;
            std::string name = tag.substr(0, tag.find_first_of(" \t", scanFrom));
            if (name == "script" || name == "style") {
                size_t close = in.find("</" + name, end);
                i = (close == std::string::npos) ? in.size() : in.find('>', close) + 1;
                continue;
            }
            if (name == "br" || name == "br/" || name == "/p" || name == "/div" ||
                name == "/h1" || name == "/h2" || name == "/h3" || name == "/h4" ||
                name == "/li" || name == "/tr" || name == "/ul" || name == "/ol")
                appendNewline(name == "/p" ? 2 : 1);
            else if (name == "li")
                { appendNewline(1); out += "\xC2\xB7 "; }  // '·' bullet (Latin-1)
            i = end + 1;
            continue;
        }
        if (ch == '&') {
            size_t semi = in.find(';', i);
            if (semi != std::string::npos && semi - i <= 8) {
                std::string ent = in.substr(i + 1, semi - i - 1);
                const char* rep = nullptr;
                if      (ent == "amp")  rep = "&";
                else if (ent == "lt")   rep = "<";
                else if (ent == "gt")   rep = ">";
                else if (ent == "quot" || ent == "#34") rep = "\"";
                else if (ent == "apos" || ent == "#39") rep = "'";
                else if (ent == "nbsp") rep = " ";
                if (rep) { out += rep; i = semi + 1; continue; }
            }
        }
        out += ch;
        i++;
    }
    // Collapse runs of 3+ newlines to paragraph breaks.
    std::string collapsed;
    collapsed.reserve(out.size());
    int nl = 0;
    for (char ch : out) {
        if (ch == '\n') { if (++nl <= 2) collapsed += ch; }
        else { nl = 0; collapsed += ch; }
    }
    while (!collapsed.empty() && (collapsed.front() == '\n' || collapsed.front() == ' '))
        collapsed.erase(collapsed.begin());
    while (!collapsed.empty() && (collapsed.back() == '\n' || collapsed.back() == ' '))
        collapsed.pop_back();
    return collapsed;
}
