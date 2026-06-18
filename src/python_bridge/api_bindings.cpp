// Python.h must be first so its macros precede C++ standard headers.
#include <Python.h>

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

static PyMethodDef JotMethods[] = {
    {"show_message", py_show_message, METH_VARARGS, "Show a status message"},
    {"set_theme_color", py_set_theme_color, METH_VARARGS,
     "Set a theme color slot"},
    {"register_command", py_register_command, METH_VARARGS,
     "Register plugin command"},
    {"get_current_buffer", py_get_current_buffer, METH_NOARGS,
     "Read current buffer"},
    {"set_current_buffer", py_set_current_buffer, METH_VARARGS,
     "Replace current buffer"},
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
