// C++ Implementations of Python API methods
void PythonAPI::py_enter_normal_mode() {
  // if (editor) editor->set_mode(MODE_NORMAL);
}

void PythonAPI::py_enter_insert_mode() {
  // if (editor) editor->set_mode(MODE_INSERT);
}

void PythonAPI::py_enter_visual_mode() {
  // if (editor) editor->set_mode(MODE_VISUAL);
}

void PythonAPI::py_move_cursor(int dx, int dy) {
  if (editor)
    editor->move_cursor(dx, dy, false);
}

void PythonAPI::py_insert_char(char c) {
  if (editor)
    editor->insert_char(c);
}

void PythonAPI::py_delete_char(bool forward) {
  if (editor)
    editor->delete_char(forward);
}

void PythonAPI::py_save_file() {
  if (editor)
    editor->save_file();
}

void PythonAPI::py_open_file(const std::string &path) {
  if (editor)
    editor->open_file(path);
}

void PythonAPI::py_show_message(const std::string &msg) {
  if (editor)
    editor->set_message(msg);
}

std::string PythonAPI::py_get_mode() { return "insert"; }

int PythonAPI::py_get_cursor_x() {
  if (!editor)
    return 0;
  FileBuffer &buf = editor->get_buffer();
  return buf.cursor.x;
}

int PythonAPI::py_get_cursor_y() {
  if (!editor)
    return 0;
  FileBuffer &buf = editor->get_buffer();
  return buf.cursor.y;
}

std::string PythonAPI::py_get_line(int line) {
  if (!editor)
    return "";
  FileBuffer &buf = editor->get_buffer();
  if (line >= 0 && line < (int)buf.lines.size()) {
    return buf.lines[line];
  }
  return "";
}

int PythonAPI::py_get_line_count() {
  if (!editor)
    return 0;
  FileBuffer &buf = editor->get_buffer();
  return buf.lines.size();
}

std::string PythonAPI::py_get_current_file() {
  if (!editor)
    return "";
  return editor->get_buffer().filepath;
}

std::string PythonAPI::py_get_buffer_content() {
  if (!editor)
    return "";
  return editor->get_buffer_text(editor->get_buffer());
}

void PythonAPI::py_set_buffer_content(const std::string &text) {
  if (!editor)
    return;

  editor->save_state();
  FileBuffer &buf = editor->get_buffer();
  buf.lines.clear();

  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    buf.lines.push_back(line);
  }

  if (!text.empty() && text.back() == '\n') {
    buf.lines.push_back("");
  }
  if (buf.lines.empty()) {
    buf.lines.push_back("");
  }

  buf.cursor = {0, 0};
  buf.preferred_x = 0;
  buf.selection = {{0, 0}, {0, 0}, false};
  buf.scroll_offset = 0;
  buf.scroll_x = 0;
  buf.modified = true;
  editor->invalidate_syntax_cache(buf);
  editor->ensure_cursor_visible();
  editor->needs_redraw = true;
  on_buffer_change(buf.filepath, text);
}

std::string PythonAPI::py_get_selected_text() {
  if (!editor)
    return "";

  FileBuffer &buf = editor->get_buffer();
  if (!buf.selection.active) {
    return "";
  }

  Cursor start = buf.selection.start;
  Cursor end = buf.selection.end;
  if (start.y > end.y || (start.y == end.y && start.x > end.x)) {
    std::swap(start, end);
  }

  std::string result;
  for (int y = start.y; y <= end.y && y < (int)buf.lines.size(); y++) {
    const std::string &line = buf.lines[y];
    int from = (y == start.y) ? std::max(0, start.x) : 0;
    int to = (y == end.y) ? std::min((int)line.size(), end.x) : (int)line.size();
    if (from < to) {
      result += line.substr((size_t)from, (size_t)(to - from));
    }
    if (y != end.y) {
      result.push_back('\n');
    }
  }
  return result;
}

void PythonAPI::py_execute_command(const std::string &command) {
  if (!editor)
    return;
  editor->command_palette_query = command;
  editor->handle_command_palette('\n');
}

int PythonAPI::py_reload_plugins() { return reload_user_plugins(); }

std::vector<std::string> PythonAPI::py_list_plugins() { return loaded_plugins; }

