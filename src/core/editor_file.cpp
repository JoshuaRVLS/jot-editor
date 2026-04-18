#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <map>

void Editor::load_file(const std::string &fname) { open_file(fname); }

int Editor::detect_indent_width(const std::vector<std::string> &lines) const {
  std::map<int, int> delta_score;
  int tab_indented_lines = 0;
  int space_indented_lines = 0;

  auto count_leading = [](const std::string &line, char ch) {
    int n = 0;
    while (n < (int)line.size() && line[n] == ch)
      n++;
    return n;
  };

  int prev_space_indent = -1;
  for (const auto &line : lines) {
    if (line.empty()) {
      continue;
    }

    int tabs = count_leading(line, '\t');
    int spaces = count_leading(line, ' ');
    if (tabs > 0) {
      tab_indented_lines++;
    } else if (spaces > 0) {
      space_indented_lines++;
    }

    if (tabs > 0 || spaces == 0) {
      prev_space_indent = -1;
      continue;
    }

    if (prev_space_indent >= 0 && spaces != prev_space_indent) {
      int delta = std::abs(spaces - prev_space_indent);
      if (delta >= 1 && delta <= 8) {
        delta_score[delta]++;
      }
    }
    prev_space_indent = spaces;
  }

  if (tab_indented_lines > space_indented_lines && tab_indented_lines >= 3) {
    return 4;
  }

  int best_width = -1;
  int best_score = 0;
  for (const auto &[width, score] : delta_score) {
    if (score > best_score || (score == best_score && width < best_width)) {
      best_width = width;
      best_score = score;
    }
  }

  if (best_width >= 1 && best_width <= 8 && best_score >= 2) {
    return best_width;
  }

  return tab_size;
}

void Editor::open_file(const std::string &path) {
  for (size_t i = 0; i < buffers.size(); i++) {
    if (buffers[i].filepath == path) {
      current_buffer = i;
      get_pane().buffer_id = i;
      return;
    }
  }

  FileBuffer fb;
  fb.filepath = path;
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;

  std::ifstream file(path);
  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      fb.lines.push_back(line);
    }
    file.close();
  }

  if (fb.lines.empty())
    fb.lines.push_back("");

  if (config.get_bool("auto_detect_indent", false)) {
    int detected_tab_size = detect_indent_width(fb.lines);
    if (detected_tab_size != tab_size && detected_tab_size >= 1 &&
        detected_tab_size <= 8) {
      tab_size = detected_tab_size;
      message = "Indent detected: " + std::to_string(tab_size) + " spaces";
    }
  }

  buffers.push_back(fb);
  current_buffer = buffers.size() - 1;
  get_pane().buffer_id = current_buffer;

  highlighter.set_language(get_file_extension(path));
  if (python_api)
    python_api->on_buffer_open(path);
}

void Editor::create_new_buffer() {
  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  buffers.push_back(fb);
  current_buffer = buffers.size() - 1;
  get_pane().buffer_id = current_buffer;
}

void Editor::save_file() {
  auto &buf = get_buffer();
  if (buf.filepath.empty()) {
    show_save_prompt = true;
    save_prompt_input.clear();
    needs_redraw = true;
    return;
  }
  std::ofstream file(buf.filepath);
  if (!file.is_open()) {
    message = "Save failed: cannot open " + buf.filepath;
    needs_redraw = true;
    return;
  }
  for (const auto &line : buf.lines) {
    file << line << '\n';
  }
  if (!file.good()) {
    message = "Save failed: write error";
    needs_redraw = true;
    return;
  }
  file.close();
  buf.modified = false;
  message = "Saved: " + get_filename(buf.filepath);
  if (python_api)
    python_api->on_buffer_save(buf.filepath);
}

void Editor::save_file_as() {
  show_command_palette = true;
  command_palette_query = "w ";
  needs_redraw = true;
}

void Editor::close_buffer_at(int index) {
  if (index < 0 || index >= (int)buffers.size())
    return;

  if (buffers.size() == 1) {
    FileBuffer &buf = buffers[0];
    buf.filepath.clear();
    buf.lines.clear();
    buf.lines.push_back("");
    buf.cursor = {0, 0};
    buf.preferred_x = 0;
    buf.selection = {{0, 0}, {0, 0}, false};
    buf.scroll_offset = 0;
    buf.scroll_x = 0;
    buf.modified = false;
    buf.undo_stack = std::stack<State>();
    buf.redo_stack = std::stack<State>();
    buf.bookmarks.clear();
    buf.diagnostics.clear();
    current_buffer = 0;
    for (auto &pane : panes) {
      pane.buffer_id = 0;
    }
    message = "Closed file";
    needs_redraw = true;
    return;
  }

  int removed = index;
  buffers.erase(buffers.begin() + removed);

  if (current_buffer > removed) {
    current_buffer--;
  } else if (current_buffer >= (int)buffers.size()) {
    current_buffer = (int)buffers.size() - 1;
  }

  for (auto &pane : panes) {
    if (pane.buffer_id == removed) {
      pane.buffer_id = current_buffer;
    } else if (pane.buffer_id > removed) {
      pane.buffer_id--;
    }
  }

  if (!panes.empty()) {
    get_pane().buffer_id = current_buffer;
  }
  message = "Closed file";
  needs_redraw = true;
}

void Editor::close_buffer() {
  close_buffer_at(current_buffer);
}
