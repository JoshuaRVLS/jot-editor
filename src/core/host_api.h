#ifndef EDITOR_HOST_API_H
#define EDITOR_HOST_API_H

#include <string>
#include <vector>

class Editor;

struct HostBufferInfo {
  int index;
  std::string filepath;
  bool modified;
  bool active;
  bool preview;
};

struct HostPaneInfo {
  int index;
  int buffer_id;
  int x;
  int y;
  int w;
  int h;
  bool focused;
};

struct HostLayoutInfo {
  int width;
  int height;
  bool sidebar_visible;
  int sidebar_width;
  bool minimap_visible;
  bool terminal_visible;
  int terminal_height;
};

class HostCoreAPI {
public:
  explicit HostCoreAPI(Editor &editor) : editor(editor) {}

  std::string current_file() const;
  std::vector<HostBufferInfo> list_buffers() const;
  bool switch_buffer(int index);
  bool close_buffer(int index);
  void new_buffer();
  std::string buffer_content() const;
  void set_buffer_content(const std::string &text);
  std::string selected_text() const;

private:
  Editor &editor;
};

class HostRenderAPI {
public:
  explicit HostRenderAPI(Editor &editor) : editor(editor) {}

  HostLayoutInfo layout() const;
  std::vector<HostPaneInfo> list_panes() const;
  void split_horizontal();
  void split_vertical();
  void focus_next_pane();
  void focus_prev_pane();
  bool resize_focused_pane(int delta);
  void request_redraw();

private:
  Editor &editor;
};

class HostIOAPI {
public:
  explicit HostIOAPI(Editor &editor) : editor(editor) {}

  void open_file(const std::string &path);
  void save_current_file();
  bool save_buffer(int index);
  void open_workspace(const std::string &path);
  void toggle_sidebar();
  void toggle_terminal();
  void execute_command(const std::string &command);

private:
  Editor &editor;
};

class HostEventsAPI {
public:
  explicit HostEventsAPI(Editor &editor) : editor(editor) {}

  void emit(const std::string &event_name,
            const std::string &payload_json = "{}");

private:
  Editor &editor;
};

class EditorHostAPI {
public:
  explicit EditorHostAPI(Editor &editor);

  HostCoreAPI core;
  HostRenderAPI render;
  HostIOAPI io;
  HostEventsAPI events;
};

#endif
