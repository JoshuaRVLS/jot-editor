// This file is #included by python_api_bindings.cpp and compiled as part of
// the same translation unit. Do not add includes here — they are inherited.
// ---------------------------------------------------------------------------


PythonAPI::PythonAPI(Editor *ed)
    : editor(ed), py_module(nullptr), python_initialized(false) {
  g_python_api = this;
}

PythonAPI::~PythonAPI() {
  cleanup();
  g_python_api = nullptr;
}

bool PythonAPI::init() {
  if (python_initialized)
    return true;

  // Register built-in module BEFORE Py_Initialize
  if (PyImport_AppendInittab("_jcode_internal", PyInit_jcode_api) == -1) {
    std::cerr << "Failed to register _jcode_internal module" << std::endl;
    return false;
  }

  Py_Initialize();
  if (!Py_IsInitialized()) {
    return false;
  }

  python_initialized = true;

  PyRun_SimpleString("import os\nimport sys\nsys.path.append(os.getcwd())\n");

  std::vector<fs::path> runtime_dirs = get_runtime_python_dirs();
  for (const auto &dir : runtime_dirs) {
    append_python_path(dir);
  }

  for (const auto &dir : runtime_dirs) {
    fs::path api_path = dir / "jcode_api.py";
    if (fs::exists(api_path) && load_plugin(api_path.string())) {
      break;
    }
  }

  for (const auto &dir : runtime_dirs) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
      continue;
    }
    load_plugin((dir / "lsp.py").string());
    load_plugin((dir / "lsp_plugin.py").string());
    load_plugin((dir / "theme_plugin.py").string());
    break;
  }

  fs::path config_root = get_user_config_root();
  if (!config_root.empty()) {
    fs::create_directories(config_root / "configs" / "colors");
    fs::create_directories(config_root / "configs" / "plugins");
    fs::create_directories(config_root / "plugins");
    fs::create_directories(config_root / "themes");

    append_python_path(config_root);
    append_python_path(config_root / "plugins");
    append_python_path(config_root / "configs");
    append_python_path(config_root / "configs" / "plugins");
    append_python_path(config_root / "configs" / "colors");
    append_python_path(config_root / "themes");

    load_plugin((config_root / "config.py").string());
    load_plugin((config_root / "init.py").string());
    load_plugin((config_root / "configs" / "init.py").string());
    load_plugins((config_root / "plugins").string());
    load_plugins((config_root / "configs" / "plugins").string());

    if (!fs::exists(config_root / "config.py") &&
        !fs::exists(config_root / "init.py") &&
        !fs::exists(config_root / "configs" / "init.py")) {
      load_plugin((config_root / "configs" / "colors" / "jcode_nvim.py").string());
    }
  }

  return true;
}

void PythonAPI::cleanup() {
  if (py_module) {
    Py_DECREF((PyObject *)py_module);
    py_module = nullptr;
  }

  if (python_initialized) {
    Py_Finalize();
    python_initialized = false;
  }
}

void PythonAPI::execute_code(const std::string &code) {
  if (!python_initialized)
    return;
  PyRun_SimpleString(code.c_str());
}

// Helper to call python function
static void call_python_hook(const char *func_name, const std::string &arg) {
  PyObject *module = PyImport_ImportModule("jcode_api");
  if (!module) {
    std::cerr << "Failed to import jcode_api for " << func_name << std::endl;
    PyErr_Print();
    return;
  }

  PyObject *func = PyObject_GetAttrString(module, func_name);
  if (func && PyCallable_Check(func)) {
    PyObject *args = PyTuple_Pack(1, PyUnicode_FromString(arg.c_str()));
    PyObject *result = PyObject_CallObject(func, args);
    if (!result) {
      std::cerr << "Error calling " << func_name << std::endl;
      PyErr_Print();
    } else {
      Py_DECREF(result);
    }
    Py_DECREF(args);
    Py_DECREF(func);
  } else {
    std::cerr << "Function " << func_name << " not found in jcode_api"
              << std::endl;
    if (PyErr_Occurred())
      PyErr_Print();
  }
  Py_DECREF(module);
}

