#ifndef PYTHON_API_H
#define PYTHON_API_H

#include "text_features.h"
#include <string>
#include <vector>

// Forward declaration
class Editor;

class PythonAPI {
private:
  Editor *editor;
  void *py_module; // PyObject* (using void* to avoid including Python.h in
                   // header)
  bool python_initialized;

  bool import_jot_api_module();

public:
  PythonAPI(Editor *ed);
  ~PythonAPI();

  bool init();
  void cleanup();
  void on_buffer_open(const std::string &filepath);
  void on_buffer_change(const std::string &filepath,
                        const std::string &content);
  void on_buffer_save(const std::string &filepath);

  // Python API functions (called from Python)
  void py_show_message(const std::string &msg);
  void py_set_theme_color(const std::string &name, int fg, int bg);
  bool py_apply_colorscheme(const std::string &name);
  std::vector<std::string> py_list_themes();
};

#endif
