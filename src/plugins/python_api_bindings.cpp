// Python.h MUST be first — its macros must precede any C++ standard headers
#include <Python.h>
#include "python_api.h"
#include "editor.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

// Note: python_api_impl.cpp is #included at the bottom of this file so that
// both halves share the same translation unit (they need the static helpers
// and g_python_api that are defined here).

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
  dirs.emplace_back(fs::current_path() / "python");

  fs::path exe_path = get_executable_path();
  if (!exe_path.empty()) {
    fs::path exe_dir = exe_path.parent_path();
    dirs.emplace_back(exe_dir / "python");
    dirs.emplace_back(exe_dir.parent_path() / "share" / "jot" / "python");
  }
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
  PyRun_SimpleString(("import sys\nsys.path.append('" + escaped + "')\n").c_str());
}

// Global instance pointer for C wrappers
static PythonAPI *g_python_api = nullptr;

// --- C Wrappers for Python ---

static PyObject *py_enter_normal_mode(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_enter_normal_mode();
  Py_RETURN_NONE;
}

static PyObject *py_enter_insert_mode(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_enter_insert_mode();
  Py_RETURN_NONE;
}

static PyObject *py_enter_visual_mode(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_enter_visual_mode();
  Py_RETURN_NONE;
}

static PyObject *py_move_cursor(PyObject *self, PyObject *args) {
  int dx, dy;
  if (!PyArg_ParseTuple(args, "ii", &dx, &dy))
    return nullptr;
  if (g_python_api)
    g_python_api->py_move_cursor(dx, dy);
  Py_RETURN_NONE;
}

static PyObject *py_insert_char(PyObject *self, PyObject *args) {
  char *c_str;
  if (!PyArg_ParseTuple(args, "s", &c_str))
    return nullptr;
  if (g_python_api && c_str[0] != '\0')
    g_python_api->py_insert_char(c_str[0]);
  Py_RETURN_NONE;
}

static PyObject *py_delete_char(PyObject *self, PyObject *args) {
  int forward;
  if (!PyArg_ParseTuple(args, "p", &forward))
    return nullptr;
  if (g_python_api)
    g_python_api->py_delete_char(forward);
  Py_RETURN_NONE;
}

static PyObject *py_save_file(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_save_file();
  Py_RETURN_NONE;
}

static PyObject *py_open_file(PyObject *self, PyObject *args) {
  char *path;
  if (!PyArg_ParseTuple(args, "s", &path))
    return nullptr;
  if (g_python_api)
    g_python_api->py_open_file(path);
  Py_RETURN_NONE;
}

static PyObject *py_show_message(PyObject *self, PyObject *args) {
  char *msg;
  if (!PyArg_ParseTuple(args, "s", &msg))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_message(msg);
  Py_RETURN_NONE;
}

static PyObject *py_get_mode(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyUnicode_FromString(g_python_api->py_get_mode().c_str());
  return PyUnicode_FromString("insert");
}

static PyObject *py_get_cursor_x(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyLong_FromLong(g_python_api->py_get_cursor_x());
  return PyLong_FromLong(0);
}

static PyObject *py_get_cursor_y(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyLong_FromLong(g_python_api->py_get_cursor_y());
  return PyLong_FromLong(0);
}

static PyObject *py_get_line(PyObject *self, PyObject *args) {
  int line;
  if (!PyArg_ParseTuple(args, "i", &line))
    return nullptr;
  if (g_python_api)
    return PyUnicode_FromString(g_python_api->py_get_line(line).c_str());
  return PyUnicode_FromString("");
}

static PyObject *py_get_line_count(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyLong_FromLong(g_python_api->py_get_line_count());
  return PyLong_FromLong(0);
}

static PyObject *py_set_theme_color(PyObject *self, PyObject *args) {
  char *name;
  int fg, bg;
  if (!PyArg_ParseTuple(args, "sii", &name, &fg, &bg))
    return nullptr;
  if (g_python_api)
    g_python_api->py_set_theme_color(name, fg, bg);
  Py_RETURN_NONE;
}

static PyObject *py_move_line_up(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_move_line_up();
  Py_RETURN_NONE;
}

static PyObject *py_move_line_down(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_move_line_down();
  Py_RETURN_NONE;
}

static PyObject *py_show_popup(PyObject *self, PyObject *args) {
  char *text;
  int x, y;
  if (!PyArg_ParseTuple(args, "sii", &text, &x, &y))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_popup(text, x, y);
  Py_RETURN_NONE;
}

static PyObject *py_hide_popup(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_hide_popup();
  Py_RETURN_NONE;
}

static PyObject *py_clear_diagnostics(PyObject *self, PyObject *args) {
  char *path;
  if (!PyArg_ParseTuple(args, "s", &path))
    return nullptr;
  if (g_python_api)
    g_python_api->py_clear_diagnostics(path);
  Py_RETURN_NONE;
}

static PyObject *py_add_diagnostic(PyObject *self, PyObject *args) {
  char *path;
  char *msg;
  int line, col, end_line, end_col, severity;
  if (!PyArg_ParseTuple(args, "siiiisi", &path, &line, &col, &end_line,
                        &end_col, &msg, &severity))
    return nullptr;
  if (g_python_api)
    g_python_api->py_add_diagnostic(path, line, col, end_line, end_col, msg,
                                    severity);
  Py_RETURN_NONE;
}

