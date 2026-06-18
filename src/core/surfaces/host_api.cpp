#include "host_api.h"
#include "editor.h"
#include "python_bridge/api.h"

#include <algorithm>
#include <sstream>

EditorHostAPI::EditorHostAPI(Editor &editor)
    : core(editor), render(editor), io(editor) {}

std::string HostCoreAPI::current_file() const {
  if (editor.buffers.empty()) {
    return "";
  }
  return editor.get_buffer().filepath;
}

std::vector<HostBufferInfo> HostCoreAPI::list_buffers() const {
  std::vector<HostBufferInfo> out;
  out.reserve(editor.buffers.size());
  for (int i = 0; i < (int)editor.buffers.size(); i++) {
    const FileBuffer &buf = editor.buffers[(size_t)i];
    HostBufferInfo info;
    info.index = i;
    info.filepath = buf.filepath;
    info.modified = buf.modified;
    info.active = (i == editor.current_buffer);
    info.preview = (i == editor.preview_buffer_index);
    out.push_back(info);
  }
  return out;
}

bool HostCoreAPI::switch_buffer(int index) {
  if (index < 0 || index >= (int)editor.buffers.size()) {
    return false;
  }

  editor.current_buffer = index;
  if (!editor.panes.empty()) {
    auto &pane = editor.get_pane();
    pane.buffer_id = index;
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                  index) == pane.tab_buffer_ids.end()) {
      pane.tab_buffer_ids.push_back(index);
    }
    int draw_w = std::max(1, pane.w);
    if (editor.show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - editor.minimap_width);
    }
    editor.reveal_local_tab(pane, editor.find_local_tab_index(pane, index),
                            draw_w);
  }

  editor.focus_state = Editor::FOCUS_EDITOR;
  editor.ensure_cursor_visible();
  editor.needs_redraw = true;
  return true;
}

bool HostCoreAPI::close_buffer(int index) {
  if (index < 0 || index >= (int)editor.buffers.size()) {
    return false;
  }
  editor.close_buffer_at(index);
  editor.needs_redraw = true;
  return true;
}

void HostCoreAPI::new_buffer() {
  editor.create_new_buffer();
  editor.needs_redraw = true;
}

std::string HostCoreAPI::buffer_content() const {
  if (editor.buffers.empty()) {
    return "";
  }
  return editor.get_buffer_text(editor.get_buffer());
}

void HostCoreAPI::set_buffer_content(const std::string &text) {
  if (editor.buffers.empty()) {
    return;
  }

  editor.save_state();
  FileBuffer &buf = editor.get_buffer();
  if (buf.is_lazy()) buf.materialize();
  buf.lines.clear();

  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      std::string line = text.substr(start);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      buf.lines.push_back(line);
      break;
    }
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    buf.lines.push_back(line);
    start = end + 1;
    if (start == text.size()) {
      buf.lines.push_back("");
      break;
    }
  }

  if (buf.lines.empty()) {
    buf.lines.push_back("");
  }

  buf.cursor = {0, 0};
  buf.preferred_x = 0;
  buf.selection = {{0, 0}, {0, 0}, false};
  buf.scroll_offset = 0;
  buf.scroll_x = 0;
  buf.modified = true;

  editor.invalidate_syntax_cache(buf);
  editor.ensure_cursor_visible();
  editor.needs_redraw = true;

  if (editor.python_api) {
    editor.python_api->on_buffer_change(buf.filepath,
                                        editor.get_buffer_text(buf));
  }
}

std::string HostCoreAPI::selected_text() const {
  if (editor.buffers.empty()) {
    return "";
  }

  const FileBuffer &buf = editor.get_buffer();
  if (!buf.selection.active) {
    return "";
  }

  Cursor start = buf.selection.start;
  Cursor end = buf.selection.end;
  if (start.y > end.y || (start.y == end.y && start.x > end.x)) {
    std::swap(start, end);
  }

  std::string out;
  for (int y = start.y; y <= end.y && y < (int)buf.line_count(); y++) {
    const std::string &line = buf.line(y);
    int from = (y == start.y) ? std::max(0, start.x) : 0;
    int to =
        (y == end.y) ? std::min((int)line.size(), end.x) : (int)line.size();
    if (from < to) {
      out += line.substr((size_t)from, (size_t)(to - from));
    }
    if (y != end.y) {
      out.push_back('\n');
    }
  }
  return out;
}

void HostCoreAPI::replace_selection(const std::string &text) {
  if (editor.buffers.empty()) {
    return;
  }

  FileBuffer &buf = editor.get_buffer();
  if (!buf.selection.active) {
    insert_text(text);
    return;
  }

  editor.save_state();
  editor.delete_selection();
  editor.insert_string(text);
}

