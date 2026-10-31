// libFuzzer: the hand-written UTF-8 decoder must never stall, overrun or
// return without advancing. The decoder is shared with the font engine, so a
// regression here affects every text path.
#include "utf8.hh"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string_view s(reinterpret_cast<const char*>(data), size);
    size_t i = 0;
    while (i < s.size()) {
        const size_t before = i;
        (void)utf8::nextCodepoint(s, i);
        if (i <= before) __builtin_trap();  // must always make progress
        if (i > s.size()) __builtin_trap(); // must never step past the end
    }
    return 0;
}
