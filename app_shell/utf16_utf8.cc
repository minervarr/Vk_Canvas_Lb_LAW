#include "utf16_utf8.hh"

namespace utf16 {

std::string to_utf8(const uint16_t* u, size_t len,
                    size_t cursorUnits, size_t* cursorByte) {
    std::string out;
    // Latin text shrinks, CJK grows by half; 3/2 is the cheap middle that
    // avoids a reallocation for either without over-reserving for both.
    out.reserve(len * 3 / 2);
    if (cursorByte) *cursorByte = 0;
    if (!u) { return out; }

    for (size_t i = 0; i < len; ) {
        // Recorded BEFORE consuming the unit at i, so a cursor sitting between
        // two characters lands between them here too.
        if (cursorByte && i == cursorUnits) *cursorByte = out.size();

        uint32_t cp = u[i++];
        if (cp >= 0xD800 && cp <= 0xDBFF && i < len) {
            uint32_t lo = u[i];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }

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
    }
    // A cursor at or past the end. Also the whole answer for the empty string,
    // and for a cursor that the loop above never reached because it pointed
    // exactly at len.
    if (cursorByte && cursorUnits >= len) *cursorByte = out.size();
    return out;
}

std::vector<uint16_t> to_utf16(const std::string& utf8) {
    std::vector<uint16_t> out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ) {
        unsigned char c = (unsigned char)utf8[i];
        uint32_t cp;
        int n;
        if      (c < 0x80)           { cp = c;         n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; n = 4; }
        else { ++i; continue; }                 // stray continuation byte
        if (i + (size_t)n > utf8.size()) break; // truncated tail
        for (int k = 1; k < n; ++k)
            cp = (cp << 6) | ((unsigned char)utf8[i + (size_t)k] & 0x3Fu);
        i += (size_t)n;

        if (cp < 0x10000) {
            out.push_back((uint16_t)cp);
        } else {
            cp -= 0x10000;
            out.push_back((uint16_t)(0xD800 + (cp >> 10)));
            out.push_back((uint16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

} // namespace utf16
