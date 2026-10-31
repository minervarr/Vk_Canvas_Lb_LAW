// CPU-only context/widget contract. Every widget is driven through simulated
// FrameInput frames and asserted on the StateStore, not on pixels: hover,
// press/release, focus, Tab/Shift+Tab, Enter/Space, slider drag and text entry.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "context.hh"
#include "id.hh"
#include "keys.hh"
#include "state.hh"
#include "theme.hh"
#include "widgets_context.hh"

namespace {

using namespace ui::literals;

struct Harness {
    std::vector<float> curves;
    Canvas canvas{curves, 800, 600, nullptr, 0, 0, 0, 0};
    FrameInput input;
    ui::StateStore store;
    ui::Theme theme = ui::kDarkTheme;
    ui::TypeScale type = ui::typeScale(1080.0f, 16.0f);
};

bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

}  // namespace

int main() {
    // ── Toggle: hover, press/release, focus follows the click ──────────────
    {
        Harness h;
        Rect row{10, 10, 300, 40};
        bool on = false;
        constexpr ui::Id id = "test.toggle"_id;

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(!ui::toggle(c, id, row, "Wi-Fi", on));
            assert(c.hot(id));
            assert(c.active(id));
            assert(c.focused(id));
        }
        assert(!on);

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerWentUp = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::toggle(c, id, row, "Wi-Fi", on));
        }
        assert(on);
        assert(h.store.active() == 0);
        assert(h.store.widget(id).focused);
        assert(!h.curves.empty());
    }

    // ── Keyboard: Tab focuses, Enter activates ─────────────────────────────
    {
        Harness h;
        Rect row{10, 10, 300, 40};
        bool on = false;
        constexpr ui::Id id = "test.keyboard"_id;

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Tab);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::toggle(c, id, row, "Wi-Fi", on);
        }
        assert(h.store.focused() == id);

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Enter);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::toggle(c, id, row, "Wi-Fi", on));
        }
        assert(on);
    }

    // ── Radio: two rows, one selected value ────────────────────────────────
    {
        Harness h;
        Rect first{10, 10, 300, 40};
        Rect second{10, 60, 300, 40};
        int selected = 0;
        constexpr ui::Id a = "test.radio.a"_id;
        constexpr ui::Id b = "test.radio.b"_id;

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 70;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(!ui::radio(c, a, first, "A", selected, 0));
            assert(!ui::radio(c, b, second, "B", selected, 1));
        }
        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 70;
        h.input.pointerWentUp = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::radio(c, a, first, "A", selected, 0);
            assert(ui::radio(c, b, second, "B", selected, 1));
        }
        assert(selected == 1);
    }

    // ── Slider: press jumps, drag follows, release clears the drag ─────────
    {
        Harness h;
        Rect row{0, 0, 300, 40};
        constexpr ui::Id id = "test.slider"_id;
        const float x0 = row.w * 0.34f;
        const float x1 = row.w * 0.84f;
        float value = 0.0f;

        h.input.beginFrame();
        h.input.pointerX = x0 + (x1 - x0) * 0.75f;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::slider(c, id, row, "Volume", value));
            assert(near(value, 0.75f));
        }

        h.input.beginFrame();
        h.input.pointerX = x0 + (x1 - x0) * 0.25f;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::slider(c, id, row, "Volume", value));
            assert(near(value, 0.25f));
        }

        h.input.beginFrame();
        h.input.pointerWentUp = true;
        h.input.pointerX = x0 + (x1 - x0) * 0.25f;
        h.input.pointerY = 20;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::slider(c, id, row, "Volume", value);
        }
        assert(h.store.active() == 0);
        assert(near(value, 0.25f));

        // Keyboard adjustment while focused (press focused the slider).
        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Right);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::slider(c, id, row, "Volume", value));
        }
        assert(near(value, 0.30f));
    }

    // ── Button: click and keyboard activation ──────────────────────────────
    {
        Harness h;
        Rect r{10, 10, 120, 40};
        constexpr ui::Id id = "test.button"_id;

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(!ui::button(c, id, r, "Apply"));
        }
        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerWentUp = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::button(c, id, r, "Apply"));
        }

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Space);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::button(c, id, r, "Apply"));
        }
    }

    // ── Text field: click focuses, typing edits the store, app string follows ─
    {
        Harness h;
        Rect row{10, 10, 300, 40};
        constexpr ui::Id id = "test.text"_id;
        std::string text = "x";

        h.input.beginFrame();
        h.input.pointerX = row.x + row.w - 5;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::textField(c, id, row, text, "Name");
        }
        assert(h.store.focused() == id);

        h.input.beginFrame();
        h.input.pointerX = row.x + row.w - 5;
        h.input.pointerY = 20;
        h.input.pointerWentUp = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::textField(c, id, row, text, "Name");
        }

        h.input.beginFrame();
        h.input.typedCodepoints = U"a";
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::textField(c, id, row, text, "Name"));
        }
        assert(text == "xa");
        assert(h.store.textSlot(id).state.text == "xa");

        // A press on empty space blurs the focused field.
        h.input.beginFrame();
        h.input.pointerX = 700;
        h.input.pointerY = 500;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::textField(c, id, row, text, "Name");
        }
        assert(h.store.focused() == 0);
    }

    // ── Scroll list: toolkit owns scroll; click selects a visible row ──────
    {
        Harness h;
        Rect area{0, 0, 200, 40};
        std::vector<std::string> items{"one", "two", "three"};
        constexpr ui::Id id = "test.list"_id;
        int selected = 0;

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.wheelDelta = -2.0f;   // two rows down
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::scrollList(c, id, area, items, selected, 40.0f);
        }
        assert(h.store.widget(id).scrollPx == 80.0f);

        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerDown = true;
        h.input.pointerWentDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::scrollList(c, id, area, items, selected, 40.0f);
        }
        h.input.beginFrame();
        h.input.pointerX = 20;
        h.input.pointerY = 20;
        h.input.pointerWentUp = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            assert(ui::scrollList(c, id, area, items, selected, 40.0f) == 2);
        }
        assert(selected == 2);

        // Keyboard selection keeps the row visible.
        h.store.widget(id).scrollPx = 0.0f;
        selected = 0;
        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Tab);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::scrollList(c, id, area, items, selected, 40.0f);
        }
        assert(h.store.focused() == id);
        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Down);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::scrollList(c, id, area, items, selected, 40.0f);
        }
        assert(selected == 1);
        assert(h.store.widget(id).scrollPx == 40.0f);
    }

    // ── Shift+Tab wraps backwards through the frame order ──────────────────
    {
        Harness h;
        Rect row{0, 0, 100, 20};
        bool a = false, b = false;
        constexpr ui::Id ida = "test.order.a"_id;
        constexpr ui::Id idb = "test.order.b"_id;

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Tab);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::toggle(c, ida, row, "A", a);
            ui::toggle(c, idb, row, "B", b);
        }
        assert(h.store.focused() == ida);

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Tab);
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::toggle(c, ida, row, "A", a);
            ui::toggle(c, idb, row, "B", b);
        }
        assert(h.store.focused() == idb);

        h.input.beginFrame();
        h.input.keysWentDown.push_back(key::Tab);
        h.input.shiftDown = true;
        {
            ui::Context c(h.canvas, h.input, h.store, h.theme, h.type);
            ui::toggle(c, ida, row, "A", a);
            ui::toggle(c, idb, row, "B", b);
        }
        assert(h.store.focused() == ida);
    }

    std::printf("context_widgets_test: OK\n");
    return 0;
}
