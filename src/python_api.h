#ifndef PYTHON_API_H
#define PYTHON_API_H

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
  std::map<std::string, std::function<bool(int, bool, bool)>> keybinds;
  std::vector<Keybind> registered_keybinds;
  bool python_initialized;

  bool init_python();
  void cleanup_python();
  bool load_plugin(const std::string &path);
  bool execute_python_callback(const std::string &callback, int key, bool ctrl,
                               bool shift);

public:
  PythonAPI(Editor *ed);
  ~PythonAPI();

  bool init();
  void cleanup();

  // Load plugins from directory
  void load_plugins(const std::string &plugin_dir = "plugins");

  // Register keybind from Python
  bool register_keybind(const std::string &key_str, const std::string &mode,
                        const std::string &callback);

  // Check if a keybind should be handled by Python
  bool handle_keybind(int key, bool ctrl, bool shift, bool alt,
                      const std::string &mode);

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
  void py_hide_popup();
  void py_set_diagnostics(
      const std::string &filepath,
      const std::string &json_diagnostics); // Parsing list of dicts from python
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
                         const std::string &message, int severity);

  std::string py_get_mode();
  int py_get_cursor_x();
  int py_get_cursor_y();
  std::string py_get_line(int line);
  int py_get_line_count();
};

#endif
