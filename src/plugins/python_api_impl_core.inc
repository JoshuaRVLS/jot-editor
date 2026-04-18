namespace {
fs::path resolve_plugin_entrypoint(const fs::path &entry) {
  std::error_code ec;
  if (!fs::exists(entry, ec)) {
    return {};
  }

  if (fs::is_regular_file(entry, ec) && entry.extension() == ".py") {
    return entry;
  }

  if (!fs::is_directory(entry, ec)) {
    return {};
  }

  const fs::path manifest_entry = entry / "plugin.py";
  if (fs::exists(manifest_entry, ec) && fs::is_regular_file(manifest_entry, ec)) {
    return manifest_entry;
  }

  const fs::path package_entry = entry / "__init__.py";
  if (fs::exists(package_entry, ec) && fs::is_regular_file(package_entry, ec)) {
    return package_entry;
  }

  return {};
}

std::vector<fs::path> sorted_plugin_entries(const fs::path &dir) {
  std::vector<fs::path> entries;
  std::error_code ec;
  if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
    return entries;
  }

  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    const std::string filename = entry.path().filename().string();
    if (!filename.empty() && filename[0] == '.') {
      continue;
    }
    fs::path resolved = resolve_plugin_entrypoint(entry.path());
    if (!resolved.empty()) {
      entries.push_back(resolved);
    }
  }

  std::sort(entries.begin(), entries.end(),
            [](const fs::path &a, const fs::path &b) {
              return a.lexically_normal().string() < b.lexically_normal().string();
            });
  return entries;
}
} // namespace

PythonAPI::PythonAPI(Editor *ed)
    : editor(ed), py_module(nullptr), py_buffer_open_hook(nullptr),
      py_buffer_change_hook(nullptr), py_buffer_save_hook(nullptr),
      py_internal_callback_hook(nullptr), py_event_hook(nullptr),
      py_reset_runtime_hook(nullptr), python_initialized(false) {
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
  if (PyImport_AppendInittab("_jot_internal", PyInit_jot_api) == -1) {
    std::cerr << "Failed to register _jot_internal module" << std::endl;
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
    if (fs::exists(dir / "jot_api.py")) {
      import_jot_api_module();
      break;
    }
  }

  if (!py_module && !import_jot_api_module()) {
    std::cerr << "Failed to import jot_api module" << std::endl;
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  for (const auto &dir : runtime_dirs) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
      continue;
    }
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

    plugin_entry_files.clear();
    plugin_directories.clear();
    loaded_plugins.clear();

    plugin_entry_files.push_back((config_root / "config.py").string());
    plugin_entry_files.push_back((config_root / "init.py").string());
    plugin_entry_files.push_back((config_root / "configs" / "init.py").string());
    plugin_directories.push_back((config_root / "plugins").string());
    plugin_directories.push_back((config_root / "configs" / "plugins").string());

    for (const auto &entry : plugin_entry_files) {
      load_plugin(entry);
    }
    for (const auto &dir : plugin_directories) {
      load_plugins(dir);
    }

    if (!fs::exists(config_root / "config.py") &&
        !fs::exists(config_root / "init.py") &&
        !fs::exists(config_root / "configs" / "init.py")) {
      load_plugin((config_root / "configs" / "colors" / "jot_nvim.py").string());
    }
  }

  on_editor_ready();
  return true;
}

void PythonAPI::cleanup() {
  if (py_buffer_open_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_buffer_open_hook));
    py_buffer_open_hook = nullptr;
  }
  if (py_buffer_change_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_buffer_change_hook));
    py_buffer_change_hook = nullptr;
  }
  if (py_buffer_save_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_buffer_save_hook));
    py_buffer_save_hook = nullptr;
  }
  if (py_internal_callback_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_internal_callback_hook));
    py_internal_callback_hook = nullptr;
  }
  if (py_event_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_event_hook));
    py_event_hook = nullptr;
  }
  if (py_reset_runtime_hook) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_reset_runtime_hook));
    py_reset_runtime_hook = nullptr;
  }
  if (py_module) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_module));
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

