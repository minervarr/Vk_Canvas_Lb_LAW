// Asserts must stay live even though the app builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "utf16_utf8.hh"

namespace {

std::string enc(std::initializer_list<uint16_t> units,
                size_t cursorUnits = 0, size_t* cursorByte = nullptr) {
    std::vector<uint16_t> v(units);
    return utf16::to_utf8(v.data(), v.size(), cursorUnits, cursorByte);
}

} // namespace

int main() {
    // ── ASCII, the case every encoding agrees on ────────────────────────────
    assert(enc({'h', 'i'}) == "hi");
    assert(enc({}) == "");
    assert(utf16::to_utf8(nullptr, 0) == "");

    // ── The three BMP widths ────────────────────────────────────────────────
    assert(enc({0x00E9}) == "\xC3\xA9");                    // e-acute, 2 bytes
    assert(enc({0x0416}) == "\xD0\x96");                    // Cyrillic Zhe
    assert(enc({0x4E00}) == "\xE4\xB8\x80");                // Han "one", 3 bytes
    assert(enc({0xAC00}) == "\xEA\xB0\x80");                // Hangul GA

    // ── Beyond the BMP: the whole reason this file exists ───────────────────
    // U+1F600 is ONE character and must come out as ONE four-byte sequence.
    // JNI's NewStringUTF/GetStringUTFChars would produce six bytes here — two
    // three-byte sequences, one per surrogate — which any standard decoder
    // reads as two broken characters.
    const std::string grin = enc({0xD83D, 0xDE00});
    assert(grin.size() == 4);
    assert(grin == "\xF0\x9F\x98\x80");

    // Round trip: back to UTF-16 it is a surrogate pair again.
    std::vector<uint16_t> back = utf16::to_utf16(grin);
    assert(back.size() == 2);
    assert(back[0] == 0xD83D && back[1] == 0xDE00);

    // ── Unpaired surrogates survive rather than vanishing ───────────────────
    // Already-broken input, but dropping it silently would shift every cursor
    // offset that follows it.
    assert(!enc({0xD83D}).empty());              // a high surrogate with no low
    assert(!enc({0xDE00, 'a'}).empty());         // a low surrogate on its own

    // ── The cursor, in UTF-16 units in and bytes out ────────────────────────
    size_t cb = 999;

    enc({'a', 'b', 'c'}, 0, &cb);   assert(cb == 0);
    enc({'a', 'b', 'c'}, 2, &cb);   assert(cb == 2);
    enc({'a', 'b', 'c'}, 3, &cb);   assert(cb == 3);   // at the end

    // Past the end clamps rather than running off.
    enc({'a', 'b', 'c'}, 99, &cb);  assert(cb == 3);
    enc({}, 5, &cb);                assert(cb == 0);

    // The units-vs-bytes distinction, which is the point: two Han characters
    // are 2 UTF-16 units and 6 UTF-8 bytes.
    enc({0x4E00, 0x4E8C}, 1, &cb);  assert(cb == 3);
    enc({0x4E00, 0x4E8C}, 2, &cb);  assert(cb == 6);

    // And a caret AFTER an astral character: 2 units in, 4 bytes out. Getting
    // this wrong puts the caret in the middle of an emoji.
    enc({0xD83D, 0xDE00, 'x'}, 2, &cb);  assert(cb == 4);
    enc({0xD83D, 0xDE00, 'x'}, 3, &cb);  assert(cb == 5);

    // ── to_utf16 skips malformed bytes instead of inventing characters ──────
    assert(utf16::to_utf16("").empty());
    assert(utf16::to_utf16("\x80").empty());          // stray continuation byte
    assert(utf16::to_utf16("\xE4\xB8").empty());      // truncated 3-byte lead
    assert(utf16::to_utf16("a").size() == 1);

    // Round trip of ordinary multiscript text, the everyday case.
    for (const std::string& s : {std::string("hello"),
                                 std::string("Пикник"),
                                 std::string("周杰倫"),
                                 std::string("아이유")}) {
        std::vector<uint16_t> u = utf16::to_utf16(s);
        assert(utf16::to_utf8(u.data(), u.size()) == s);
    }

    printf("utf16_utf8_test: all assertions passed\n");
    return 0;
}
