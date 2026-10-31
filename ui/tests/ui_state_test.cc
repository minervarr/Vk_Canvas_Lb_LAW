// CPU-only UI-state contract. TextFieldState is the toolkit's first piece of
// caller-owned UI state; these assertions pin insertion, deletion, cursor
// movement, selection and IME replacement without a window or a GPU.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "frame_input.hh"
#include "keys.hh"
#include "theme.hh"
#include "widgets.hh"

static void type(FrameInput& in, std::u32string text) {
    in.beginFrame();
    in.typedCodepoints = std::move(text);
}

static void press(FrameInput& in, int key, bool ctrl = false, bool shift = false) {
    in.beginFrame();
    in.ctrlDown = ctrl;
    in.shiftDown = shift;
    in.keysWentDown.push_back(key);
}

int main() {
    widgets::TextFieldState state;

    // Insertion appends at the cursor and advances it on codepoint boundaries.
    {
        FrameInput in;
        type(in, U"abc");
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "abc");
        assert(state.cursorByte == 3);

        type(in, U"\u00e9");   // é, two UTF-8 bytes
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "abc\xc3\xa9");
        assert(state.cursorByte == 5);
    }

    // Backspace removes one whole codepoint, never half a UTF-8 sequence.
    {
        FrameInput in;
        press(in, key::Backspace);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "abc");
        assert(state.cursorByte == 3);
    }

    // Home / Left / Right move the caret; insertion lands in the middle.
    {
        FrameInput in;
        press(in, key::Home);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.cursorByte == 0);

        press(in, key::Right);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.cursorByte == 1);

        type(in, U"X");
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "aXbc");
        assert(state.cursorByte == 2);

        press(in, key::End);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.cursorByte == 4);
    }

    // Ctrl+A selects all; typing replaces the selection.
    {
        FrameInput in;
        press(in, key::A, /*ctrl=*/true);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.selectionAnchor == 0);
        assert(state.cursorByte == state.text.size());

        type(in, U"z");
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "z");
        assert(state.cursorByte == 1);
    }

    // Shift+Left extends the selection; Backspace deletes exactly that run.
    {
        FrameInput in;
        type(in, U"nine");
        assert(widgets::textFieldHandleInput(state, in));

        press(in, key::Left, /*ctrl=*/false, /*shift=*/true);
        assert(widgets::textFieldHandleInput(state, in));
        press(in, key::Left, /*ctrl=*/false, /*shift=*/true);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.selectionAnchor == 5);
        assert(state.cursorByte == 3);

        press(in, key::Backspace);
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "zni");
        assert(state.cursorByte == 3);
        assert(state.selectionAnchor == 3);
    }

    // An input method replaces the buffer wholesale; the delta path is skipped.
    {
        FrameInput in;
        in.beginFrame();
        in.textEdited = true;
        in.editedText = "한";
        in.editedCursorByte = 3;
        assert(widgets::textFieldHandleInput(state, in));
        assert(state.text == "한");
        assert(state.cursorByte == 3);
    }

    // No input means no change, and the function reports that honestly.
    {
        FrameInput in;
        in.beginFrame();
        assert(!widgets::textFieldHandleInput(state, in));
        assert(state.text == "한");
    }

    // Theme skeleton: the engine owns the type ladder, the app supplies the
    // legibility floor; the default palette is data, not a hidden global.
    {
        ui::TypeScale ts = ui::typeScale(1080.0f, 16.0f);
        assert(ts.scale == 1.0f);
        assert(ts.text.caption == 16.0f);
        assert(ts.text.body > ts.text.caption);
        assert(ui::kDarkTheme.colors.accent.b > ui::kDarkTheme.colors.accent.r);
        assert(ui::kDarkTheme.spacing.md > ui::kDarkTheme.spacing.sm);
        assert(ui::kDarkTheme.strokes.thick > ui::kDarkTheme.strokes.hairline);
    }

    std::printf("ui_state_test: OK\n");
    return 0;
}
