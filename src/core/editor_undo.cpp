#include "editor.h"

void Editor::save_state() {
    auto& buf = get_buffer();
    State s;
    s.lines = buf.lines;
    s.cursor = buf.cursor;
    s.selection = buf.selection;
    buf.undo_stack.push(s);
    while (!buf.redo_stack.empty()) buf.redo_stack.pop();
}

void Editor::undo() {
    auto& buf = get_buffer();
    if (!buf.undo_stack.empty()) {
        State s;
        s.lines = buf.lines;
        s.cursor = buf.cursor;
        s.selection = buf.selection;
        buf.redo_stack.push(s);
        
        State prev = buf.undo_stack.top();
        buf.undo_stack.pop();
        buf.lines = prev.lines;
        buf.cursor = prev.cursor;
        buf.selection = prev.selection;
        clamp_cursor(get_pane().buffer_id);
    }
}

void Editor::redo() {
    auto& buf = get_buffer();
    if (!buf.redo_stack.empty()) {
        State s;
        s.lines = buf.lines;
        s.cursor = buf.cursor;
        s.selection = buf.selection;
        buf.undo_stack.push(s);
        
        State next = buf.redo_stack.top();
        buf.redo_stack.pop();
        buf.lines = next.lines;
        buf.cursor = next.cursor;
        buf.selection = next.selection;
        clamp_cursor(get_pane().buffer_id);
    }
}

