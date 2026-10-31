#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

class TextBuffer {
public:
    explicit TextBuffer(size_t initial_gap = 4096);

    void insert(char ch);
    void insert(const std::string& text);
    void erase_before();
    void erase_after();

    void move_cursor(int delta);
    void move_cursor_to(size_t pos);
    void move_to_line_col(int line, int col);

    size_t cursor_pos() const;
    size_t length() const;
    char   at(size_t pos) const;
    std::string to_string() const;

    int    line_count();
    int    cursor_line();
    int    cursor_col();
    size_t line_start(int line);
    size_t line_end(int line);   // exclusive, includes the \n

    bool   dirty()      const { return dirty_; }
    void   clear_dirty()      { dirty_ = false; }
    size_t generation() const { return gen_; }

    // Zero-copy line access. Returns a string_view directly into the gap buffer
    // for the characters of line `li` (excluding the trailing \n). Returns an
    // empty view if the line spans the gap (only the cursor line ever does).
    std::string_view line_view(int li);

    // Raw buffer pointers for hot rendering paths
    const char* pre_gap_ptr()   const { return buf_.data(); }
    size_t      pre_gap_size()  const { return gap_start_; }
    const char* post_gap_ptr()  const { return buf_.data() + gap_end_; }
    size_t      post_gap_size() const { return buf_.size() - gap_end_; }
    bool spans_gap(size_t lstart, size_t lend) const {
        return lstart < gap_start_ && lend > gap_start_;
    }

private:
    std::vector<char> buf_;
    size_t gap_start_ = 0;
    size_t gap_end_   = 0;
    bool   dirty_     = false;
    size_t gen_       = 0;

    std::vector<size_t> line_starts_;
    bool line_cache_valid_ = false;

    void grow_gap(size_t needed);
    void move_gap_to(size_t pos);
    void invalidate_lines() { line_cache_valid_ = false; }
    void rebuild_lines();

    // Shift all line starts > threshold by delta (+1 or -1).
    // Used for incremental cache updates when a non-newline char is inserted/erased.
    void shift_line_starts_after(size_t threshold, int delta);

    size_t logical_to_raw(size_t pos) const;
};
