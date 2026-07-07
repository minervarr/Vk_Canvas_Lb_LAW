#include "undo_redo.hh"
#include "text_buffer.hh"

static constexpr size_t kMaxCoalesceLen = 64;

void UndoRedo::record_insert(size_t pos, const std::string& text) {
    redo_stack_.clear();
    undo_stack_.push_back({CmdType::Insert, pos, text});
}

void UndoRedo::record_erase(size_t pos, const std::string& text) {
    redo_stack_.clear();
    undo_stack_.push_back({CmdType::Erase, pos, text});
}

bool UndoRedo::try_coalesce_insert(size_t pos, char ch) {
    if (ch == '\n' || ch == '\r') return false;
    if (undo_stack_.empty()) return false;
    EditCmd& last = undo_stack_.back();
    if (last.type != CmdType::Insert) return false;
    if (last.text.size() >= kMaxCoalesceLen) return false;
    if (last.pos + last.text.size() != pos) return false;
    redo_stack_.clear();
    last.text += ch;
    return true;
}

void UndoRedo::apply(TextBuffer& buf, const EditCmd& cmd, bool reverse) {
    if (!reverse) {
        buf.move_cursor_to(cmd.pos);
        if (cmd.type == CmdType::Insert) buf.insert(cmd.text);
        else for (size_t i = 0; i < cmd.text.size(); ++i) buf.erase_after();
    } else {
        if (cmd.type == CmdType::Insert) {
            buf.move_cursor_to(cmd.pos + cmd.text.size());
            for (size_t i = 0; i < cmd.text.size(); ++i) buf.erase_before();
        } else {
            buf.move_cursor_to(cmd.pos);
            buf.insert(cmd.text);
        }
    }
}

void UndoRedo::undo(TextBuffer& buf) {
    if (undo_stack_.empty()) return;
    EditCmd cmd = undo_stack_.back(); undo_stack_.pop_back();
    apply(buf, cmd, true);
    redo_stack_.push_back(std::move(cmd));
}

void UndoRedo::redo(TextBuffer& buf) {
    if (redo_stack_.empty()) return;
    EditCmd cmd = redo_stack_.back(); redo_stack_.pop_back();
    apply(buf, cmd, false);
    undo_stack_.push_back(std::move(cmd));
}

void UndoRedo::clear() { undo_stack_.clear(); redo_stack_.clear(); }
