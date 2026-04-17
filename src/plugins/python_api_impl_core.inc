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

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

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
  if (!python_initialized)
    return false;

  PyObject *main_module = PyImport_AddModule("__main__");
  PyObject *main_dict = PyModule_GetDict(main_module);

  PyObject *func = PyDict_GetItemString(main_dict, callback.c_str());
  if (!func || !PyCallable_Check(func)) {
    return false;
  }

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
