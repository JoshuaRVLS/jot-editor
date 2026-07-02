// Python headers must be first so their macros precede C++ standard headers.
#include "python_bridge/python_headers.h"

#include "python_bridge/api.h"
#include "python_bridge/api_internal.h"

#include <methodobject.h>
#include <modsupport.h>
#include <object.h>
#include <pytypedefs.h>
#include <unicodeobject.h>

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

static PyObject *py_register_command(PyObject *, PyObject *args) {
  char *name;
  char *callback;
  char *detail;
  if (!PyArg_ParseTuple(args, "sss", &name, &callback, &detail))
    return nullptr;
  if (g_python_api)
    g_python_api->py_register_command(name, callback, detail);
  Py_RETURN_NONE;
}

static PyObject *py_register_keymap(PyObject *, PyObject *args) {
  char *key;
  char *callback;
  char *command;
  char *detail;
  char *mode;
  if (!PyArg_ParseTuple(args, "sssss", &key, &callback, &command, &detail,
                        &mode))
    return nullptr;
  if (g_python_api)
    g_python_api->py_register_keymap(key, callback, command, detail, mode);
  Py_RETURN_NONE;
}

static PyObject *py_register_autocmd(PyObject *, PyObject *args) {
  char *event;
  char *callback;
  if (!PyArg_ParseTuple(args, "ss", &event, &callback))
    return nullptr;
  if (g_python_api)
    g_python_api->py_register_autocmd(event, callback);
  Py_RETURN_NONE;
}

static PyObject *py_register_panel(PyObject *, PyObject *args) {
  char *name;
  char *callback;
  char *title;
  if (!PyArg_ParseTuple(args, "sss", &name, &callback, &title))
    return nullptr;
  if (g_python_api)
    g_python_api->py_register_panel(name, callback, title);
  Py_RETURN_NONE;
}

static PyObject *py_get_current_buffer(PyObject *, PyObject *) {
  if (!g_python_api)
    return PyUnicode_FromString("");
  return PyUnicode_FromString(g_python_api->py_get_current_buffer().c_str());
}

static PyObject *py_set_current_buffer(PyObject *, PyObject *args) {
  char *text;
  if (!PyArg_ParseTuple(args, "s", &text))
    return nullptr;
  if (g_python_api)
    g_python_api->py_set_current_buffer(text);
  Py_RETURN_NONE;
}

static PyObject *py_get_selection(PyObject *, PyObject *) {
  if (!g_python_api)
    return PyUnicode_FromString("");
  return PyUnicode_FromString(g_python_api->py_get_selection().c_str());
}

static PyObject *py_replace_selection(PyObject *, PyObject *args) {
  char *text;
  if (!PyArg_ParseTuple(args, "s", &text))
    return nullptr;
  if (g_python_api)
    g_python_api->py_replace_selection(text);
  Py_RETURN_NONE;
}

static PyObject *py_insert_text(PyObject *, PyObject *args) {
  char *text;
  if (!PyArg_ParseTuple(args, "s", &text))
    return nullptr;
  if (g_python_api)
    g_python_api->py_insert_text(text);
  Py_RETURN_NONE;
}

static PyObject *py_get_cursor(PyObject *, PyObject *) {
  if (!g_python_api)
    return PyUnicode_FromString("0:0");
  return PyUnicode_FromString(g_python_api->py_get_cursor().c_str());
}

static PyObject *py_set_cursor(PyObject *, PyObject *args) {
  int line;
  int col;
  if (!PyArg_ParseTuple(args, "ii", &line, &col))
    return nullptr;
  if (g_python_api)
    g_python_api->py_set_cursor(line, col);
  Py_RETURN_NONE;
}

static PyObject *py_current_file(PyObject *, PyObject *) {
  if (!g_python_api)
    return PyUnicode_FromString("");
  return PyUnicode_FromString(g_python_api->py_current_file().c_str());
}

