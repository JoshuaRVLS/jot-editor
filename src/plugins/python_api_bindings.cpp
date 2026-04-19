// Python.h MUST be first — its macros must precede any C++ standard headers
#include <Python.h>
#include "python_api.h"
#include "editor.h"
#include "editor_host_api.h"
#include <algorithm>
#include <cctype>
#include <chrono>
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

  // Prefer user-local installed runtime first.
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
    // Then prefer runtime installed next to binary prefix (e.g. /usr/local/share/jot/python).
    push_unique(exe_dir.parent_path() / "share" / "jot" / "python");
    // Dev fallback.
    push_unique(exe_dir.parent_path() / "src" / "python");
  }

  // Final fallback for running from source tree.
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

// Global instance pointer for C wrappers
static PythonAPI *g_python_api = nullptr;

PythonAPI *PythonAPI::active() { return g_python_api; }

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

static PyObject *py_get_current_file(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyUnicode_FromString(g_python_api->py_get_current_file().c_str());
  return PyUnicode_FromString("");
}

static PyObject *py_get_buffer_content(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyUnicode_FromString(g_python_api->py_get_buffer_content().c_str());
  return PyUnicode_FromString("");
}

static PyObject *py_set_buffer_content(PyObject *self, PyObject *args) {
  char *text;
  if (!PyArg_ParseTuple(args, "s", &text))
    return nullptr;
  if (g_python_api)
    g_python_api->py_set_buffer_content(text);
  Py_RETURN_NONE;
}