void PythonAPI::on_buffer_open(const std::string &filepath) {
  if (!python_initialized)
    return;
  call_python_hook("_on_buffer_open", filepath);
}

void PythonAPI::on_buffer_change(const std::string &filepath,
                                 const std::string &content) {
  if (!python_initialized)
    return;
  call_python_hook("_on_buffer_change", filepath);
}

void PythonAPI::on_buffer_save(const std::string &filepath) {
  if (!python_initialized)
    return;
  call_python_hook("_on_buffer_save", filepath);
}

void PythonAPI::load_plugins(const std::string &plugin_dir) {
  if (!fs::exists(plugin_dir) || !fs::is_directory(plugin_dir)) {
    fs::create_directories(plugin_dir);
    return;
  }

  for (const auto &entry : fs::directory_iterator(plugin_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".py") {
      std::string plugin_path = entry.path().string();
      load_plugin(plugin_path);
    }
  }
}

bool PythonAPI::load_plugin(const std::string &path) {
  if (!python_initialized)
    return false;

  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string code = buffer.str();

  // Run in __main__ context
  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  // Execute plugin code
  PyObject *result =
      PyRun_String(code.c_str(), Py_file_input, main_dict, main_dict);

  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  Py_DECREF(result);
  return true;
}

bool PythonAPI::register_keybind(const std::string &key_str,
                                 const std::string &mode,
                                 const std::string &callback) {
  Keybind kb;
  kb.mode = mode;
  kb.callback = callback;
  kb.ctrl = false;
  kb.shift = false;
  kb.alt = false;

  std::string key_lower = key_str;
  std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(),
                 ::tolower);

  size_t pos = key_lower.find("ctrl+");
  if (pos != std::string::npos) {
    kb.ctrl = true;
    key_lower.erase(pos, 5);
  }

  pos = key_lower.find("shift+");
  if (pos != std::string::npos) {
    kb.shift = true;
    key_lower.erase(pos, 6);
  }

  pos = key_lower.find("alt+");
  if (pos != std::string::npos) {
    kb.alt = true;
    key_lower.erase(pos, 4);
  }

  // Parse the actual key
  if (key_lower == "escape" || key_lower == "esc") {
    kb.key = 27;
  } else if (key_lower.length() == 1) {
    kb.key = key_lower[0];
    if (kb.ctrl && kb.key >= 'a' && kb.key <= 'z') {
      kb.key = kb.key - 'a' + 1;
    }
  } else if (key_lower == "enter" || key_lower == "return") {
    kb.key = '\n';
  } else if (key_lower == "tab") {
    kb.key = '\t';
  } else if (key_lower == "backspace") {
    kb.key = 127;
  } else {
    // Unknown key
    return false;
  }

  registered_keybinds.push_back(kb);
  return true;
}

void PythonAPI::register_command(const std::string &name,
                                 const std::string &callback) {
  if (editor)
    editor->register_command(name, callback);
}

void PythonAPI::py_show_input_prompt(const std::string &msg,
                                     const std::string &callback) {
  if (editor)
    editor->show_input_prompt(msg, callback);
}

bool PythonAPI::handle_keybind(int key, bool ctrl, bool shift, bool alt,
                               const std::string &mode) {
  if (!python_initialized)
    return false;

  for (const auto &kb : registered_keybinds) {
    if (kb.key == key && kb.ctrl == ctrl && kb.shift == shift &&
        kb.alt == alt && (kb.mode == "all" || kb.mode == mode)) {

      return execute_python_callback(kb.callback, key, ctrl, shift);
    }
  }

  return false;
}

bool PythonAPI::execute_python_callback(const std::string &callback, int key,
                                        bool ctrl, bool shift) {
  if (!python_initialized)
    return false;

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  // Find callback in __main__ namespace
  PyObject *func = PyDict_GetItemString(main_dict, callback.c_str());
  if (!func || !PyCallable_Check(func)) {
    return false;
  }

  // Call it
  PyObject *args = PyTuple_New(0);
  PyObject *result = PyObject_CallObject(func, args);

  Py_DECREF(args);

  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  bool handled = true;
  if (PyBool_Check(result)) {
    handled = (result == Py_True);
  }

  Py_DECREF(result);
  return handled;
}

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
