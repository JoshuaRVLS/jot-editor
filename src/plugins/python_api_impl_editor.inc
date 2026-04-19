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
    editor->host().io.save_current_file();
}

void PythonAPI::py_open_file(const std::string &path) {
  if (editor)
    editor->host().io.open_file(path);
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
  return editor->host().core.current_file();
}

std::string PythonAPI::py_get_buffer_content() {
  if (!editor)
    return "";
  return editor->host().core.buffer_content();
}

void PythonAPI::py_set_buffer_content(const std::string &text) {
  if (!editor)
    return;
  editor->host().core.set_buffer_content(text);
}

std::string PythonAPI::py_get_selected_text() {
  if (!editor)
    return "";
  return editor->host().core.selected_text();
}

void PythonAPI::py_execute_command(const std::string &command) {
  if (!editor)
    return;
  editor->host().io.execute_command(command);
}

void PythonAPI::py_execute_ex_command(const std::string &command_line) {
  if (!editor)
    return;

  std::string line = command_line;
  if (!line.empty() && line[0] == ':') {
    line.erase(0, 1);
  }
  editor->command_palette_query = ":" + line;
  editor->handle_command_palette('\n');
}

int PythonAPI::py_reload_plugins() { return reload_user_plugins(); }

std::vector<std::string> PythonAPI::py_list_plugins() { return loaded_plugins; }

bool PythonAPI::py_apply_colorscheme(const std::string &name) {
  if (!python_initialized || !py_module) {
    return false;
  }

  PyObject *module = reinterpret_cast<PyObject *>(py_module);
  PyObject *func = PyObject_GetAttrString(module, "apply_colorscheme");
  if (!func) {
    PyErr_Clear();
    return false;
  }

  if (!PyCallable_Check(func)) {
    Py_DECREF(func);
    return false;
  }

  PyObject *arg = PyUnicode_FromString(name.c_str());
  if (!arg) {
    Py_DECREF(func);
    PyErr_Clear();
    return false;
  }

  PyObject *result = PyObject_CallFunctionObjArgs(func, arg, nullptr);
  Py_DECREF(arg);
  Py_DECREF(func);
  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  bool ok = PyObject_IsTrue(result) == 1;
  Py_DECREF(result);
  return ok;
}

std::vector<std::string> PythonAPI::py_list_themes() {
  std::vector<std::string> themes;
  if (!python_initialized || !py_module) {
    return themes;
  }

  PyObject *module = reinterpret_cast<PyObject *>(py_module);
  PyObject *func = PyObject_GetAttrString(module, "list_colorschemes");
  if (!func) {
    PyErr_Clear();
    return themes;
  }

  if (!PyCallable_Check(func)) {
    Py_DECREF(func);
    return themes;
  }

  PyObject *result = PyObject_CallObject(func, nullptr);
  Py_DECREF(func);
  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return themes;
  }

  if (PyList_Check(result) || PyTuple_Check(result)) {
    const Py_ssize_t count = PySequence_Size(result);
    themes.reserve((size_t)std::max<Py_ssize_t>(0, count));
    for (Py_ssize_t i = 0; i < count; i++) {
      PyObject *item = PySequence_GetItem(result, i);
      if (!item) {
        PyErr_Clear();
        continue;
      }
      if (PyUnicode_Check(item)) {
        const char *s = PyUnicode_AsUTF8(item);
        if (s && *s) {
          themes.emplace_back(s);
        }
      }
      Py_DECREF(item);
    }
  }

  Py_DECREF(result);
  return themes;
}

