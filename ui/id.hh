#pragma once
#include <cstdint>
#include <string_view>

// Stable widget identity for the toolkit state store.
//
// Use a literal at the call site (`"settings.wifi"_id`) or an app-stable
// integer. Never derive an Id from an address: pointer values change between
// runs and would silently reset focus/scroll state.
namespace ui {

using Id = std::uint64_t;

constexpr Id idFrom(std::string_view text) {
    // FNV-1a 64-bit: small, constexpr, and stable across platforms.
    std::uint64_t hash = 14695981039346656037ull;
    for (char ch : text) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

constexpr Id idFrom(const char* text) { return idFrom(std::string_view(text)); }

namespace literals {
constexpr Id operator""_id(const char* text, std::size_t size) {
    return idFrom(std::string_view(text, size));
}
}  // namespace literals

}  // namespace ui
