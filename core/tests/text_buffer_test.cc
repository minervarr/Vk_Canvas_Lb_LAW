// Asserts must stay live even though the library builds Release (NDEBUG).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>

#include "text_buffer.hh"

int main() {
    // ── Empty buffer ────────────────────────────────────────────────────────
    {
        TextBuffer b;
        assert(b.length() == 0);
        assert(b.cursor_pos() == 0);
        assert(b.to_string().empty());
        assert(b.line_count() == 1);
        assert(b.cursor_line() == 0);
        assert(b.cursor_col() == 0);
        assert(b.line_start(0) == 0);
        assert(b.line_end(0) == 0);
        assert(b.line_view(0).empty());
        b.erase_before();   // no-op at the start
        b.erase_after();    // no-op at the end
        assert(b.length() == 0);
    }

    // ── Insert / cursor movement / erase before and after ───────────────────
    {
        TextBuffer b;
        b.insert('h');
        b.insert('e');
        b.insert('l');
        b.insert('l');
        b.insert('o');
        assert(b.length() == 5);
        assert(b.to_string() == "hello");
        assert(b.at(0) == 'h' && b.at(4) == 'o');
        assert(b.cursor_pos() == 5);

        b.move_cursor_to(2);
        assert(b.cursor_pos() == 2);
        b.insert('X');
        assert(b.to_string() == "heXllo");
        assert(b.cursor_pos() == 3);

        b.erase_before();
        assert(b.to_string() == "hello");
        assert(b.cursor_pos() == 2);

        b.erase_after();
        assert(b.to_string() == "helo");
        assert(b.cursor_pos() == 2);

        b.move_cursor(-100);
        assert(b.cursor_pos() == 0);
        b.move_cursor(100);
        assert(b.cursor_pos() == b.length());
    }

    // ── Multi-line indexing and line views ─────────────────────────────────
    {
        TextBuffer b;
        b.insert(std::string("ab\ncd\nef"));
        assert(b.length() == 8);
        assert(b.to_string() == "ab\ncd\nef");
        assert(b.line_count() == 3);

        assert(b.line_start(0) == 0);
        assert(b.line_end(0) == 3);   // includes the newline
        assert(b.line_start(1) == 3);
        assert(b.line_end(1) == 6);
        assert(b.line_start(2) == 6);
        assert(b.line_end(2) == 8);

        assert(b.line_view(0) == std::string_view("ab"));
        assert(b.line_view(1) == std::string_view("cd"));
        assert(b.line_view(2) == std::string_view("ef"));
        assert(b.line_view(-1).empty());
        assert(b.line_view(99).empty());

        // Moving the cursor into line 1 makes it span the gap; the zero-copy
        // view of that line is deliberately unavailable, while the other
        // lines still are.
        b.move_cursor_to(4);
        assert(b.line_view(0) == std::string_view("ab"));
        assert(b.line_view(1).empty());
        assert(b.line_view(2) == std::string_view("ef"));
        assert(b.cursor_line() == 1);
        assert(b.cursor_col() == 1);
    }

    // ── move_to_line_col clamps to the line and never lands past a newline ─
    {
        TextBuffer b;
        b.insert(std::string("ab\ncd\nef"));

        b.move_to_line_col(1, 1);
        assert(b.cursor_pos() == 4);
        assert(b.cursor_line() == 1);
        assert(b.cursor_col() == 1);

        b.move_to_line_col(0, 99);
        assert(b.cursor_pos() == 2);   // clamped before the '\n'
        assert(b.cursor_line() == 0);
        assert(b.cursor_col() == 2);

        b.move_to_line_col(2, 99);
        assert(b.cursor_pos() == 8);
        assert(b.cursor_line() == 2);
        assert(b.cursor_col() == 2);

        b.move_to_line_col(99, 0);     // clamped to the last line
        assert(b.cursor_line() == 2);
    }

    // ── Gap growth: an insertion larger than the initial gap ───────────────
    {
        TextBuffer b(8);
        std::string big(5000, 'z');
        b.insert(big);
        assert(b.length() == big.size());
        assert(b.to_string() == big);
        b.move_cursor_to(2500);
        assert(b.cursor_col() == 2500);
        b.insert('!');
        assert(b.length() == big.size() + 1);
        assert(b.at(2500) == '!');
    }

    // ── Dirty/generation bookkeeping ────────────────────────────────────────
    {
        TextBuffer b;
        b.clear_dirty();
        const size_t gen = b.generation();
        b.insert('a');
        assert(b.dirty());
        assert(b.generation() == gen + 1);
        b.clear_dirty();
        assert(!b.dirty());
        b.erase_before();
        assert(b.dirty());
        assert(b.generation() == gen + 2);
    }

    std::printf("text_buffer_test: all assertions passed\n");
    return 0;
}
