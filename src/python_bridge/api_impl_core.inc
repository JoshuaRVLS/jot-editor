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

  // Register built-in module before Py_Initialize so jot_api can import it.
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

  fs::path config_root = get_user_config_root();
  if (!config_root.empty()) {
    fs::create_directories(config_root / "configs" / "colors");
    fs::create_directories(config_root / "themes");

    append_python_path(config_root / "configs");
    append_python_path(config_root / "configs" / "colors");
    append_python_path(config_root / "themes");
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

  return true;
}

void PythonAPI::cleanup() {
  if (py_module) {
    Py_DECREF(reinterpret_cast<PyObject *>(py_module));
    py_module = nullptr;
  }

  if (python_initialized) {
    Py_Finalize();
    python_initialized = false;
  }
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
  return true;
}

void PythonAPI::on_buffer_open(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_open(filepath);
}

void PythonAPI::on_buffer_change(const std::string &filepath,
                                 const std::string &content) {
  (void)content;
  if (editor)
    editor->notify_lsp_change(filepath);
}

void PythonAPI::on_buffer_save(const std::string &filepath) {
  if (editor)
    editor->notify_lsp_save(filepath);
}
