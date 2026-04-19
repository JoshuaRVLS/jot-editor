#ifndef PYTHON_API_H
#define PYTHON_API_H

#include "editor_features.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

// Forward declaration
class Editor;

struct Keybind {
  int key;
  bool ctrl;
  bool shift;
  bool alt;
  std::string mode; // "normal", "insert", "visual", "all"
  std::string callback;
};

class PythonAPI {
private:
  Editor *editor;
  void *py_module; // PyObject* (using void* to avoid including Python.h in
                   // header)
  void *py_buffer_open_hook;   // PyObject*
  void *py_buffer_change_hook; // PyObject*
  void *py_buffer_save_hook;   // PyObject*
  void *py_internal_callback_hook; // PyObject*
  void *py_event_hook;             // PyObject*
  void *py_reset_runtime_hook;     // PyObject*
  void *py_plugin_loaded_hook;     // PyObject*
  void *py_plugin_before_reload_hook; // PyObject*
  void *py_plugins_ready_hook;        // PyObject*
  std::map<std::string, std::function<bool(int, bool, bool)>> keybinds;
  std::vector<Keybind> registered_keybinds;
  std::vector<std::string> plugin_entry_files;
  std::vector<std::string> plugin_directories;
  std::vector<std::string> loaded_plugins;
  bool python_initialized;

  bool init_python();
  void cleanup_python();
  bool import_jot_api_module();
  void refresh_python_hooks();
  void call_python_hook(void *hook, const std::string &arg);
  bool invoke_python_callback(const std::string &callback,
                              const std::string &arg = "",
                              bool *handled = nullptr);
  void notify_python_plugin_loaded(const std::string &path, bool ok,
                                   const std::string &error, double load_ms);
  void notify_python_before_plugin_reload();
  void notify_python_plugins_ready();
  void emit_python_event(const std::string &event_name,
                         const std::string &payload_json);
  bool load_plugin(const std::string &path);
  bool execute_python_callback(const std::string &callback, int key, bool ctrl,
                               bool shift);

public:
  PythonAPI(Editor *ed);
  ~PythonAPI();
  static PythonAPI *active();

  bool init();
  void cleanup();
  void execute_code(const std::string &code);
  bool invoke_callback(const std::string &callback,
                       const std::string &arg = "", bool *handled = nullptr) {
    return invoke_python_callback(callback, arg, handled);
  }
  void on_buffer_open(const std::string &filepath);
  void on_buffer_change(const std::string &filepath,
                        const std::string &content);
  void on_buffer_save(const std::string &filepath);
  void on_editor_ready();
  void emit_event(const std::string &event_name,
                  const std::string &payload_json = "{}");

  // Load plugins from directory
  void load_plugins(const std::string &plugin_dir = "plugins");
  int reload_user_plugins();
  const std::vector<std::string> &get_loaded_plugins() const {
    return loaded_plugins;
  }

  // Register keybind from Python
  bool register_keybind(const std::string &key_str, const std::string &mode,
                        const std::string &callback);

  // Check if a keybind should be handled by Python
  bool handle_keybind(int key, bool ctrl, bool shift, bool alt,
                      const std::string &mode);
  std::vector<std::string>
  command_palette_suggestions(const std::string &query);
  bool command_palette_execute(const std::string &line, bool *handled = nullptr);
  bool feature_should_auto_close(char c);
  char feature_get_closing_bracket(char c);
  bool feature_is_closing_bracket(char c);
  bool feature_should_skip_closing(char c, const std::string &line, int pos);
  bool feature_should_auto_indent(const std::string &line);
  bool feature_should_dedent(const std::string &line);
  int feature_find_matching_bracket(const std::vector<std::string> &lines,
                                    int line, int col, char open, char close);

  void register_command(const std::string &name,
                        const std::string &callback); // New

  // Python API functions (called from Python)
  void py_enter_normal_mode();
  void py_enter_insert_mode();
  void py_enter_visual_mode();
  void py_move_cursor(int dx, int dy);
  void py_insert_char(char c);
  void py_delete_char(bool forward);
  void py_save_file();
  void py_open_file(const std::string &path);
  void py_show_message(const std::string &msg);
  void py_set_theme_color(const std::string &name, int fg, int bg); // New
  void py_move_line_up();
  void py_move_line_down();

  // LSP / UI
  void py_show_popup(const std::string &text, int x, int y);
  void py_show_input_prompt(const std::string &msg,
                            const std::string &callback);
  void py_hide_popup();

  // is hard in C++, stick to JSON
  // string or simple parallel arrays?
  // Actually, passing Python object directly requires PyObject*. We are
  // avoiding that in C++ logic if possible. Let's assume we pass primitives. Or
  // strict JSON string which we parse? Let's use parallel arrays for simplicity
  // or just JSON string if we had a json parser. simpler:
  // py_set_diagnostics(file, vector<int> lines, vector<int> cols,
  // vector<string> msgs, vector<int> severities) Or just one method per
  // diagnostic? slightly inefficient. Let's do: void
  // py_clear_diagnostics(file), void py_add_diagnostic(file, ...).
  void py_clear_diagnostics(const std::string &filepath);
  void py_add_diagnostic(const std::string &filepath, int line, int col,
                         int end_line, int end_col, const std::string &message,
                         int severity);
  void py_set_diagnostics(const std::string &filepath,
                          const std::vector<Diagnostic> &diagnostics); // New

  std::string py_get_mode();
  int py_get_cursor_x();
  int py_get_cursor_y();
  std::string py_get_line(int line);
  int py_get_line_count();
  std::string py_get_current_file();
  std::string py_get_buffer_content();
  void py_set_buffer_content(const std::string &text);
  std::string py_get_selected_text();
  void py_execute_command(const std::string &command);
  void py_execute_ex_command(const std::string &command_line);
  int py_reload_plugins();
  std::vector<std::string> py_list_plugins();
  bool py_apply_colorscheme(const std::string &name);
  std::vector<std::string> py_list_themes();
  std::string py_list_buffers_json();
  std::string py_list_panes_json();
  std::string py_get_layout_json();
  bool py_switch_buffer(int index);
  bool py_close_buffer(int index);
  void py_new_buffer();
  void py_split_pane(const std::string &direction);
  bool py_resize_pane(int delta);
  void py_focus_next_pane();
  void py_focus_prev_pane();
  void py_toggle_sidebar();
  void py_open_workspace(const std::string &path);
  void py_toggle_terminal();
  void py_request_redraw();
  void py_request_quit(bool force);
  void py_save_and_quit();
  void py_toggle_minimap();
};

#endif
