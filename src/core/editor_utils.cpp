#include "editor.h"

FileBuffer& Editor::get_buffer(int id) {
    if (id == -1) id = current_buffer;
    if (buffers.empty()) {
        FileBuffer fb;
        fb.lines.push_back("");
        fb.cursor = {0, 0};
        fb.selection = {{0, 0}, {0, 0}, false};
        fb.scroll_offset = 0;
        fb.scroll_x = 0;
        fb.modified = false;
        buffers.push_back(fb);
    }
    return buffers[id >= 0 && id < buffers.size() ? id : 0];
}

SplitPane& Editor::get_pane(int id) {
    if (id == -1) id = current_pane;
    if (panes.empty()) {
        int h = ui->get_height();
        int w = ui->get_width();
        create_pane(0, 0, w, h, -1);
    }
    return panes[id >= 0 && id < panes.size() ? id : 0];
}

std::string Editor::get_file_extension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        return path.substr(dot);
    }
    return "";
}

std::string Editor::get_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        return path.substr(pos + 1);
    }
    return path;
}

void Editor::select_all() {
    auto& buf = get_buffer();
    buf.selection.start = {0, 0};
    buf.selection.end = {(int)buf.lines.back().length(), (int)buf.lines.size() - 1};
    buf.selection.active = true;
    buf.cursor = buf.selection.end;
}

void Editor::clear_selection() {
    get_buffer().selection.active = false;
}

