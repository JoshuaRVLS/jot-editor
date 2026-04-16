#include "editor.h"

void Editor::toggle_bookmark() {
    auto& buf = get_buffer();
    if (buf.bookmarks.count(buf.cursor.y)) {
        buf.bookmarks.erase(buf.cursor.y);
        message = "Bookmark removed";
    } else {
        buf.bookmarks.insert(buf.cursor.y);
        message = "Bookmark added";
    }
}

void Editor::next_bookmark() {
    auto& buf = get_buffer();
    if (buf.bookmarks.empty()) return;
    
    auto it = buf.bookmarks.upper_bound(buf.cursor.y);
    if (it != buf.bookmarks.end()) {
        buf.cursor.y = *it;
    } else {
        buf.cursor.y = *buf.bookmarks.begin();
    }
    clamp_cursor(get_pane().buffer_id);
}

void Editor::prev_bookmark() {
    auto& buf = get_buffer();
    if (buf.bookmarks.empty()) return;
    
    auto it = buf.bookmarks.lower_bound(buf.cursor.y);
    if (it != buf.bookmarks.begin()) {
        --it;
        buf.cursor.y = *it;
    } else {
        buf.cursor.y = *buf.bookmarks.rbegin();
    }
    clamp_cursor(get_pane().buffer_id);
}

