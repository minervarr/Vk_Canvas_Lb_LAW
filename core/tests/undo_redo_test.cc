// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "text_buffer.hh"
#include "undo_redo.hh"

int main() {
    // ── Insert: undo removes it, redo puts it back ─────────────────────────
    {
        TextBuffer b;
        UndoRedo u;
        b.insert(std::string("abc"));
        u.record_insert(0, "abc");
        assert(b.to_string() == "abc");
        assert(u.can_undo());
        assert(!u.can_redo());

        u.undo(b);
        assert(b.to_string().empty());
        assert(b.cursor_pos() == 0);
        assert(!u.can_undo());
        assert(u.can_redo());

        u.redo(b);
        assert(b.to_string() == "abc");
        assert(b.cursor_pos() == 3);
        assert(u.can_undo());
        assert(!u.can_redo());
    }

    // ── Erase: undo restores, redo removes again ───────────────────────────
    {
        TextBuffer b;
        UndoRedo u;
        b.insert(std::string("abc"));
        b.move_cursor_to(1);
        b.erase_after();
        u.record_erase(1, "b");
        assert(b.to_string() == "ac");

        u.undo(b);
        assert(b.to_string() == "abc");
        // Undo restores the erased text and leaves the cursor after it.
        assert(b.cursor_pos() == 2);

        u.redo(b);
        assert(b.to_string() == "ac");
    }

    // ── A new edit clears the redo stack ───────────────────────────────────
    {
        TextBuffer b;
        UndoRedo u;
        b.insert('a');
        u.record_insert(0, "a");
        b.insert('b');
        u.record_insert(1, "b");
        u.undo(b);
        assert(b.to_string() == "a");
        assert(u.can_redo());

        b.insert('c');
        u.record_insert(1, "c");
        assert(!u.can_redo());
        assert(b.to_string() == "ac");
    }

    // ── Coalescing: consecutive single-character inserts become one edit ───
    {
        TextBuffer b;
        UndoRedo u;
        std::string typed;
        for (char ch : std::string("hello")) {
            b.insert(ch);
            if (!u.try_coalesce_insert(typed.size(), ch)) {
                u.record_insert(typed.size(), std::string(1, ch));
            }
            typed.push_back(ch);
        }
        assert(b.to_string() == "hello");
        u.undo(b);
        assert(b.to_string().empty());
        u.redo(b);
        assert(b.to_string() == "hello");
    }

    // ── Coalescing refuses newlines, non-adjacent positions and long runs ─
    {
        UndoRedo u;
        u.record_insert(0, "a");
        assert(!u.try_coalesce_insert(0, 'b'));   // wrong position
        assert(!u.try_coalesce_insert(1, '\n'));  // newline is its own edit
        assert(u.try_coalesce_insert(1, 'b'));

        // Fill the coalesced run to the 64-byte ceiling, then refuse.
        std::string long_run = "ab";
        while (long_run.size() < 64) {
            assert(u.try_coalesce_insert(long_run.size(), 'x'));
            long_run.push_back('x');
        }
        assert(!u.try_coalesce_insert(long_run.size(), 'y'));
    }

    // ── Empty stacks are no-ops ────────────────────────────────────────────
    {
        TextBuffer b;
        UndoRedo u;
        b.insert(std::string("keep"));
        u.undo(b);
        u.redo(b);
        assert(b.to_string() == "keep");
        u.clear();
        assert(!u.can_undo());
        assert(!u.can_redo());
    }

    std::printf("undo_redo_test: all assertions passed\n");
    return 0;
}