void HostCoreAPI::insert_text(const std::string &text) {
  if (editor.buffers.empty()) {
    return;
  }
  editor.save_state();
  editor.insert_string(text);
}

std::pair<int, int> HostCoreAPI::cursor() const {
  if (editor.buffers.empty()) {
    return {0, 0};
  }
  const FileBuffer &buf = editor.get_buffer();
  return {buf.cursor.y, buf.cursor.x};
}

void HostCoreAPI::set_cursor(int line, int col) {
  if (editor.buffers.empty()) {
    return;
  }
  FileBuffer &buf = editor.get_buffer();
  if (buf.line_count() == 0) {
    buf.cursor = {0, 0};
    return;
  }
  buf.cursor.y = std::clamp(line, 0, (int)buf.line_count() - 1);
  buf.cursor.x = std::clamp(col, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  editor.clear_selection();
  editor.ensure_cursor_visible();
  editor.needs_redraw = true;
}

HostLayoutInfo HostRenderAPI::layout() const {
  HostLayoutInfo info{};
  info.width = editor.terminal.get_width();
  info.height = editor.terminal.get_height();
  info.sidebar_visible = editor.show_sidebar;
  info.sidebar_width = editor.effective_sidebar_width();
  info.minimap_visible = editor.show_minimap;
  info.terminal_visible = editor.show_integrated_terminal;
  info.terminal_height = editor.integrated_terminal_height;
  return info;
}

std::vector<HostPaneInfo> HostRenderAPI::list_panes() const {
  std::vector<HostPaneInfo> out;
  out.reserve(editor.panes.size());
  for (int i = 0; i < (int)editor.panes.size(); i++) {
    const SplitPane &pane = editor.panes[(size_t)i];
    HostPaneInfo info;
    info.index = i;
    info.buffer_id = pane.buffer_id;
    info.x = pane.x;
    info.y = pane.y;
    info.w = pane.w;
    info.h = pane.h;
    info.focused = (i == editor.current_pane);
    out.push_back(info);
  }
  return out;
}

void HostRenderAPI::split_horizontal() { editor.split_pane_horizontal(); }

void HostRenderAPI::split_vertical() { editor.split_pane_vertical(); }

void HostRenderAPI::focus_next_pane() { editor.next_pane(); }

void HostRenderAPI::focus_prev_pane() { editor.prev_pane(); }

bool HostRenderAPI::resize_focused_pane(int delta) {
  return editor.resize_current_pane(delta);
}

void HostRenderAPI::request_redraw() { editor.needs_redraw = true; }

void HostIOAPI::open_file(const std::string &path) { editor.open_file(path); }

void HostIOAPI::save_current_file() { editor.save_file(); }

bool HostIOAPI::save_buffer(int index) { return editor.save_buffer_at(index, false); }

void HostIOAPI::open_workspace(const std::string &path) {
  editor.open_workspace(path);
}

void HostIOAPI::toggle_sidebar() { editor.toggle_sidebar(); }

void HostIOAPI::toggle_terminal() { editor.toggle_integrated_terminal(); }

void HostIOAPI::execute_command(const std::string &command) {
  editor.execute_command(command);
}

void HostIOAPI::run_job(const std::string &command, const std::string &cwd,
                        const std::string &label) {
  if (command.empty()) {
    editor.set_message("Plugin job requires a command");
    return;
  }
  std::string job_label = label.empty() ? "plugin" : label;
  editor.create_integrated_terminal("plugin:" + job_label,
                                    cwd.empty() ? editor.root_dir : cwd);
  IntegratedTerminal *term = editor.get_integrated_terminal();
  if (!term) {
    return;
  }
  term->send_text(command);
  term->send_text("\n");
}

void HostIOAPI::show_plugin_picker(const std::string &title,
                                   const std::string &items_callback,
                                   const std::string &select_callback) {
  if (!editor.python_api) {
    return;
  }
  std::vector<Editor::QuickPickItem> items;
  for (const auto &line : editor.python_api->plugin_picker_items(items_callback)) {
    Editor::QuickPickItem item;
    item.label = line;
    item.detail = "Plugin";
    item.preview = line;
    item.filepath.clear();
    item.line = 0;
    item.col = 0;
    items.push_back(std::move(item));
  }
  editor.plugin_quick_pick_select_callback = select_callback;
  editor.open_quick_pick(Editor::QUICK_PICK_PLUGIN,
                         title.empty() ? "Plugin Picker" : title,
                         std::move(items));
}

void HostIOAPI::show_plugin_panel(const std::string &name) {
  if (name.empty()) {
    return;
  }
  editor.active_plugin_panel = name;
  editor.show_right_panel = true;
  editor.active_right_panel_tab = Editor::RIGHT_PANEL_PLUGIN;
  editor.show_home_menu = false;
  editor.update_pane_layout();
  editor.needs_redraw = true;
}