bool PythonAPI::import_jot_api_module() {
  if (py_module) {
    return true;
  }

  PyObject *module = PyImport_ImportModule("jot_api");
  if (!module) {
    return false;
  }

  py_module = module;
  refresh_python_hooks();
  return true;
}

void PythonAPI::refresh_python_hooks() {
  auto reset_hook = [](void *&hook) {
    if (hook) {
      Py_DECREF(reinterpret_cast<PyObject *>(hook));
      hook = nullptr;
    }
  };

  reset_hook(py_buffer_open_hook);
  reset_hook(py_buffer_change_hook);
  reset_hook(py_buffer_save_hook);
  reset_hook(py_internal_callback_hook);
  reset_hook(py_event_hook);
  reset_hook(py_reset_runtime_hook);

  if (!py_module) {
    return;
  }

  PyObject *module = reinterpret_cast<PyObject *>(py_module);

  auto load_hook = [module](const char *name) -> void * {
    PyObject *hook = PyObject_GetAttrString(module, name);
    if (!hook) {
      PyErr_Clear();
      return nullptr;
    }
    if (!PyCallable_Check(hook)) {
      Py_DECREF(hook);
      return nullptr;
    }
    return hook;
  };

  py_buffer_open_hook = load_hook("_on_buffer_open");
  py_buffer_change_hook = load_hook("_on_buffer_change");
  py_buffer_save_hook = load_hook("_on_buffer_save");
  py_internal_callback_hook = load_hook("_internal_call_callback");
  py_event_hook = load_hook("_emit_event");
  py_reset_runtime_hook = load_hook("_reset_runtime_state");
}

void PythonAPI::call_python_hook(void *hook, const std::string &arg) {
  if (!hook) {
    return;
  }

  PyObject *args = PyTuple_New(1);
  if (!args) {
    PyErr_Clear();
    return;
  }

  PyObject *value = PyUnicode_FromString(arg.c_str());
  if (!value) {
    Py_DECREF(args);
    PyErr_Clear();
    return;
  }

  PyTuple_SET_ITEM(args, 0, value);
  PyObject *result =
      PyObject_CallObject(reinterpret_cast<PyObject *>(hook), args);
  if (!result) {
    PyErr_Print();
    PyErr_Clear();
  } else {
    Py_DECREF(result);
  }
  Py_DECREF(args);
}

bool PythonAPI::invoke_python_callback(const std::string &callback,
                                       const std::string &arg,
                                       bool *handled) {
  if (!python_initialized || !py_internal_callback_hook) {
    return false;
  }

  PyObject *args = PyTuple_New(2);
  if (!args) {
    PyErr_Clear();
    return false;
  }

  PyObject *name = PyUnicode_FromString(callback.c_str());
  PyObject *value = PyUnicode_FromString(arg.c_str());
  if (!name || !value) {
    Py_XDECREF(name);
    Py_XDECREF(value);
    Py_DECREF(args);
    PyErr_Clear();
    return false;
  }

  PyTuple_SET_ITEM(args, 0, name);
  PyTuple_SET_ITEM(args, 1, value);
  PyObject *result =
      PyObject_CallObject(reinterpret_cast<PyObject *>(py_internal_callback_hook),
                          args);
  Py_DECREF(args);

  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  if (handled) {
    if (PyBool_Check(result)) {
      *handled = (result == Py_True);
    } else {
      *handled = true;
    }
  }

  Py_DECREF(result);
  return true;
}

void PythonAPI::emit_python_event(const std::string &event_name,
                                  const std::string &payload_json) {
  if (!python_initialized || !py_event_hook) {
    return;
  }

  PyObject *args = PyTuple_New(2);
  if (!args) {
    PyErr_Clear();
    return;
  }

  PyObject *name = PyUnicode_FromString(event_name.c_str());
  PyObject *payload = PyUnicode_FromString(payload_json.c_str());
  if (!name || !payload) {
    Py_XDECREF(name);
    Py_XDECREF(payload);
    Py_DECREF(args);
    PyErr_Clear();
    return;
  }

  PyTuple_SET_ITEM(args, 0, name);
  PyTuple_SET_ITEM(args, 1, payload);
  PyObject *result =
      PyObject_CallObject(reinterpret_cast<PyObject *>(py_event_hook), args);
  Py_DECREF(args);

  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return;
  }
  Py_DECREF(result);
}