static PyObject *py_open_file(PyObject *, PyObject *args) {
  char *path;
  if (!PyArg_ParseTuple(args, "s", &path))
    return nullptr;
  if (g_python_api)
    g_python_api->py_open_file(path);
  Py_RETURN_NONE;
}

static PyObject *py_save_current_file(PyObject *, PyObject *) {
  if (g_python_api)
    g_python_api->py_save_current_file();
  Py_RETURN_NONE;
}

static PyObject *py_execute_command(PyObject *, PyObject *args) {
  char *command;
  if (!PyArg_ParseTuple(args, "s", &command))
    return nullptr;
  if (g_python_api)
    g_python_api->py_execute_command(command);
  Py_RETURN_NONE;
}

static PyObject *py_run_job(PyObject *, PyObject *args) {
  char *command;
  char *cwd;
  char *label;
  if (!PyArg_ParseTuple(args, "sss", &command, &cwd, &label))
    return nullptr;
  if (g_python_api)
    g_python_api->py_run_job(command, cwd, label);
  Py_RETURN_NONE;
}

static PyObject *py_show_picker(PyObject *, PyObject *args) {
  char *title;
  char *items_callback;
  char *select_callback;
  if (!PyArg_ParseTuple(args, "sss", &title, &items_callback,
                        &select_callback))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_picker(title, items_callback, select_callback);
  Py_RETURN_NONE;
}

static PyObject *py_show_panel(PyObject *, PyObject *args) {
  char *name;
  if (!PyArg_ParseTuple(args, "s", &name))
    return nullptr;
  if (g_python_api)
    g_python_api->py_show_panel(name);
  Py_RETURN_NONE;
}

static PyMethodDef JotMethods[] = {
    {"show_message", py_show_message, METH_VARARGS, "Show a status message"},
    {"set_theme_color", py_set_theme_color, METH_VARARGS,
     "Set a theme color slot"},
    {"register_command", py_register_command, METH_VARARGS,
     "Register plugin command"},
    {"register_keymap", py_register_keymap, METH_VARARGS,
     "Register plugin keymap"},
    {"register_autocmd", py_register_autocmd, METH_VARARGS,
     "Register plugin autocmd"},
    {"register_panel", py_register_panel, METH_VARARGS,
     "Register plugin panel"},
    {"get_current_buffer", py_get_current_buffer, METH_NOARGS,
     "Read current buffer"},
    {"set_current_buffer", py_set_current_buffer, METH_VARARGS,
     "Replace current buffer"},
    {"get_selection", py_get_selection, METH_NOARGS, "Read selection"},
    {"replace_selection", py_replace_selection, METH_VARARGS,
     "Replace selection"},
    {"insert_text", py_insert_text, METH_VARARGS, "Insert text"},
    {"get_cursor", py_get_cursor, METH_NOARGS, "Read cursor"},
    {"set_cursor", py_set_cursor, METH_VARARGS, "Set cursor"},
    {"current_file", py_current_file, METH_NOARGS, "Current file path"},
    {"open_file", py_open_file, METH_VARARGS, "Open file"},
    {"save_current_file", py_save_current_file, METH_NOARGS,
     "Save current file"},
    {"execute_command", py_execute_command, METH_VARARGS,
     "Execute ex command"},
    {"run_job", py_run_job, METH_VARARGS, "Run terminal job"},
    {"show_picker", py_show_picker, METH_VARARGS, "Show plugin picker"},
    {"show_panel", py_show_panel, METH_VARARGS, "Show plugin panel"},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef jot_module = {
    PyModuleDef_HEAD_INIT, "_jot_internal", "Jot theme runtime API", -1,
    JotMethods};

static PyObject *PyInit_jot_api(void) { return PyModule_Create(&jot_module); }

namespace PythonBridgeInternal {
bool register_internal_module() {
  return PyImport_AppendInittab("_jot_internal", PyInit_jot_api) != -1;
}

void set_active_api(PythonAPI *api) { g_python_api = api; }
} // namespace PythonBridgeInternal
