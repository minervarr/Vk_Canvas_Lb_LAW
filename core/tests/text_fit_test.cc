// Text-fit correctness plus the algorithmic guard for wrapText(): a fixed
// 5px-per-codepoint font makes every expected break deterministic, and the
// counting allocator catches a return to the old per-prefix measurement
// (O(n²) allocations) without timing anything.
#undef NDEBUG
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "canvas.hh"
#include "text_font.hh"
#include "text_util.hh"

namespace {
std::atomic<size_t> g_allocations{0};
void* countedAlloc(std::size_t n) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
}  // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
// Every codepoint advances exactly size/2 px; no kerning.
struct FixedFont : TextFont {
    float layout(uint32_t, float pen, float, float size, GlyphQuad& q,
                 uint32_t) const override {
        q.draw = false;
        return pen + 0.5f * size;
    }
    float layoutByKey(uint32_t, float pen, float, float size, GlyphQuad& q) const override {
        q.draw = false;
        return pen + 0.5f * size;
    }
    float advance(uint32_t, float size) const override { return 0.5f * size; }
    float advanceKey(uint32_t, float size) const override { return 0.5f * size; }
    float textWidth(std::string_view s, float size) const override {
        return float(s.size()) * 0.5f * size;
    }
    float lineHeight(float size) const override { return 1.2f * size; }
    float ascender(float size) const override { return 0.8f * size; }
    uint32_t keyForStyle(FontStyle, uint32_t cp) const override { return cp; }
    float kernEmStyled(FontStyle, uint32_t, uint32_t) const override { return 0.0f; }
    bool hasCodepoint(uint32_t) const override { return true; }
    bool valid() const override { return true; }
    const std::vector<uint8_t>& atlas() const override {
        static const std::vector<uint8_t> empty;
        return empty;
    }
    uint32_t atlasW() const override { return 1; }
    uint32_t atlasH() const override { return 1; }
    uint32_t atlasChannels() const override { return 1; }
    TextMode textMode() const override { return TextMode::Raster; }
    float distanceRange() const override { return 0.0f; }
    bool isMtsdf() const override { return false; }
};
}  // namespace

int main() {
    std::vector<float> curves, quads;
    FixedFont font;
    Canvas c(curves, 1920, 1080, nullptr, 0, 0, 0, 0);
    c.useMsdf(&font, &quads);

    // 5 px per byte at size 10, so maxW 55 px fits 11 characters.
    std::vector<std::string> lines;
    lines.reserve(512);
    wrapText(c, "hello world again", 55.0f, 10.0f, FontStyle::Roman, lines);
    assert(lines.size() == 2);
    assert(lines[0] == "hello");
    assert(lines[1] == "world again");

    lines.clear();
    wrapText(c, "a\n\nb", 55.0f, 10.0f, FontStyle::Roman, lines);
    assert(lines.size() == 3);
    assert(lines[0] == "a" && lines[1].empty() && lines[2] == "b");

    assert(truncateToWidth(c, "abcdefg", 30.0f, 10.0f, FontStyle::Roman) == "abc...");

    std::string l1, l2;
    splitTwoLines(c, "hello world again", 55.0f, 10.0f, FontStyle::Roman, l1, l2);
    assert(l1 == "hello");
    assert(l2 == "world again");

    // 4400 bytes at 5 px each, 360 px per line. The old implementation
    // allocated once per measured prefix; the single-pass version allocates
    // only the output lines.
    std::string longLine;
    for (int i = 0; i < 100; ++i)
        longLine += "the quick brown fox jumps over the lazy dog ";
    g_allocations.store(0, std::memory_order_relaxed);
    lines.clear();
    wrapText(c, longLine, 360.0f, 10.0f, FontStyle::Roman, lines);
    const size_t allocs = g_allocations.load(std::memory_order_relaxed);
    std::printf("wrapText: %zu lines, %zu allocations\n", lines.size(), allocs);
    assert(!lines.empty());
    assert(allocs <= lines.size() + 4);
    return 0;
}