void PythonAPI::on_buffer_open(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_open(filepath);
  if (!python_initialized)
    return;
  call_python_hook(py_buffer_open_hook, filepath);
}

void PythonAPI::on_buffer_change(const std::string &filepath,
                                 const std::string &content) {
  (void)content;
  if (editor)
    editor->notify_lsp_change(filepath);
  if (!python_initialized)
    return;
  call_python_hook(py_buffer_change_hook, filepath);
}

void PythonAPI::on_buffer_save(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_save(filepath);
  if (!python_initialized)
    return;
  call_python_hook(py_buffer_save_hook, filepath);
}

void PythonAPI::on_editor_ready() {
  emit_python_event("startup", "{}");
}

void PythonAPI::load_plugins(const std::string &plugin_dir) {
  if (!fs::exists(plugin_dir) || !fs::is_directory(plugin_dir)) {
    fs::create_directories(plugin_dir);
    return;
  }

  for (const auto &entry : sorted_plugin_entries(plugin_dir)) {
    load_plugin(entry.string());
  }
}

bool PythonAPI::load_plugin(const std::string &path) {
  if (!python_initialized)
    return false;

  fs::path plugin_path(path);
  append_python_path(plugin_path.parent_path());

  std::ifstream file(path);
  if (!file.is_open())
    return false;

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string code = buffer.str();

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);
  PyObject *py_file = PyUnicode_FromString(path.c_str());
  if (py_file) {
    PyDict_SetItemString(main_dict, "__file__", py_file);
    Py_DECREF(py_file);
  } else {
    PyErr_Clear();
  }

  std::string package_name;
  if (plugin_path.filename() == "__init__.py") {
    package_name = plugin_path.parent_path().filename().string();
  } else if (plugin_path.filename() == "plugin.py" &&
             fs::exists(plugin_path.parent_path() / "__init__.py")) {
    package_name = plugin_path.parent_path().filename().string();
  }

  PyObject *py_package = PyUnicode_FromString(package_name.c_str());
  if (py_package) {
    PyDict_SetItemString(main_dict, "__package__", py_package);
    Py_DECREF(py_package);
  } else {
    PyErr_Clear();
  }

  PyObject *result =
      PyRun_String(code.c_str(), Py_file_input, main_dict, main_dict);

  if (!result) {
    PyErr_Print();
    PyErr_Clear();
    return false;
  }

  Py_DECREF(result);
  loaded_plugins.push_back(path);
  return true;
}

int PythonAPI::reload_user_plugins() {
  if (!python_initialized) {
    return 0;
  }

  registered_keybinds.clear();
  loaded_plugins.clear();
  if (editor) {
    editor->custom_commands.clear();
  }

  if (py_reset_runtime_hook) {
    PyObject *args = PyTuple_New(0);
    PyObject *result =
        PyObject_CallObject(reinterpret_cast<PyObject *>(py_reset_runtime_hook),
                            args);
    Py_DECREF(args);
    if (!result) {
      PyErr_Print();
      PyErr_Clear();
    } else {
      Py_DECREF(result);
    }
  }

  int loaded = 0;
  for (const auto &entry : plugin_entry_files) {
    if (load_plugin(entry)) {
      loaded++;
    }
  }
  for (const auto &dir : plugin_directories) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
      fs::create_directories(dir, ec);
      continue;
    }
    for (const auto &entry : sorted_plugin_entries(dir)) {
      if (load_plugin(entry.string())) {
        loaded++;
      }
    }
  }

  on_editor_ready();
  return loaded;
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
  bool handled = true;
  (void)key;
  (void)ctrl;
  (void)shift;
  if (!invoke_python_callback(callback, "", &handled)) {
    return false;
  }
  return handled;
}