static PyObject *py_get_selected_text(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyUnicode_FromString(g_python_api->py_get_selected_text().c_str());
  return PyUnicode_FromString("");
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

static PyObject *py_execute_command(PyObject *self, PyObject *args) {
  char *command;
  if (!PyArg_ParseTuple(args, "s", &command))
    return nullptr;
  if (g_python_api)
    g_python_api->py_execute_command(command);
  Py_RETURN_NONE;
}

static PyObject *py_execute_ex_command(PyObject *self, PyObject *args) {
  char *command;
  if (!PyArg_ParseTuple(args, "s", &command))
    return nullptr;
  if (g_python_api)
    g_python_api->py_execute_ex_command(command);
  Py_RETURN_NONE;
}

static PyObject *py_list_buffers(PyObject *self, PyObject *args) {
  if (!g_python_api)
    return PyUnicode_FromString("[]");
  return PyUnicode_FromString(g_python_api->py_list_buffers_json().c_str());
}

static PyObject *py_list_panes(PyObject *self, PyObject *args) {
  if (!g_python_api)
    return PyUnicode_FromString("[]");
  return PyUnicode_FromString(g_python_api->py_list_panes_json().c_str());
}

static PyObject *py_get_layout(PyObject *self, PyObject *args) {
  if (!g_python_api)
    return PyUnicode_FromString("{}");
  return PyUnicode_FromString(g_python_api->py_get_layout_json().c_str());
}

static PyObject *py_switch_buffer(PyObject *self, PyObject *args) {
  int index = -1;
  if (!PyArg_ParseTuple(args, "i", &index))
    return nullptr;
  if (!g_python_api)
    Py_RETURN_FALSE;
  if (g_python_api->py_switch_buffer(index))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject *py_close_buffer(PyObject *self, PyObject *args) {
  int index = -1;
  if (!PyArg_ParseTuple(args, "i", &index))
    return nullptr;
  if (!g_python_api)
    Py_RETURN_FALSE;
  if (g_python_api->py_close_buffer(index))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject *py_new_buffer(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_new_buffer();
  Py_RETURN_NONE;
}

static PyObject *py_split_pane(PyObject *self, PyObject *args) {
  char *direction;
  if (!PyArg_ParseTuple(args, "s", &direction))
    return nullptr;
  if (g_python_api)
    g_python_api->py_split_pane(direction);
  Py_RETURN_NONE;
}

static PyObject *py_resize_pane(PyObject *self, PyObject *args) {
  int delta = 0;
  if (!PyArg_ParseTuple(args, "i", &delta))
    return nullptr;
  if (!g_python_api)
    Py_RETURN_FALSE;
  if (g_python_api->py_resize_pane(delta))
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static PyObject *py_focus_next_pane(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_focus_next_pane();
  Py_RETURN_NONE;
}

static PyObject *py_focus_prev_pane(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_focus_prev_pane();
  Py_RETURN_NONE;
}

static PyObject *py_toggle_sidebar(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_toggle_sidebar();
  Py_RETURN_NONE;
}

static PyObject *py_open_workspace(PyObject *self, PyObject *args) {
  char *path;
  if (!PyArg_ParseTuple(args, "s", &path))
    return nullptr;
  if (g_python_api)
    g_python_api->py_open_workspace(path);
  Py_RETURN_NONE;
}

static PyObject *py_toggle_terminal(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_toggle_terminal();
  Py_RETURN_NONE;
}

static PyObject *py_request_redraw(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_request_redraw();
  Py_RETURN_NONE;
}

static PyObject *py_request_quit(PyObject *self, PyObject *args) {
  int force = 0;
  if (!PyArg_ParseTuple(args, "|p", &force))
    return nullptr;
  if (g_python_api)
    g_python_api->py_request_quit(force != 0);
  Py_RETURN_NONE;
}

static PyObject *py_save_and_quit(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_save_and_quit();
  Py_RETURN_NONE;
}

static PyObject *py_toggle_minimap(PyObject *self, PyObject *args) {
  if (g_python_api)
    g_python_api->py_toggle_minimap();
  Py_RETURN_NONE;
}

static PyObject *py_emit_event(PyObject *self, PyObject *args) {
  char *event_name;
  char *payload;
  if (!PyArg_ParseTuple(args, "ss", &event_name, &payload))
    return nullptr;
  if (g_python_api)
    g_python_api->emit_event(event_name, payload);
  Py_RETURN_NONE;
}

static PyObject *py_reload_plugins(PyObject *self, PyObject *args) {
  if (g_python_api)
    return PyLong_FromLong(g_python_api->py_reload_plugins());
  return PyLong_FromLong(0);
}

static PyObject *py_list_plugins(PyObject *self, PyObject *args) {
  if (!g_python_api)
    return PyList_New(0);

  const auto plugins = g_python_api->py_list_plugins();
  PyObject *list = PyList_New((Py_ssize_t)plugins.size());
  for (Py_ssize_t i = 0; i < (Py_ssize_t)plugins.size(); i++) {
    PyObject *item = PyUnicode_FromString(plugins[(size_t)i].c_str());
    PyList_SET_ITEM(list, i, item);
  }
  return list;
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
    {"get_current_file", py_get_current_file, METH_VARARGS,
     "Get current file path"},
    {"get_buffer_content", py_get_buffer_content, METH_VARARGS,
     "Get current buffer content"},
    {"set_buffer_content", py_set_buffer_content, METH_VARARGS,
     "Replace current buffer content"},
    {"get_selected_text", py_get_selected_text, METH_VARARGS,
     "Get selected text"},
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
    {"execute_command", py_execute_command, METH_VARARGS,
     "Execute an ex-style command"},
    {"execute_ex_command", py_execute_ex_command, METH_VARARGS,
     "Execute built-in ex command line"},
    {"list_buffers", py_list_buffers, METH_VARARGS, "List buffers as JSON"},
    {"list_panes", py_list_panes, METH_VARARGS, "List panes as JSON"},
    {"get_layout", py_get_layout, METH_VARARGS, "Get layout as JSON"},
    {"switch_buffer", py_switch_buffer, METH_VARARGS, "Switch active buffer"},
    {"close_buffer", py_close_buffer, METH_VARARGS, "Close buffer by index"},
    {"new_buffer", py_new_buffer, METH_VARARGS, "Create new buffer"},
    {"split_pane", py_split_pane, METH_VARARGS, "Split focused pane"},
    {"resize_pane", py_resize_pane, METH_VARARGS, "Resize focused pane"},
    {"focus_next_pane", py_focus_next_pane, METH_VARARGS,
     "Focus next pane"},
    {"focus_prev_pane", py_focus_prev_pane, METH_VARARGS,
     "Focus previous pane"},
    {"toggle_sidebar", py_toggle_sidebar, METH_VARARGS, "Toggle sidebar"},
    {"open_workspace", py_open_workspace, METH_VARARGS, "Open workspace"},
    {"toggle_terminal", py_toggle_terminal, METH_VARARGS,
     "Toggle integrated terminal"},
    {"request_redraw", py_request_redraw, METH_VARARGS,
     "Request editor redraw"},
    {"request_quit", py_request_quit, METH_VARARGS, "Request editor quit"},
    {"save_and_quit", py_save_and_quit, METH_VARARGS, "Save current file and quit"},
    {"toggle_minimap", py_toggle_minimap, METH_VARARGS, "Toggle minimap"},
    {"emit_event", py_emit_event, METH_VARARGS, "Emit custom editor event"},
    {"reload_plugins", py_reload_plugins, METH_VARARGS,
     "Reload user plugins"},
    {"list_plugins", py_list_plugins, METH_VARARGS, "List loaded plugins"},
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
