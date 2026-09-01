#ifndef LUA_API_H
#define LUA_API_H

#include "text_features.h"
#include <string>
#include <vector>
#include <unordered_map>

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
class EditorHostAPI;

class LuaAPI {
private:
  Editor *editor;
  std::vector<PluginCommand> plugin_commands;
  std::vector<PluginKeymap> plugin_keymaps;
  std::vector<PluginAutocmd> plugin_autocmds;
  std::vector<PluginPanel> plugin_panels;
  std::vector<PluginLoadStatus> plugin_load_status;

  void *lua_state; // lua_State* (opaque in public header)
  bool lua_initialized;
public:
  std::unordered_map<std::string, int> lua_callbacks;

private:
  void clear_runtime_state();
  bool load_script_path(const std::string &module_name,
                        const std::string &path);
  bool call_callback_string(const std::string &callback,
                            const std::string &arg);
  bool call_callback_event(const std::string &callback, const std::string &event,
                           const std::string &filepath, int buffer);

public:
  LuaAPI(Editor *ed);
  ~LuaAPI();
  EditorHostAPI &host();

  bool init();
  void cleanup();
  void on_buffer_open(const std::string &filepath);
  void on_buffer_change(const std::string &filepath,
                        const std::string &content);
  void on_buffer_save(const std::string &filepath);

  // Lua API functions used by the embedded runtime.
  void show_message(const std::string &msg);
  void set_theme_color(const std::string &name, int fg, int bg);
  bool apply_colorscheme(const std::string &name);
  std::vector<std::string> list_themes();

  // Plugin system
  void load_plugins();
  void reload_plugins();
  bool run_plugin_command(const std::string &name, const std::string &arg);
  bool run_plugin_keymap(const std::string &key,
                         const std::string &mode = "global");
  void fire_autocmd(const std::string &event, const std::string &filepath = "",
                    int buffer = -1);
  std::vector<std::string> plugin_panel_lines(const std::string &name);
  std::vector<std::string> plugin_picker_items(const std::string &callback);
  bool run_plugin_callback(const std::string &callback,
                           const std::string &arg = "");
  void register_command(const std::string &name,
                           const std::string &callback,
                           const std::string &detail);
  void register_keymap(const std::string &key, const std::string &callback,
                          const std::string &command,
                          const std::string &detail,
                          const std::string &mode);
  void register_autocmd(const std::string &event,
                           const std::string &callback);
  void register_panel(const std::string &name,
                         const std::string &callback,
                         const std::string &title);

  const std::vector<PluginCommand> &commands() const { return plugin_commands; }
  const std::vector<PluginKeymap> &keymaps() const { return plugin_keymaps; }
  const std::vector<PluginAutocmd> &autocmds() const { return plugin_autocmds; }
  const std::vector<PluginPanel> &panels() const { return plugin_panels; }
  const std::vector<PluginLoadStatus> &load_status() const {
    return plugin_load_status;
  }
  std::string get_current_buffer();
  void set_current_buffer(const std::string &text);
  std::string get_selection();
  void replace_selection(const std::string &text);
  void insert_text(const std::string &text);
  std::pair<int, int> get_cursor();
  void set_cursor(int line, int col);
  std::string current_file();
  void open_file(const std::string &path);
  void save_current_file();
  void execute_command(const std::string &command);
  void run_job(const std::string &command, const std::string &cwd,
                  const std::string &label);
  void show_picker(const std::string &title,
                      const std::string &items_callback,
                      const std::string &select_callback);
  void show_panel(const std::string &name);
};

#endif
