// C++ implementations of the theme-facing Python API methods.
void PythonAPI::py_show_message(const std::string &msg) {
  if (editor)
    editor->set_message(msg);
}

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
  } else if (name == "status_logo" || name == "statusline_logo" ||
             name == "fg_status_logo" || name == "bg_status_logo") {
    set_pair(theme.fg_status_logo, theme.bg_status_logo);
  } else if (name == "status_file" || name == "statusline_file" ||
             name == "fg_status_file" || name == "bg_status_file") {
    set_pair(theme.fg_status_file, theme.bg_status_file);
  } else if (name == "status_info" || name == "statusline_info" ||
             name == "fg_status_info" || name == "bg_status_info") {
    set_pair(theme.fg_status_info, theme.bg_status_info);
  } else if (name == "status_warning" || name == "status_warn" ||
             name == "statusline_warning" || name == "fg_status_warning" ||
             name == "bg_status_warning") {
    set_pair(theme.fg_status_warning, theme.bg_status_warning);
  } else if (name == "status_error" || name == "statusline_error" ||
             name == "fg_status_error" || name == "bg_status_error") {
    set_pair(theme.fg_status_error, theme.bg_status_error);
  } else if (name == "status_muted" || name == "statusline_muted" ||
             name == "fg_status_muted" || name == "bg_status_muted") {
    set_pair(theme.fg_status_muted, theme.bg_status_muted);
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