void PythonAPI::py_set_theme_color(const std::string &name, int fg, int bg) {
  if (!editor)
    return;
  Theme &theme = editor->get_theme();
  auto set_pair = [&](int &slot_fg, int &slot_bg) {
    if (fg != -1)
      slot_fg = fg;
    if (bg != -1)
      slot_bg = bg;
  };
  auto set_fg = [&](int &slot_fg) {
    if (fg != -1)
      slot_fg = fg;
  };
  auto set_bg = [&](int &slot_bg) {
    if (bg != -1)
      slot_bg = bg;
  };

  if (name == "default" || name == "normal") {
    set_pair(theme.fg_default, theme.bg_default);
  } else if (name == "keyword") {
    set_pair(theme.fg_keyword, theme.bg_keyword);
  } else if (name == "string") {
    set_pair(theme.fg_string, theme.bg_string);
  } else if (name == "comment") {
    set_pair(theme.fg_comment, theme.bg_comment);
  } else if (name == "number") {
    set_pair(theme.fg_number, theme.bg_number);
  } else if (name == "function") {
    set_pair(theme.fg_function, theme.bg_function);
  } else if (name == "type") {
    set_pair(theme.fg_type, theme.bg_type);
  } else if (name == "panel_border" || name == "border") {
    set_pair(theme.fg_panel_border, theme.bg_panel_border);
  } else if (name == "selection" || name == "visual" || name == "bg_selection" ||
             name == "fg_selection") {
    set_pair(theme.fg_selection, theme.bg_selection);
  } else if (name == "line_num" || name == "line_number" ||
             name == "fg_line_num" || name == "bg_line_num") {
    set_pair(theme.fg_line_num, theme.bg_line_num);
  } else if (name == "cursor" || name == "fg_cursor" || name == "bg_cursor") {
    set_pair(theme.fg_cursor, theme.bg_cursor);
  } else if (name == "status" || name == "status_bar" ||
             name == "bg_status_bar" || name == "fg_status_bar" ||
             name == "bg_status" || name == "fg_status") {
    set_pair(theme.fg_status, theme.bg_status);
  } else if (name == "status_message" || name == "statusline_message" ||
             name == "fg_status_message") {
    set_fg(theme.fg_status_message);
  } else if (name == "command" || name == "cmdline" || name == "fg_command" ||
             name == "bg_command") {
    set_pair(theme.fg_command, theme.bg_command);
  } else if (name == "search_match" || name == "search" ||
             name == "incsearch" || name == "fg_search_match" ||
             name == "bg_search_match") {
    set_pair(theme.fg_search_match, theme.bg_search_match);
  } else if (name == "minimap") {
    set_pair(theme.fg_minimap, theme.bg_minimap);
  } else if (name == "sidebar" || name == "fg_sidebar" || name == "bg_sidebar") {
    set_pair(theme.fg_sidebar, theme.bg_sidebar);
  } else if (name == "sidebar_directory" || name == "sidebar_dir") {
    set_fg(theme.fg_sidebar_directory);
  } else if (name == "sidebar_selected" || name == "sidebar_sel" ||
             name == "fg_sidebar_selected" || name == "bg_sidebar_selected") {
    set_pair(theme.fg_sidebar_selected, theme.bg_sidebar_selected);
  } else if (name == "sidebar_selected_inactive" || name == "sidebar_sel_nc") {
    set_pair(theme.fg_sidebar_selected_inactive, theme.bg_sidebar_selected_inactive);
  } else if (name == "sidebar_border" || name == "fg_sidebar_border") {
    set_fg(theme.fg_sidebar_border);
  } else if (name == "tab_active" || name == "tabline_sel" ||
             name == "fg_tab_active" || name == "bg_tab_active") {
    set_pair(theme.fg_tab_active, theme.bg_tab_active);
  } else if (name == "tab_inactive" || name == "tabline" ||
             name == "fg_tab_inactive" || name == "bg_tab_inactive") {
    set_pair(theme.fg_tab_inactive, theme.bg_tab_inactive);
  } else if (name == "tab_close" || name == "fg_tab_close") {
    set_fg(theme.fg_tab_close);
  } else if (name == "tab_separator" || name == "tabline_fill" ||
             name == "fg_tab_separator") {
    set_fg(theme.fg_tab_separator);
  } else if (name == "active_border" || name == "win_active_border" ||
             name == "fg_active_border" || name == "bg_active_border") {
    set_pair(theme.fg_active_border, theme.bg_active_border);
  } else if (name == "image_border") {
    set_pair(theme.fg_image_border, theme.bg_image_border);
  } else if (name == "diagnostic_error" || name == "fg_diagnostic_error") {
    set_fg(theme.fg_diagnostic_error);
  } else if (name == "diagnostic_warning" || name == "diagnostic_warn" ||
             name == "fg_diagnostic_warning") {
    set_fg(theme.fg_diagnostic_warning);
  } else if (name == "diagnostic_info" || name == "fg_diagnostic_info") {
    set_fg(theme.fg_diagnostic_info);
  } else if (name == "diagnostic_hint" || name == "fg_diagnostic_hint") {
    set_fg(theme.fg_diagnostic_hint);
  } else if (name == "bracket_match") {
    set_pair(theme.fg_bracket_match, theme.bg_bracket_match);
  } else if (name == "bracket1" || name == "fg_bracket1") {
    set_fg(theme.fg_bracket1);
  } else if (name == "bracket2" || name == "fg_bracket2") {
    set_fg(theme.fg_bracket2);
  } else if (name == "bracket3" || name == "fg_bracket3") {
    set_fg(theme.fg_bracket3);
  } else if (name == "bracket4" || name == "fg_bracket4") {
    set_fg(theme.fg_bracket4);
  } else if (name == "bracket5" || name == "fg_bracket5") {
    set_fg(theme.fg_bracket5);
  } else if (name == "bracket6" || name == "fg_bracket6") {
    set_fg(theme.fg_bracket6);
  } else if (name == "telescope") {
    set_pair(theme.fg_telescope, theme.bg_telescope);
  } else if (name == "telescope_selected") {
    set_pair(theme.fg_telescope_selected, theme.bg_telescope_selected);
  } else if (name == "telescope_preview") {
    set_pair(theme.fg_telescope_preview, theme.bg_telescope_preview);
  } else if (name == "terminal" || name == "terminal_panel" ||
             name == "fg_terminal" || name == "bg_terminal") {
    set_pair(theme.fg_terminal, theme.bg_terminal);
  } else if (name == "terminal_tab_inactive" ||
             name == "fg_terminal_tab_inactive" ||
             name == "bg_terminal_tab_inactive") {
    set_pair(theme.fg_terminal_tab_inactive, theme.bg_terminal_tab_inactive);
  } else if (name == "terminal_tab_active" ||
             name == "fg_terminal_tab_active" ||
             name == "bg_terminal_tab_active") {
    set_pair(theme.fg_terminal_tab_active, theme.bg_terminal_tab_active);
  } else if (name == "terminal_tab_focused" ||
             name == "fg_terminal_tab_focused" ||
             name == "bg_terminal_tab_focused") {
    set_pair(theme.fg_terminal_tab_focused, theme.bg_terminal_tab_focused);
  } else if (name == "terminal_tab_close" || name == "fg_terminal_tab_close") {
    set_fg(theme.fg_terminal_tab_close);
  } else if (name == "terminal_tab_plus" ||
             name == "fg_terminal_tab_plus" ||
             name == "bg_terminal_tab_plus") {
    set_pair(theme.fg_terminal_tab_plus, theme.bg_terminal_tab_plus);
  } else if (name == "terminal_tab_separator" ||
             name == "fg_terminal_tab_separator") {
    set_fg(theme.fg_terminal_tab_separator);
  } else if (name == "bg_default") {
    set_bg(theme.bg_default);
  } else if (name == "fg_default") {
    set_fg(theme.fg_default);
  } else if (name == "fg_keyword") {
    set_fg(theme.fg_keyword);
  } else if (name == "fg_string") {
    set_fg(theme.fg_string);
  } else if (name == "fg_comment") {
    set_fg(theme.fg_comment);
  } else if (name == "fg_function") {
    set_fg(theme.fg_function);
  } else if (name == "fg_type") {
    set_fg(theme.fg_type);
  } else if (name == "fg_number") {
    set_fg(theme.fg_number);
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

namespace {
std::string py_json_escape(const std::string &in) {
  std::string out;
  out.reserve(in.size() + 8);
  for (char c : in) {
    switch (c) {
    case '\\':
      out += "\\\\\\\\";
      break;
    case '\"':
      out += "\\\\\"";
      break;
    case '\n':
      out += "\\\\n";
      break;
    case '\r':
      out += "\\\\r";
      break;
    case '\t':
      out += "\\\\t";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}
} // namespace

std::string PythonAPI::py_list_buffers_json() {
  if (!editor) {
    return "[]";
  }

  const auto buffers = editor->host().core.list_buffers();
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < buffers.size(); i++) {
    if (i > 0) {
      ss << ",";
    }
    const auto &b = buffers[i];
    ss << "{\"index\":" << b.index << ",\"filepath\":\"" << py_json_escape(b.filepath)
       << "\",\"modified\":" << (b.modified ? "true" : "false")
       << ",\"active\":" << (b.active ? "true" : "false")
       << ",\"preview\":" << (b.preview ? "true" : "false") << "}";
  }
  ss << "]";
  return ss.str();
}

std::string PythonAPI::py_list_panes_json() {
  if (!editor) {
    return "[]";
  }

  const auto panes = editor->host().render.list_panes();
  std::stringstream ss;
  ss << "[";
  for (size_t i = 0; i < panes.size(); i++) {
    if (i > 0) {
      ss << ",";
    }
    const auto &p = panes[i];
    ss << "{\"index\":" << p.index << ",\"buffer_id\":" << p.buffer_id
       << ",\"x\":" << p.x << ",\"y\":" << p.y << ",\"w\":" << p.w
       << ",\"h\":" << p.h << ",\"focused\":" << (p.focused ? "true" : "false")
       << "}";
  }
  ss << "]";
  return ss.str();
}

std::string PythonAPI::py_get_layout_json() {
  if (!editor) {
    return "{}";
  }

  const auto layout = editor->host().render.layout();
  std::stringstream ss;
  ss << "{\"width\":" << layout.width << ",\"height\":" << layout.height
     << ",\"sidebar_visible\":" << (layout.sidebar_visible ? "true" : "false")
     << ",\"sidebar_width\":" << layout.sidebar_width
     << ",\"minimap_visible\":" << (layout.minimap_visible ? "true" : "false")
     << ",\"terminal_visible\":" << (layout.terminal_visible ? "true" : "false")
     << ",\"terminal_height\":" << layout.terminal_height << "}";
  return ss.str();
}

bool PythonAPI::py_switch_buffer(int index) {
  if (!editor) {
    return false;
  }
  return editor->host().core.switch_buffer(index);
}

bool PythonAPI::py_close_buffer(int index) {
  if (!editor) {
    return false;
  }
  return editor->host().core.close_buffer(index);
}

void PythonAPI::py_new_buffer() {
  if (editor) {
    editor->host().core.new_buffer();
  }
}

void PythonAPI::py_split_pane(const std::string &direction) {
  if (!editor) {
    return;
  }
  if (direction == "h" || direction == "horizontal" || direction == "row") {
    editor->host().render.split_horizontal();
  } else {
    editor->host().render.split_vertical();
  }
}

bool PythonAPI::py_resize_pane(int delta) {
  if (!editor) {
    return false;
  }
  return editor->host().render.resize_focused_pane(delta);
}

void PythonAPI::py_focus_next_pane() {
  if (editor) {
    editor->host().render.focus_next_pane();
  }
}

void PythonAPI::py_focus_prev_pane() {
  if (editor) {
    editor->host().render.focus_prev_pane();
  }
}

void PythonAPI::py_toggle_sidebar() {
  if (editor) {
    editor->host().io.toggle_sidebar();
  }
}

void PythonAPI::py_open_workspace(const std::string &path) {
  if (editor) {
    editor->host().io.open_workspace(path);
  }
}

void PythonAPI::py_toggle_terminal() {
  if (editor) {
    editor->host().io.toggle_terminal();
  }
}

void PythonAPI::py_request_redraw() {
  if (editor) {
    editor->host().render.request_redraw();
  }
}

void PythonAPI::py_request_quit(bool force) {
  if (!editor) {
    return;
  }

  if (force) {
    editor->running = false;
    return;
  }

  bool has_unsaved = false;
  for (const auto &buf : editor->buffers) {
    if (buf.modified) {
      has_unsaved = true;
      break;
    }
  }

  if (has_unsaved) {
    editor->show_quit_prompt = true;
    editor->needs_redraw = true;
  } else {
    editor->running = false;
  }
}

void PythonAPI::py_save_and_quit() {
  if (!editor) {
    return;
  }
  editor->host().io.save_current_file();
  editor->running = false;
}

void PythonAPI::py_toggle_minimap() {
  if (!editor) {
    return;
  }
  editor->toggle_minimap();
}