static PyObject *py_set_diagnostics(PyObject *self, PyObject *args) {
  char *path;
  PyObject *list;
  if (!PyArg_ParseTuple(args, "sO", &path, &list))
    return nullptr;
  if (!PyList_Check(list)) {
    PyErr_SetString(PyExc_TypeError, "Expected a list of diagnostics");
    return nullptr;
  }

  std::vector<Diagnostic> diagnostics;
  int size = PyList_Size(list);
  for (int i = 0; i < size; i++) {
    PyObject *item = PyList_GetItem(list, i);
    if (!PyDict_Check(item))
      continue;

    Diagnostic d;
    // Helper to get int from dict
    PyObject *pLine = PyDict_GetItemString(item, "line");
    d.line = pLine ? PyLong_AsLong(pLine) : 0;

    PyObject *pCol = PyDict_GetItemString(item, "col");
    d.col = pCol ? PyLong_AsLong(pCol) : 0;

    PyObject *pEndLine = PyDict_GetItemString(item, "end_line");
    d.end_line = pEndLine ? PyLong_AsLong(pEndLine) : d.line;

    PyObject *pEndCol = PyDict_GetItemString(item, "end_col");
    d.end_col = pEndCol ? PyLong_AsLong(pEndCol) : d.col;

    PyObject *pSeverity = PyDict_GetItemString(item, "severity");
    d.severity = pSeverity ? PyLong_AsLong(pSeverity) : 1;

    PyObject *pMsg = PyDict_GetItemString(item, "message");
    if (pMsg) {
      d.message = PyUnicode_AsUTF8(pMsg);
    }
    diagnostics.push_back(d);
  }

  if (g_python_api)
    g_python_api->py_set_diagnostics(path, diagnostics);
  Py_RETURN_NONE;
}

// Register Keybind from Python
static PyObject *py_register_keybind(PyObject *self, PyObject *args) {
  char *key, *mode, *cb;
  if (!PyArg_ParseTuple(args, "sss", &key, &mode, &cb))
    return nullptr;
  if (g_python_api)
    g_python_api->register_keybind(key, mode, cb);
  Py_RETURN_NONE;
}

static PyObject *py_register_command(PyObject *self, PyObject *args) {
  char *name, *cb;
  if (!PyArg_ParseTuple(args, "ss", &name, &cb))
    return nullptr;
  if (g_python_api)
    g_python_api->register_command(name, cb);
  Py_RETURN_NONE;
}

static PyObject *py_show_input_prompt(PyObject *self, PyObject *args) {
  char *msg, *cb;
  if (!PyArg_ParseTuple(args, "ss", &msg, &cb))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_input_prompt(msg, cb);
  Py_RETURN_NONE;
}

static PyMethodDef JotMethods[] = {
    {"enter_normal_mode", py_enter_normal_mode, METH_VARARGS,
     "Switch to normal mode"},
    {"enter_insert_mode", py_enter_insert_mode, METH_VARARGS,
     "Switch to insert mode"},
    {"enter_visual_mode", py_enter_visual_mode, METH_VARARGS,
     "Switch to visual mode"},
    {"move_cursor", py_move_cursor, METH_VARARGS, "Move cursor"},
    {"insert_char", py_insert_char, METH_VARARGS, "Insert char"},
    {"delete_char", py_delete_char, METH_VARARGS, "Delete char"},
    {"save_file", py_save_file, METH_VARARGS, "Save file"},
    {"open_file", py_open_file, METH_VARARGS, "Open file"},
    {"show_message", py_show_message, METH_VARARGS, "Show message"},
    {"get_mode", py_get_mode, METH_VARARGS, "Get current mode"},
    {"get_cursor_x", py_get_cursor_x, METH_VARARGS, "Get cursor X"},
    {"get_cursor_y", py_get_cursor_y, METH_VARARGS, "Get cursor Y"},
    {"get_line", py_get_line, METH_VARARGS, "Get line content"},
    {"get_line_count", py_get_line_count, METH_VARARGS, "Get line count"},
    {"set_theme_color", py_set_theme_color, METH_VARARGS, "Set theme color"},
    {"move_line_up", py_move_line_up, METH_VARARGS, "Move line up"},
    {"move_line_down", py_move_line_down, METH_VARARGS, "Move line down"},
    {"show_popup", py_show_popup, METH_VARARGS, "Show popup"},
    {"hide_popup", py_hide_popup, METH_VARARGS, "Hide popup"},
    {"clear_diagnostics", py_clear_diagnostics, METH_VARARGS,
     "Clear diagnostics"},
    {"set_diagnostics", py_set_diagnostics, METH_VARARGS,
     "Set diagnostics"}, // New
    {"add_diagnostic", py_add_diagnostic, METH_VARARGS, "Add diagnostic"},
    {"register_keybind", py_register_keybind, METH_VARARGS,
     "Register key binding"},
    {"register_command", py_register_command, METH_VARARGS, "Register command"},
    {"show_input", py_show_input_prompt, METH_VARARGS, "Show input prompt"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef jot_module = {
    PyModuleDef_HEAD_INIT, "_jot_internal", "Jot Editor Internal API", -1,
    JotMethods};

static PyObject *PyInit_jot_api(void) {
  return PyModule_Create(&jot_module);
}

// Include the PythonAPI class implementation in this same translation unit
// so it can access the static helpers and g_python_api defined above.
#include "python_api_impl.cpp"
