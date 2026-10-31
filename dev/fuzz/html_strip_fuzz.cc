// libFuzzer: stripHtmlToPlain is the only byte-level parser in the draw layer
// that consumes untrusted text (HTML sidecar files). It must terminate on any
// input, including unterminated tags, entities and script blocks.
#include "text_util.hh"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string in(reinterpret_cast<const char*>(data), size);
    const std::string out = stripHtmlToPlain(in);

    // Every byte the parser emits comes from the input or a fixed expansion;
    // nothing should grow unboundedly (the entity table only shrinks, tags are
    // dropped, and the lone expansion is "<li>" -> "\n\xC2\xB7 ").
    if (out.size() > in.size() + 2) __builtin_trap();
    return 0;
}
