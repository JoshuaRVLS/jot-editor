#ifndef LUA_API_H
#define LUA_API_H

#include "text_features.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <map>

struct LuaScratchBuffer {
  int handle = 0;
  bool listed = false;
  bool scratch = true;
  bool valid = true;
  std::vector<std::string> lines = {""};
};

struct LuaFloatWindow {
  int handle = 0;
  int buffer = 0;
  int x = 0, y = 0, w = 1, h = 1;
  int row = 0, col = 0;
  int zindex = 50;
  bool valid = true;
  bool enter = false;
  bool focusable = true;
  bool mouse = false;
  bool hide = false;
  bool style_minimal = false;
  int fg = 7, bg = 0;
  std::string relative = "editor";
  std::string anchor = "NW";
  std::string border = "none";
  std::array<std::string, 8> custom_border = {"", "", "", "", "", "", "", ""};
  std::string title;
  std::string footer;
  int key_callback = -1;
  int mouse_callback = -1;
  int creation_order = 0;
};

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
class TreeSitterManager;
struct lua_State;

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
  int next_scratch_buffer = 1;
  int next_float_window = 1;
  int next_float_order = 1;
  int current_float_window = 0;
  std::map<int, LuaScratchBuffer> scratch_buffers;
  std::map<int, LuaFloatWindow> float_windows;
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
  TreeSitterManager &tree_sitter_manager();
  bool is_main_thread() const;
  void register_treesitter_api(lua_State *L);
  bool load_treesitter_runtime(lua_State *L);

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

  int create_scratch_buffer(bool listed, bool scratch);
  bool set_scratch_lines(int buffer, int start, int end, bool strict,
                         const std::vector<std::string> &lines);
  std::vector<std::string> get_scratch_lines(int buffer, int start, int end,
                                             bool strict) const;
  bool delete_scratch_buffer(int buffer);
  int open_float(int buffer, bool enter, lua_State *L, int config_index);
  bool configure_float(int window, lua_State *L, int config_index);
  bool close_float(int window, bool force);
  bool is_float_valid(int window) const;
  bool float_input(int ch, bool ctrl, bool shift, bool alt);
  bool float_mouse(int x, int y, int button, bool pressed, bool released,
                   bool motion, bool ctrl, bool shift, bool alt);
  void render_floats();
  void clear_floats();
};

#endif
