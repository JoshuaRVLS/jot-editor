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

struct PluginKeymap {
  std::string key;
  std::string callback;
  std::string command;
  std::string detail;
  std::string mode;
};

struct PluginAutocmd {
  std::string event;
  std::string callback;
};

struct PluginPanel {
  std::string name;
  std::string callback;
  std::string title;
};

struct PluginLoadStatus {
  std::string name;
  std::string path;
  bool loaded;
  std::string error;
};

// Forward declaration
class Editor;

class PythonAPI {
private:
  Editor *editor;
  std::vector<PluginCommand> plugin_commands;
  std::vector<PluginKeymap> plugin_keymaps;
  std::vector<PluginAutocmd> plugin_autocmds;
  std::vector<PluginPanel> plugin_panels;
  std::vector<PluginLoadStatus> plugin_load_status;

  void *py_module; // PyObject* (using void* to avoid including Python.h in
                   // header)
  bool python_initialized;

  bool import_jot_api_module();
  void clear_plugin_state();
  bool load_script_path(const std::string &module_name,
                        const std::string &path);
  bool call_callback_string(const std::string &callback,
                            const std::string &arg);

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
  void reload_plugins();
  bool run_plugin_command(const std::string &name, const std::string &arg);
  bool run_plugin_keymap(const std::string &key);
  void fire_autocmd(const std::string &event, const std::string &filepath = "",
                    int buffer = -1);
  std::vector<std::string> plugin_panel_lines(const std::string &name);
  std::vector<std::string> plugin_picker_items(const std::string &callback);
  bool run_plugin_callback(const std::string &callback,
                           const std::string &arg = "");
  void py_register_command(const std::string &name,
                           const std::string &callback,
                           const std::string &detail);
  void py_register_keymap(const std::string &key, const std::string &callback,
                          const std::string &command,
                          const std::string &detail,
                          const std::string &mode);
  void py_register_autocmd(const std::string &event,
                           const std::string &callback);
  void py_register_panel(const std::string &name,
                         const std::string &callback,
                         const std::string &title);

  const std::vector<PluginCommand> &commands() const { return plugin_commands; }
  const std::vector<PluginKeymap> &keymaps() const { return plugin_keymaps; }
  const std::vector<PluginAutocmd> &autocmds() const { return plugin_autocmds; }
  const std::vector<PluginPanel> &panels() const { return plugin_panels; }
  const std::vector<PluginLoadStatus> &load_status() const {
    return plugin_load_status;
  }
  std::string py_get_current_buffer();
  void py_set_current_buffer(const std::string &text);
  std::string py_get_selection();
  void py_replace_selection(const std::string &text);
  void py_insert_text(const std::string &text);
  std::string py_get_cursor();
  void py_set_cursor(int line, int col);
  std::string py_current_file();
  void py_open_file(const std::string &path);
  void py_save_current_file();
  void py_execute_command(const std::string &command);
  void py_run_job(const std::string &command, const std::string &cwd,
                  const std::string &label);
  void py_show_picker(const std::string &title,
                      const std::string &items_callback,
                      const std::string &select_callback);
  void py_show_panel(const std::string &name);
};

#endif
