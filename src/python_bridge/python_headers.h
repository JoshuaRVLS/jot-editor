#ifndef JOT_PYTHON_BRIDGE_PYTHON_HEADERS_H
#define JOT_PYTHON_BRIDGE_PYTHON_HEADERS_H

#if defined(_MSC_VER) && !defined(Py_NO_LINK_LIB)
#define Py_NO_LINK_LIB
#endif

#if defined(_WIN32) && defined(_DEBUG)
#define JOT_RESTORE_MSVC_DEBUG
#undef _DEBUG
#endif

#include <Python.h>

#ifdef JOT_RESTORE_MSVC_DEBUG
#define _DEBUG
#undef JOT_RESTORE_MSVC_DEBUG
#endif

#endif // JOT_PYTHON_BRIDGE_PYTHON_HEADERS_H
