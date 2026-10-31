// libFuzzer: drive the gap-buffer text editor through arbitrary operation
// sequences and assert the index invariants after every step. Catches cursor
// and line-cache bugs that a fixed unit-test sequence does not reach.
#include "text_buffer.hh"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    TextBuffer buf(4);  // small initial gap: growth paths run early
    for (size_t i = 0; i < size; ++i) {
        const uint8_t op = data[i];
        switch (op % 7) {
        case 0:
            buf.insert(static_cast<char>('a' + (op % 26)));
            break;
        case 1: {
            std::string s;
            const size_t n = 1 + (op % 5);
            for (size_t k = 0; k < n; ++k) s.push_back(static_cast<char>('A' + ((op + k) % 26)));
            buf.insert(s);
            break;
        }
        case 2:
            buf.erase_before();
            break;
        case 3:
            buf.erase_after();
            break;
        case 4:
            buf.move_cursor(static_cast<int>(op % 9) - 4);
            break;
        case 5:
            buf.move_cursor_to(op * 13u % (buf.length() + 1));
            break;
        case 6:
            if (op & 1) buf.insert('\n');
            else        buf.move_to_line_col(static_cast<int>(op % 5) - 2,
                                             static_cast<int>(op % 7) - 3);
            break;
        }

        if (buf.cursor_pos() > buf.length()) __builtin_trap();
        if (buf.to_string().size() != buf.length()) __builtin_trap();
        const int lines = buf.line_count();
        if (lines < 1) __builtin_trap();
        for (int line = 0; line < lines; ++line) {
            const size_t start = buf.line_start(line);
            const size_t end = buf.line_end(line);
            if (start > buf.length() || end < start || end > buf.length()) __builtin_trap();
            (void)buf.line_view(line);  // must tolerate spans_gap and empty lines
        }
        if (buf.length() > 0) (void)buf.at(buf.cursor_pos() % buf.length());
    }
    return 0;
}
