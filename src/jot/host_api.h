#ifndef EDITOR_HOST_API_H
#define EDITOR_HOST_API_H

#include <string>
#include <vector>

class Editor;
struct Selection;

struct HostBufferInfo
{
  int index;
  std::string filepath;
  bool modified;
  bool active;
  bool preview;
};

struct HostPaneInfo
{
  int index;
  int buffer_id;
  int x;
  int y;
  int w;
  int h;
  bool focused;
};

struct HostLayoutInfo
{
  int width;
  int height;
  bool sidebar_visible;
  int sidebar_width;
  bool minimap_visible;
  bool terminal_visible;
  int terminal_height;
};

class HostCoreAPI
{
public:
  explicit HostCoreAPI(Editor &editor) : editor(editor)
  {
  }

  std::string current_file() const;
  std::vector<HostBufferInfo> list_buffers() const;
  bool switch_buffer(int index);
  bool close_buffer(int index);
  void new_buffer();
  std::string buffer_content() const;
  void set_buffer_content(const std::string &text);
  std::string selected_text() const;
  size_t extra_caret_count() const;
  std::string extra_caret_text(size_t index) const;
  std::string selected_text_for(const Selection &sel) const;
  void replace_selection(const std::string &text);
  void insert_text(const std::string &text);
  void insert_char_at_carets(char c);
  void undo();
  void redo();
  bool multicursor_active();
  void clear_extra_carets();
  bool add_caret_at(int line, int col);
  bool select_next_occurrence();
  std::pair<int, int> cursor() const;
  void set_cursor(int line, int col);

private:
  Editor &editor;
};

class HostRenderAPI
{
public:
  explicit HostRenderAPI(Editor &editor) : editor(editor)
  {
  }

  HostLayoutInfo layout() const;
  std::vector<HostPaneInfo> list_panes() const;
  void split_horizontal();
  void split_vertical();
  void focus_next_pane();
  void focus_prev_pane();
  bool resize_focused_pane(int delta);
  bool resize_focused_pane_direction(char dir, int step);
  void equalize_panes();
  void toggle_pane_zoom();
  void swap_panes();
  void request_redraw();

private:
  Editor &editor;
};

class HostIOAPI
{
public:
  explicit HostIOAPI(Editor &editor) : editor(editor)
  {
  }

  void open_file(const std::string &path);
  void save_current_file();
  bool save_buffer(int index);
  void open_workspace(const std::string &path);
  void open_command_palette(const std::string &query);
  void toggle_sidebar();
  // Zen focus mode; returns true when zen is now active.
  bool toggle_zen();
  void toggle_terminal();
  void execute_command(const std::string &command);
  void run_job(const std::string &command, const std::string &cwd, const std::string &label);
  void show_plugin_picker(const std::string &title,
                          const std::string &items_callback,
                          const std::string &select_callback);
  void show_plugin_panel(const std::string &name);

private:
  Editor &editor;
};

class EditorHostAPI
{
public:
  explicit EditorHostAPI(Editor &editor);

  HostCoreAPI core;
  HostRenderAPI render;
  HostIOAPI io;
};

#endif
