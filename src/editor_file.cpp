#include "editor.h"
#include <algorithm>
#include <dirent.h>
#include <fstream>

void Editor::load_file(const std::string &fname) { open_file(fname); }

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

  buffers.push_back(fb);
  current_buffer = buffers.size() - 1;
  get_pane().buffer_id = current_buffer;

  highlighter.set_language(get_file_extension(path));
}

void Editor::create_new_buffer() {
  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
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
  for (const auto &line : buf.lines) {
    file << line << '\n';
  }
  file.close();
  buf.modified = false;
  message = "Saved: " + get_filename(buf.filepath);
}

void Editor::save_file_as() {
  show_command_palette = true;
  command_palette_query = "Save as: ";
  command_palette_results.clear();
}

void Editor::close_buffer() {
  if (buffers.size() <= 1)
    return;
  buffers.erase(buffers.begin() + current_buffer);
  if (current_buffer >= buffers.size())
    current_buffer = buffers.size() - 1;
  get_pane().buffer_id = current_buffer;
}
