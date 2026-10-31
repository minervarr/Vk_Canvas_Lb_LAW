// libFuzzer: the JNI text seam converts between Java's UTF-16 and the app's
// UTF-8. Both directions must terminate on any input, keep the cursor offset
// inside the returned string, and never emit more than 3 bytes per UTF-16 unit.
#include "utf16_utf8.hh"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Interpret the input as UTF-16 code units (little-endian), including
    // unpaired surrogates, which the converter deliberately passes through.
    const size_t units = size / 2;
    std::vector<uint16_t> u(units);
    for (size_t i = 0; i < units; ++i)
        u[i] = static_cast<uint16_t>(data[2 * i] | (data[2 * i + 1] << 8));

    const size_t cursors[] = { 0, units / 2, units, units + 3 };
    for (size_t cursor : cursors) {
        size_t cursor_byte = static_cast<size_t>(-1);
        const std::string out = utf16::to_utf8(u.data(), units, cursor, &cursor_byte);
        if (cursor_byte > out.size()) __builtin_trap();
        if (out.size() > units * 3) __builtin_trap();
        // Round trip through to_utf16 and back; must not crash or grow.
        const std::vector<uint16_t> back = utf16::to_utf16(out);
        const std::string again = utf16::to_utf8(back.data(), back.size());
        if (again.size() > out.size() + 3) __builtin_trap();
    }

    // The null/empty contract.
    if (!utf16::to_utf8(nullptr, 0).empty()) __builtin_trap();

    // Arbitrary bytes in the other direction: malformed sequences are skipped,
    // never read past the end.
    const std::string bytes(reinterpret_cast<const char*>(data), size);
    const std::vector<uint16_t> converted = utf16::to_utf16(bytes);
    if (converted.size() > bytes.size()) __builtin_trap();  // one unit per byte at most
    (void)utf16::to_utf8(converted.data(), converted.size());
    return 0;
}
