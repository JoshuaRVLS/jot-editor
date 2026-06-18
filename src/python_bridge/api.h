#ifndef PYTHON_API_H
#define PYTHON_API_H

#include "text_features.h"
#include <string>
#include <vector>

struct PluginCommand {
  std::string name;
  std::string callback;
  std::string detail;
};

// Forward declaration
class Editor;

class PythonAPI {
private:
  Editor *editor;
  std::vector<PluginCommand> plugin_commands;

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

  // Plugin system
  void load_plugins();
  bool run_plugin_command(const std::string &name, const std::string &arg);
  void py_register_command(const std::string &name,
                           const std::string &callback,
                           const std::string &detail);

  const std::vector<PluginCommand> &commands() const { return plugin_commands; }
  std::string py_get_current_buffer();
  void py_set_current_buffer(const std::string &text);
};

#endif
