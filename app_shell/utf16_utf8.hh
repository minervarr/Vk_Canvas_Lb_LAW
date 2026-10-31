#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ── UTF-16 <-> UTF-8, because JNI's own converters are subtly wrong ──────────
//
// Java strings are UTF-16 and this library's are UTF-8, so a text seam across
// JNI has to convert. The obvious calls — GetStringUTFChars and NewStringUTF —
// look like they do exactly this and do not: they speak MODIFIED UTF-8, which
// encodes a character outside the Basic Multilingual Plane as its UTF-16
// surrogate PAIR, each surrogate separately as three bytes. A standard UTF-8
// decoder reads those six bytes as two broken characters.
//
// That is not an exotic case. Any emoji in an album title reaches it, and so
// does a good deal of historic CJK. Hence these.
//
// Kept free of every JNI type (jchar is uint16_t, jsize is int32_t) so the
// tests can compile them on a desktop with no NDK in sight — the property this
// repository's tests are built around.
namespace utf16 {

// UTF-16 to UTF-8.
//
// `cursorUnits` is a caret position measured in UTF-16 CODE UNITS, which is
// what Java's getSelectionEnd() returns; when `cursorByte` is non-null it
// receives the equivalent offset in BYTES into the returned string. Computing
// it during the walk is the only cheap way to get it — the two indices have no
// fixed relationship, and re-deriving it afterwards means decoding twice.
//
// A cursor at or past the end clamps to the end. An unpaired surrogate is
// emitted as-is rather than dropped: it is already broken input, and losing it
// silently would shift every offset after it.
std::string to_utf8(const uint16_t* u, size_t len,
                    size_t cursorUnits = 0, size_t* cursorByte = nullptr);

// UTF-8 to UTF-16. Malformed bytes are skipped rather than substituted — this
// converts strings the app itself produced, so a replacement character would
// only hide a bug upstream.
std::vector<uint16_t> to_utf16(const std::string& utf8);

} // namespace utf16
