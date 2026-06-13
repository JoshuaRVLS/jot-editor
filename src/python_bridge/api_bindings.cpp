// Python.h must be first so its macros precede C++ standard headers.
#include <Python.h>

#include "editor.h"
#include "python_bridge/api.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// Note: api_impl.cpp is included at the bottom of this file so the
// implementation can access the static helpers and g_python_api.

static fs::path get_executable_path() {
  std::vector<char> buffer(4096, '\0');
  ssize_t len = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (len <= 0) {
    return {};
  }
  buffer[static_cast<size_t>(len)] = '\0';
  return fs::path(buffer.data());
}

static std::vector<fs::path> get_runtime_python_dirs() {
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

static fs::path get_user_config_root() {
  const char *home = getenv("HOME");
  if (!home) {
    return {};
  }
  return fs::path(home) / ".config" / "jot";
}

static void append_python_path(const fs::path &path) {
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

static PythonAPI *g_python_api = nullptr;

static PyObject *py_show_message(PyObject *self, PyObject *args) {
  (void)self;
  char *msg;
  if (!PyArg_ParseTuple(args, "s", &msg))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_message(msg);
  Py_RETURN_NONE;
}

static PyObject *py_set_theme_color(PyObject *self, PyObject *args) {
  (void)self;
  char *name;
  int fg, bg;
  if (!PyArg_ParseTuple(args, "sii", &name, &fg, &bg))
    return nullptr;
  if (g_python_api)
    g_python_api->py_set_theme_color(name, fg, bg);
  Py_RETURN_NONE;
}

static PyMethodDef JotMethods[] = {
    {"show_message", py_show_message, METH_VARARGS, "Show a status message"},
    {"set_theme_color", py_set_theme_color, METH_VARARGS,
     "Set a theme color slot"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef jot_module = {
    PyModuleDef_HEAD_INIT, "_jot_internal", "Jot theme runtime API", -1,
    JotMethods};

static PyObject *PyInit_jot_api(void) { return PyModule_Create(&jot_module); }

#include "api_impl.cpp"
