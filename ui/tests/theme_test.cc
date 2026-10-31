// CPU-only theme and type-role contract: presets, token overrides, density and
// the role fit policy. No Canvas, no GPU, no window.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "theme.hh"
#include "type.hh"
#include "widgets.hh"

int main() {
    using namespace ui;

    // Dark and light are data, and they are actually different.
    assert(kDarkTheme.colors.background.r < 0.1f);
    assert(kLightTheme.colors.background.r > 0.9f);
    assert(kLightTheme.colors.primary.r < 0.2f);
    assert(kDarkTheme.colors.primary.r > 0.9f);
    assert(kDarkTheme.colors.accent.b > kDarkTheme.colors.accent.r);

    // A caller overrides one token by copying the theme; nothing else moves.
    {
        Theme custom = kDarkTheme;
        custom.colors.accent = {1.0f, 0.0f, 0.5f, 1.0f};
        custom.radius = 12.0f;
        assert(custom.colors.accent.r == 1.0f);
        assert(custom.colors.background.r == kDarkTheme.colors.background.r);
        assert(custom.radius == 12.0f);
        assert(kDarkTheme.radius == 8.0f);
    }

    // Density is a spacing multiplier, not a separate set of magic numbers.
    {
        Theme compact = kDarkTheme.withDensity(0.85f);
        Theme comfortable = kDarkTheme.withDensity(1.25f);
        assert(compact.density == 0.85f);
        assert(compact.scaledSpacing().unit < kDarkTheme.scaledSpacing().unit);
        assert(comfortable.scaledSpacing().unit > kDarkTheme.scaledSpacing().unit);
        assert(comfortable.scaledSpacing().md == comfortable.scaledSpacing().unit * 3.0f);
    }

    // One type ladder, five roles; size and overflow policy are separate facts.
    {
        TypeScale ts = typeScale(1080.0f, 16.0f);
        assert(typeSize(ts, TypeRole::Caption) == 16.0f);
        assert(typeSize(ts, TypeRole::Body) > typeSize(ts, TypeRole::Caption));
        assert(typeSize(ts, TypeRole::Header) > typeSize(ts, TypeRole::Title));

        assert(typeStyle(ts, TypeRole::Caption).fit.ellipsis);
        assert(!typeStyle(ts, TypeRole::Body).fit.ellipsis);
        assert(typeStyle(ts, TypeRole::Title).fit.shrink);

        assert(typeColor(kDarkTheme, TypeRole::Header).b == kDarkTheme.colors.accent.b);
        assert(typeColor(kDarkTheme, TypeRole::Caption).r == kDarkTheme.colors.secondary.r);
        assert(typeColor(kDarkTheme, TypeRole::Body).r == kDarkTheme.colors.primary.r);
    }

    // The legibility floor still pins the smallest role at any screen size.
    {
        TypeScale small = typeScale(480.0f, 18.0f);
        assert(typeSize(small, TypeRole::Caption) == 18.0f);
        assert(typeSize(small, TypeRole::Header) > typeSize(small, TypeRole::Caption));
    }

    std::printf("theme_test: OK\n");
    return 0;
}