void PythonAPI::py_set_theme_color(const std::string &name, int fg, int bg) {
  if (!editor)
    return;
  Theme &theme = editor->get_theme();

  if (name == "default") {
    theme.fg_default = fg;
    theme.bg_default = bg;
  } else if (name == "keyword") {
    theme.fg_keyword = fg;
    theme.bg_keyword = bg;
  } else if (name == "string") {
    theme.fg_string = fg;
    theme.bg_string = bg;
  } else if (name == "comment") {
    theme.fg_comment = fg;
    theme.bg_comment = bg;
  } else if (name == "number") {
    theme.fg_number = fg;
    theme.bg_number = bg;
  } else if (name == "function") {
    theme.fg_function = fg;
    theme.bg_function = bg;
  } else if (name == "type") {
    theme.fg_type = fg;
    theme.bg_type = bg;
  } else if (name == "panel_border") {
    theme.fg_panel_border = fg;
    theme.bg_panel_border = bg;
  } else if (name == "bg_default") {
    theme.bg_default = bg;
  } else if (name == "fg_default") {
    theme.fg_default = fg;
  } else if (name == "selection" || name == "bg_selection" ||
             name == "fg_selection") {
    if (bg != -1)
      theme.bg_selection = bg;
    if (fg != -1)
      theme.fg_selection = fg;
  } else if (name == "line_num" || name == "fg_line_num" ||
             name == "bg_line_num") {
    if (fg != -1)
      theme.fg_line_num = fg;
    if (bg != -1)
      theme.bg_line_num = bg;
  } else if (name == "cursor" || name == "fg_cursor") {
    if (fg != -1)
      theme.fg_cursor = fg;
    if (bg != -1)
      theme.bg_cursor = bg;
  } else if (name == "bg_status_bar") {
    theme.bg_status = bg;
  } else if (name == "fg_status_bar") {
    theme.fg_status = fg;
  } else if (name == "fg_keyword") {
    theme.fg_keyword = fg;
  } else if (name == "fg_string") {
    theme.fg_string = fg;
  } else if (name == "fg_comment") {
    theme.fg_comment = fg;
  } else if (name == "fg_function") {
    theme.fg_function = fg;
  } else if (name == "fg_type") {
    theme.fg_type = fg;
  } else if (name == "fg_number") {
    theme.fg_number = fg;
  } else if (name == "fg_command" || name == "bg_command") {
    if (fg != -1)
      theme.fg_command = fg;
    if (bg != -1)
      theme.bg_command = bg;
  } else if (name == "minimap") {
    theme.fg_minimap = fg;
    theme.bg_minimap = bg;
  } else if (name == "image_border") {
    theme.fg_image_border = fg;
    theme.bg_image_border = bg;
  } else if (name == "bracket_match") {
    theme.fg_bracket_match = fg;
    theme.bg_bracket_match = bg;
  } else if (name == "telescope") {
    theme.fg_telescope = fg;
    theme.bg_telescope = bg;
  } else if (name == "telescope_selected") {
    theme.fg_telescope_selected = fg;
    theme.bg_telescope_selected = bg;
  } else if (name == "telescope_preview") {
    theme.fg_telescope_preview = fg;
    theme.bg_telescope_preview = bg;
  }
}

void PythonAPI::py_move_line_up() {
  if (editor)
    editor->move_line_up();
}

void PythonAPI::py_move_line_down() {
  if (editor)
    editor->move_line_down();
}

void PythonAPI::py_show_popup(const std::string &text, int x, int y) {
  if (editor)
    editor->show_popup(text, x, y);
}

void PythonAPI::py_hide_popup() {
  if (editor)
    editor->hide_popup();
}

void PythonAPI::py_clear_diagnostics(const std::string &filepath) {
  if (editor) {
    editor->set_diagnostics(filepath, {});
  }
}

void PythonAPI::py_add_diagnostic(const std::string &filepath, int line,
                                  int col, int end_line, int end_col,
                                  const std::string &message, int severity) {
  if (editor) {
    Diagnostic d;
    d.line = line;
    d.col = col;
    d.end_line = end_line;
    d.end_col = end_col;
    d.message = message;
    d.severity = severity;
    editor->add_diagnostic(filepath, d);
  }
}

void PythonAPI::py_set_diagnostics(const std::string &filepath,
                                   const std::vector<Diagnostic> &diagnostics) {
  if (editor) {
    editor->set_diagnostics(filepath, diagnostics);
  }
}
