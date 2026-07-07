#pragma once
#include <cstdint>

enum class FontWeight : uint8_t {
    Regular    = 0,
    Bold       = 1,
    Italic     = 2,
    BoldItalic = 3,
};
constexpr int kFontWeightCount = 4;
