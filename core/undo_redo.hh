#pragma once
#include <string>
#include <vector>
#include <cstddef>

class TextBuffer;

enum class CmdType { Insert, Erase };

struct EditCmd {
    CmdType     type;
    size_t      pos;
    std::string text;
};

class UndoRedo {
public:
    void record_insert(size_t pos, const std::string& text);
    void record_erase (size_t pos, const std::string& text);
    bool try_coalesce_insert(size_t pos, char ch);

    void undo(TextBuffer& buf);
    void redo(TextBuffer& buf);

    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    void clear();

private:
    std::vector<EditCmd> undo_stack_;
    std::vector<EditCmd> redo_stack_;
    void apply(TextBuffer& buf, const EditCmd& cmd, bool reverse);
};
