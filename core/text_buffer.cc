#include "text_buffer.hh"
#include <algorithm>

TextBuffer::TextBuffer(size_t initial_gap) {
    buf_.resize(initial_gap);
    gap_start_ = 0;
    gap_end_   = initial_gap;
}

size_t TextBuffer::length() const {
    return buf_.size() - (gap_end_ - gap_start_);
}

size_t TextBuffer::cursor_pos() const { return gap_start_; }

size_t TextBuffer::logical_to_raw(size_t pos) const {
    if (pos < gap_start_) return pos;
    return pos + (gap_end_ - gap_start_);
}

char TextBuffer::at(size_t pos) const { return buf_[logical_to_raw(pos)]; }

std::string TextBuffer::to_string() const {
    std::string s;
    s.reserve(length());
    s.append(buf_.data(), gap_start_);
    s.append(buf_.data() + gap_end_, buf_.size() - gap_end_);
    return s;
}

void TextBuffer::grow_gap(size_t needed) {
    size_t gap_size = gap_end_ - gap_start_;
    if (gap_size >= needed) return;
    size_t new_gap = std::max(needed, gap_size + 4096);
    std::vector<char> newbuf(buf_.size() + new_gap - gap_size);
    std::copy(buf_.begin(), buf_.begin() + gap_start_, newbuf.begin());
    std::copy(buf_.begin() + gap_end_, buf_.end(),
              newbuf.begin() + gap_start_ + new_gap);
    gap_end_ = gap_start_ + new_gap;
    buf_     = std::move(newbuf);
}

void TextBuffer::move_gap_to(size_t pos) {
    if (pos == gap_start_) return;
    if (pos < gap_start_) {
        size_t delta = gap_start_ - pos;
        std::copy_backward(buf_.begin() + pos,
                           buf_.begin() + gap_start_,
                           buf_.begin() + gap_end_);
        gap_start_ -= delta;
        gap_end_   -= delta;
    } else {
        size_t delta = pos - gap_start_;
        std::copy(buf_.begin() + gap_end_,
                  buf_.begin() + gap_end_ + delta,
                  buf_.begin() + gap_start_);
        gap_start_ += delta;
        gap_end_   += delta;
    }
}

void TextBuffer::shift_line_starts_after(size_t threshold, int delta) {
    for (auto& ls : line_starts_)
        if (ls > threshold) ls += delta;
}

void TextBuffer::insert(char ch) {
    grow_gap(1);
    size_t insert_pos = gap_start_;
    buf_[gap_start_++] = ch;
    dirty_ = true; ++gen_;
    if (ch == '\n') {
        invalidate_lines();
    } else if (line_cache_valid_) {
        // No new line: just shift starts after the insertion point
        shift_line_starts_after(insert_pos, +1);
    }
}

void TextBuffer::insert(const std::string& text) {
    grow_gap(text.size());
    size_t insert_pos = gap_start_;
    std::copy(text.begin(), text.end(), buf_.begin() + gap_start_);
    gap_start_ += text.size();
    dirty_ = true; ++gen_;
    // If the inserted text contains a newline, full rebuild
    bool has_newline = text.find('\n') != std::string::npos;
    if (has_newline || !line_cache_valid_) {
        invalidate_lines();
    } else {
        shift_line_starts_after(insert_pos, (int)text.size());
    }
}

void TextBuffer::erase_before() {
    if (gap_start_ == 0) return;
    char erased = buf_[gap_start_ - 1];
    --gap_start_;
    dirty_ = true; ++gen_;
    if (erased == '\n') {
        invalidate_lines();
    } else if (line_cache_valid_) {
        shift_line_starts_after(gap_start_, -1);
    }
}

void TextBuffer::erase_after() {
    if (gap_end_ >= buf_.size()) return;
    char erased = buf_[gap_end_];
    ++gap_end_;
    dirty_ = true; ++gen_;
    if (erased == '\n') {
        invalidate_lines();
    } else if (line_cache_valid_) {
        // Erasing at gap_start_ (logical position) shifts everything after by -1
        shift_line_starts_after(gap_start_, -1);
    }
}

void TextBuffer::move_cursor(int delta) {
    int pos = static_cast<int>(gap_start_) + delta;
    pos = std::clamp(pos, 0, static_cast<int>(length()));
    move_gap_to(static_cast<size_t>(pos));
}

void TextBuffer::move_cursor_to(size_t pos) {
    pos = std::min(pos, length());
    move_gap_to(pos);
}

void TextBuffer::rebuild_lines() {
    line_starts_.clear();
    line_starts_.push_back(0);
    size_t len = length();
    for (size_t i = 0; i < len; ++i)
        if (at(i) == '\n' && i + 1 <= len)
            line_starts_.push_back(i + 1);
    line_cache_valid_ = true;
}

int TextBuffer::line_count() {
    if (!line_cache_valid_) rebuild_lines();
    return static_cast<int>(line_starts_.size());
}

int TextBuffer::cursor_line() {
    if (!line_cache_valid_) rebuild_lines();
    size_t cur = cursor_pos();
    int lo = 0, hi = static_cast<int>(line_starts_.size()) - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (line_starts_[mid] <= cur) lo = mid; else hi = mid - 1;
    }
    return lo;
}

int TextBuffer::cursor_col() {
    if (!line_cache_valid_) rebuild_lines();
    return static_cast<int>(cursor_pos() - line_starts_[cursor_line()]);
}

size_t TextBuffer::line_start(int line) {
    if (!line_cache_valid_) rebuild_lines();
    if (line < 0 || line >= (int)line_starts_.size()) return 0;
    return line_starts_[line];
}

size_t TextBuffer::line_end(int line) {
    if (!line_cache_valid_) rebuild_lines();
    int next = line + 1;
    if (next < (int)line_starts_.size()) return line_starts_[next];
    return length();
}

void TextBuffer::move_to_line_col(int line, int col) {
    if (!line_cache_valid_) rebuild_lines();
    line = std::clamp(line, 0, line_count() - 1);
    size_t start = line_start(line);
    size_t end   = line_end(line);
    size_t target = std::min(start + (size_t)col, end);
    if (target > 0 && target == end && at(target - 1) == '\n') --target;
    move_cursor_to(target);
}

std::string_view TextBuffer::line_view(int li) {
    if (!line_cache_valid_) rebuild_lines();
    size_t start = line_start(li);
    size_t end   = line_end(li);
    // Exclude the trailing newline
    if (end > start && end <= length() && at(end - 1) == '\n') --end;
    if (end <= start) return {};
    if (spans_gap(start, end)) return {};  // cursor line — spans the gap
    // Map logical start to raw index; range is contiguous
    size_t raw = logical_to_raw(start);
    return std::string_view(buf_.data() + raw, end - start);
}
