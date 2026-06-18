// Python.h must be first so its macros precede C++ standard headers.
#include <Python.h>

#include "editor.h"
#include "host_api.h"
#include "python_bridge/api.h"
#include "python_bridge/api_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {
fs::path get_executable_path() {
  std::vector<char> buffer(4096, '\0');
  ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (len <= 0) {
    return {};
  }
  buffer[static_cast<size_t>(len)] = '\0';
  return fs::path(buffer.data());
}

std::vector<fs::path> get_runtime_python_dirs() {
  std::vector<fs::path> dirs;
  auto push_unique = [&dirs](const fs::path &p) {
    if (p.empty()) {
      return;
    }
    for (const auto &existing : dirs) {
      if (existing == p) {
        return;
      }
    }
    dirs.push_back(p);
  };

  const char *env_runtime = getenv("JOT_PYTHON_PATH");
  if (env_runtime && *env_runtime) {
    push_unique(fs::path(env_runtime));
  }

  const char *xdg_data_home = getenv("XDG_DATA_HOME");
  if (xdg_data_home && *xdg_data_home) {
    push_unique(fs::path(xdg_data_home) / "jot" / "python");
  } else {
    const char *home = getenv("HOME");
    if (home && *home) {
      push_unique(fs::path(home) / ".local" / "share" / "jot" / "python");
    }
  }

  fs::path exe_path = get_executable_path();
  if (!exe_path.empty()) {
    fs::path exe_dir = exe_path.parent_path();
    push_unique(exe_dir.parent_path() / "share" / "jot" / "python");
    push_unique(exe_dir.parent_path() / "src" / "python");
  }

  push_unique(fs::current_path() / "src" / "python");
  return dirs;
}

fs::path get_user_config_root() {
  const char *home = getenv("HOME");
  if (!home) {
    return {};
  }
  return fs::path(home) / ".config" / "jot";
}

void append_python_path(const fs::path &path) {
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return;
  }

  std::string escaped = path.string();
  size_t pos = 0;
  while ((pos = escaped.find('\\', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\\\");
    pos += 2;
  }
  pos = 0;
  while ((pos = escaped.find('\'', pos)) != std::string::npos) {
    escaped.replace(pos, 1, "\\'");
    pos += 2;
  }
  PyRun_SimpleString(
      ("import sys\n"
       "p = '" +
       escaped +
       "'\n"
       "if p not in sys.path:\n"
       "    sys.path.append(p)\n")
          .c_str());
}
} // namespace

PythonAPI::PythonAPI(Editor *ed)
    : editor(ed), py_module(nullptr), python_initialized(false) {
  PythonBridgeInternal::set_active_api(this);
}

PythonAPI::~PythonAPI() {
  cleanup();
  PythonBridgeInternal::set_active_api(nullptr);
}

bool PythonAPI::init() {
  if (python_initialized)
    return true;

  // Register built-in module before Py_Initialize so jot_api can import it.
  if (!PythonBridgeInternal::register_internal_module()) {
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

  load_plugins();

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

void PythonAPI::load_plugins() {
  fs::path root = get_user_config_root() / "plugins";
  fs::create_directories(root);
  append_python_path(root);
  plugin_commands.clear();

  for (const auto &entry : fs::directory_iterator(root)) {
    if (entry.path().extension() != ".py")
      continue;

    std::string module = entry.path().stem().string();
    PyObject *loaded = PyImport_ImportModule(module.c_str());
    if (!loaded) {
      PyErr_Print();
      PyErr_Clear();
      if (editor)
        editor->set_message("Plugin failed: " + module);
      continue;
    }
    Py_DECREF(loaded);
  }
}

void PythonAPI::py_register_command(const std::string &name,
                                    const std::string &callback,
                                    const std::string &detail) {
  if (name.empty() || callback.empty()) {
    return;
  }
  std::string normalized = name;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  for (auto &command : plugin_commands) {
    if (command.name == normalized) {
      command.callback = callback;
      command.detail = detail;
      return;
    }
  }
  plugin_commands.push_back({normalized, callback, detail});
}

bool PythonAPI::run_plugin_command(const std::string &name,
                                   const std::string &arg) {
  for (const auto &cmd : plugin_commands) {
    if (cmd.name != name)
      continue;

    PyObject *api = PyImport_ImportModule("jot_api");
    if (!api) {
      PyErr_Print();
      PyErr_Clear();
      return true;
    }
    PyObject *registry = PyObject_GetAttrString(api, "_plugin_callbacks");
    if (!registry || !PyDict_Check(registry)) {
      Py_XDECREF(registry);
      Py_DECREF(api);
      return true;
    }
    PyObject *fn = PyDict_GetItemString(registry, cmd.callback.c_str());

    if (fn && PyCallable_Check(fn)) {
      PyObject *res = PyObject_CallFunction(fn, "s", arg.c_str());
      if (!res) {
        PyErr_Print();
        PyErr_Clear();
        return true;
      }
      Py_DECREF(res);
    }

    Py_XDECREF(registry);
    Py_XDECREF(api);
    return true;
  }
  return false;
}

std::string PythonAPI::py_get_current_buffer() {
  if (!editor || !editor->host_api) {
    return "";
  }
  return editor->host_api->core.buffer_content();
}

void PythonAPI::py_set_current_buffer(const std::string &text) {
  if (!editor || !editor->host_api) {
    return;
  }
  editor->host_api->core.set_buffer_content(text);
}
