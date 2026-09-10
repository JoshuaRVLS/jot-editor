// Workspace lifecycle: opening a workspace, sidebar / zen toggles, and
// session save / restore (recent-file lists, buffer set, layout).

#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "jot/workspace/workspace_internal.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include "cpp_assist.h"
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

using namespace workspace_internal;

namespace
{

  std::string session_root_dir()
  {
    const char *override_home = std::getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return (fs::path(override_home) / "workspaces").string();
    }
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return (fs::path(app_data) / "jot" / "workspaces").string();
    }
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    if (!home || !*home)
    {
      return "";
    }
    return (fs::path(home) / ".config" / "jot" / "workspaces").string();
  }

  std::string session_file_for_root(const std::string &root)
  {
    if (root.empty())
    {
      return "";
    }
    const std::string base = session_root_dir();
    if (base.empty())
    {
      return "";
    }
    std::size_t hv = std::hash<std::string>{}(root);
    std::ostringstream oss;
    oss << base << "/" << std::hex << hv << ".session";
    return oss.str();
  }

  // Stable names for right-dock tabs in the session file (readable and
  // immune to enum reordering).
  const char *right_panel_tab_name(RightPanelTab tab)
  {
    switch (tab)
    {
    case RIGHT_PANEL_GIT:
      return "git";
    case RIGHT_PANEL_GIT_DIFF:
      return "diff";
    case RIGHT_PANEL_SYMBOLS:
      return "symbols";
    case RIGHT_PANEL_DEBUG:
      return "debug";
    case RIGHT_PANEL_PLUGIN:
      return "plugin";
    }
    return "git";
  }

  bool parse_right_panel_tab(const std::string &name, RightPanelTab &out)
  {
    if (name == "git")
    {
      out = RIGHT_PANEL_GIT;
      return true;
    }
    if (name == "diff")
    {
      out = RIGHT_PANEL_GIT_DIFF;
      return true;
    }
    if (name == "symbols")
    {
      out = RIGHT_PANEL_SYMBOLS;
      return true;
    }
    if (name == "debug")
    {
      out = RIGHT_PANEL_DEBUG;
      return true;
    }
    if (name == "plugin")
    {
      out = RIGHT_PANEL_PLUGIN;
      return true;
    }
    return false;
  }
} // namespace
void Editor::toggle_sidebar()
{
  show_sidebar = !show_sidebar;
  if (show_sidebar)
  {
    if (file_tree.empty())
    {
      load_file_tree(root_dir);
    }
    focus_state = FOCUS_SIDEBAR;
  }
  else
  {
    focus_state = FOCUS_EDITOR;
  }
  update_pane_layout();
  needs_redraw = true;
}
bool Editor::toggle_zen_mode()
{
  zen_mode = !zen_mode;
  if (zen_mode)
  {
    zen_saved_sidebar_ = show_sidebar;
    zen_saved_panel_ = show_right_panel;
    zen_saved_status_height_ = status_height;
    show_sidebar = false;
    show_right_panel = false;
    if (focus_state == FOCUS_SIDEBAR)
    {
      focus_state = FOCUS_EDITOR;
    }
    status_height = 0;
  }
  else
  {
    show_sidebar = zen_saved_sidebar_;
    show_right_panel = zen_saved_panel_;
    status_height = zen_saved_status_height_;
  }
  // The side panel float is keyed off show_right_panel; drop it explicitly so
  // it does not linger over the centered pane area.
  if (lua_api)
  {
    lua_api->emit_lua_ui_close("side_panel");
  }
  update_pane_layout();
  needs_redraw = true;
  return zen_mode;
}
void Editor::open_workspace(const std::string &path, bool restore_session)
{
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec)
  {
    p = fs::path(path);
  }
  if (!fs::exists(p) || !fs::is_directory(p))
  {
    set_message("Workspace not found: " + path);
    return;
  }

  std::string normalized = p.lexically_normal().string();
  track_recent_workspace(normalized);

  if (workspace_session_enabled && !workspace_session_root.empty()
      && workspace_session_root != normalized)
  {
    save_workspace_session();
  }

  load_file_tree(normalized);
  show_sidebar = true;
  focus_state = FOCUS_SIDEBAR;
  workspace_session_enabled = true;
  workspace_session_root = root_dir;
  show_home_menu = false;

  // Reset editor buffers when entering a workspace so sessions do not mix.
  stop_all_lsp_clients();
  lsp_disabled_servers.clear();
  buffers.clear();
  workspace_diagnostic_severity.clear();
  invalidate_sidebar_diagnostics_cache();
  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  fb.is_preview = false;
  fb.is_placeholder = true;
  buffers.push_back(std::move(fb));
  current_buffer = 0;
  tab_scroll_index = 0;
  preview_buffer_index = -1;
  for (auto &pane : panes)
  {
    pane.buffer_id = 0;
    pane.tab_buffer_ids.clear();
    pane.tab_buffer_ids.push_back(0);
    pane.tab_scroll_index = 0;
  }

  bool restored = false;
  if (restore_session)
  {
    restored = restore_workspace_session();
  }

  if (!restored)
  {
    set_message("Workspace: " + root_dir);
  }
  refresh_git_status(true);
  needs_redraw = true;
  if (lua_api)
    lua_api->fire_autocmd("WorkspaceEnter", normalized, -1);
}
bool Editor::resume_last_workspace_session()
{
  while (!recent_workspaces.empty())
  {
    const std::string candidate = recent_workspaces.front();
    std::error_code ec;
    if (fs::exists(candidate, ec) && !ec && fs::is_directory(candidate, ec))
    {
      open_workspace(candidate, true);
      show_home_menu = false;
      set_message("Resumed: " + get_filename(candidate));
      needs_redraw = true;
      return true;
    }
    recent_workspaces.erase(recent_workspaces.begin());
  }

  show_home_menu = true;
  set_message("No recent workspace session");
  needs_redraw = true;
  return false;
}
void Editor::save_workspace_session()
{
  if (!workspace_session_enabled || workspace_session_root.empty())
  {
    return;
  }

  const std::string session_path = session_file_for_root(workspace_session_root);
  if (session_path.empty())
  {
    return;
  }

  std::vector<FileBuffer *> persisted;
  persisted.reserve(buffers.size());
  for (auto &buf : buffers)
  {
    if (!buf.filepath.empty())
    {
      persisted.push_back(&buf);
    }
  }

  std::error_code ec;
  fs::create_directories(fs::path(session_path).parent_path(), ec);
  if (ec)
  {
    return;
  }

  std::ofstream out(session_path, std::ios::trunc);
  if (!out.is_open())
  {
    return;
  }

  out << "version\t1\n";
  out << "root\t" << escape_field(workspace_session_root) << "\n";
  out << "show_sidebar\t" << (show_sidebar ? 1 : 0) << "\n";
  out << "sidebar_width\t" << effective_sidebar_width() << "\n";
  out << "sidebar_view\t" << (active_sidebar_view == SIDEBAR_VIEW_GIT ? "git" : "explorer") << "\n";
  out << "right_panel_width\t" << right_panel_width << "\n";
  out << "show_right_panel\t" << (show_right_panel ? 1 : 0) << "\n";
  if (!right_panel_tabs.empty())
  {
    std::string tab_list;
    for (size_t i = 0; i < right_panel_tabs.size(); i++)
    {
      if (i)
      {
        tab_list += ",";
      }
      tab_list += right_panel_tab_name(right_panel_tabs[i]);
    }
    out << "right_panel_tabs\t" << tab_list << "\n";
    out << "right_panel_active_tab\t" << right_panel_tab_name(active_right_panel_tab) << "\n";
  }
  out << "sidebar_show_hidden\t" << (sidebar_show_hidden ? 1 : 0) << "\n";

  std::string current_file;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    current_file = buffers[current_buffer].filepath;
  }
  out << "current_file\t" << escape_field(current_file) << "\n";

  for (FileBuffer *buf : persisted)
  {
    if (!buf->is_lazy())
    {
      Folding::refresh_ranges(buf->fold_ranges, buf->lines, get_file_extension(buf->filepath));
      buf->folds_dirty = false;
    }
    std::string fold_payload = Folding::encode_collapsed_ranges(buf->fold_ranges);
    out << "file\t" << escape_field(buf->filepath) << "\t" << buf->cursor.y << "\t" << buf->cursor.x
        << "\t" << buf->scroll_offset << "\t" << buf->scroll_x << "\t" << (buf->is_preview ? 1 : 0)
        << "\t" << escape_field(fold_payload) << "\n";
  }
}
bool Editor::restore_workspace_session()
{
  if (!workspace_session_enabled || workspace_session_root.empty())
  {
    return false;
  }

  const std::string session_path = session_file_for_root(workspace_session_root);
  if (session_path.empty())
  {
    return false;
  }

  std::ifstream in(session_path);
  if (!in.is_open())
  {
    return false;
  }

  struct Entry
  {
    std::string path;
    int cy;
    int cx;
    int scroll;
    int scroll_x;
    bool preview;
    bool has_collapsed_folds = false;
    std::vector<FoldRange> collapsed_folds;
  };

  bool restored_show_sidebar = true;
  SidebarView restored_sidebar_view = SIDEBAR_VIEW_EXPLORER;
  bool restored_hidden = sidebar_show_hidden;
  int restored_sidebar_width = sidebar_width;
  int restored_right_panel_width = right_panel_width;
  bool restored_show_right_panel = false;
  std::vector<RightPanelTab> restored_right_panel_tabs;
  RightPanelTab restored_active_tab = RIGHT_PANEL_DEBUG;
  bool restored_has_active_tab = false;
  std::string target_current_file;
  std::vector<Entry> entries;
  auto clamp_restored_right_panel_width = [&]()
  {
    int max_w = max_right_panel_width();
    int min_w = std::min(min_right_panel_width(), max_w);
    return std::clamp(restored_right_panel_width, min_w, max_w);
  };

  std::string line;
  while (std::getline(in, line))
  {
    if (line.empty())
    {
      continue;
    }
    std::vector<std::string> parts = split_tab(line);
    if (parts.empty())
    {
      continue;
    }
    const std::string key = parts[0];
    if (key == "show_sidebar" && parts.size() >= 2)
    {
      restored_show_sidebar = (parts[1] == "1");
    }
    else if (key == "sidebar_width" && parts.size() >= 2)
    {
      try
      {
        restored_sidebar_width = std::stoi(parts[1]);
      }
      catch (...)
      {
      }
    }
    else if (key == "sidebar_view" && parts.size() >= 2)
    {
      restored_sidebar_view = parts[1] == "git" ? SIDEBAR_VIEW_GIT : SIDEBAR_VIEW_EXPLORER;
    }
    else if (key == "right_panel_width" && parts.size() >= 2)
    {
      try
      {
        restored_right_panel_width = std::stoi(parts[1]);
      }
      catch (...)
      {
      }
    }
    else if (key == "sidebar_show_hidden" && parts.size() >= 2)
    {
      restored_hidden = (parts[1] == "1");
    }
    else if (key == "show_right_panel" && parts.size() >= 2)
    {
      restored_show_right_panel = (parts[1] == "1");
    }
    else if (key == "right_panel_tabs" && parts.size() >= 2)
    {
      std::vector<RightPanelTab> tabs;
      std::stringstream ss(parts[1]);
      std::string name;
      while (std::getline(ss, name, ','))
      {
        RightPanelTab tab;
        if (parse_right_panel_tab(name, tab)
            && std::find(tabs.begin(), tabs.end(), tab) == tabs.end())
        {
          tabs.push_back(tab);
        }
      }
      restored_right_panel_tabs = std::move(tabs);
    }
    else if (key == "right_panel_active_tab" && parts.size() >= 2)
    {
      RightPanelTab tab;
      if (parse_right_panel_tab(parts[1], tab))
      {
        restored_active_tab = tab;
        restored_has_active_tab = true;
      }
    }
    else if (key == "current_file" && parts.size() >= 2)
    {
      target_current_file = unescape_field(parts[1]);
    }
    else if (key == "file" && parts.size() >= 7)
    {
      Entry e;
      e.path = unescape_field(parts[1]);
      try
      {
        e.cy = std::stoi(parts[2]);
        e.cx = std::stoi(parts[3]);
        e.scroll = std::stoi(parts[4]);
        e.scroll_x = std::stoi(parts[5]);
        e.preview = (parts[6] == "1");
        if (parts.size() >= 8)
        {
          e.has_collapsed_folds = true;
          e.collapsed_folds = Folding::decode_collapsed_ranges(unescape_field(parts[7]));
        }
        entries.push_back(e);
      }
      catch (...)
      {
      }
    }
  }

  // Restore the right dock tab strip as it was left: the same tabs in the
  // same order, the same active tab, and the dock open/closed state.
  if (!restored_right_panel_tabs.empty())
  {
    right_panel_tabs = restored_right_panel_tabs;
    if (restored_has_active_tab
        && std::find(right_panel_tabs.begin(), right_panel_tabs.end(), restored_active_tab)
               != right_panel_tabs.end())
    {
      active_right_panel_tab = restored_active_tab;
    }
    else
    {
      active_right_panel_tab = right_panel_tabs.back();
    }
    show_right_panel = restored_show_right_panel;
    // A restored git-diff tab needs its backing state so closing it routes
    // through the panel's closer (return-to-git) instead of the generic one.
    if (std::find(right_panel_tabs.begin(), right_panel_tabs.end(), RIGHT_PANEL_GIT_DIFF)
        != right_panel_tabs.end())
    {
      git_diff_panel.visible = true;
    }
  }

  if (entries.empty())
  {
    show_sidebar = restored_show_sidebar;
    active_sidebar_view = kExplorerOnly ? SIDEBAR_VIEW_EXPLORER : restored_sidebar_view;
    sidebar_width = std::clamp(restored_sidebar_width, min_sidebar_width(), max_sidebar_width());
    right_panel_width = clamp_restored_right_panel_width();
    sidebar_show_hidden = restored_hidden;
    return false;
  }

  sidebar_width = std::clamp(restored_sidebar_width, min_sidebar_width(), max_sidebar_width());
  active_sidebar_view = kExplorerOnly ? SIDEBAR_VIEW_EXPLORER : restored_sidebar_view;
  right_panel_width = clamp_restored_right_panel_width();
  sidebar_show_hidden = restored_hidden;
  load_file_tree(workspace_session_root);

  if (buffers.size() == 1 && is_empty_scratch_buffer(buffers[0]))
  {
    buffers.clear();
    current_buffer = 0;
    for (auto &pane : panes)
    {
      pane.buffer_id = 0;
      pane.tab_buffer_ids.clear();
      pane.tab_buffer_ids.push_back(0);
      pane.tab_scroll_index = 0;
    }
  }

  preview_buffer_index = -1;

  std::vector<std::string> restored_paths;
  restored_paths.reserve(entries.size());
  for (const auto &entry : entries)
  {
    std::error_code ec;
    if (!fs::exists(entry.path, ec) || ec || fs::is_directory(entry.path, ec))
    {
      continue;
    }

    open_file(entry.path, false);
    restored_paths.push_back(entry.path);

    if (current_buffer >= 0 && current_buffer < (int)buffers.size())
    {
      FileBuffer &buf = buffers[current_buffer];
      buf.cursor.y = std::clamp(entry.cy, 0, std::max(0, (int)buf.line_count() - 1));
      buf.cursor.x = std::clamp(entry.cx, 0, (int)buf.line(buf.cursor.y).size());
      buf.preferred_x = buf.cursor.x;
      buf.scroll_offset = std::max(0, entry.scroll);
      buf.scroll_x = std::max(0, entry.scroll_x);
      buf.is_preview = entry.preview;
      if (!buf.is_lazy())
      {
        Folding::refresh_ranges(buf.fold_ranges, buf.lines, get_file_extension(buf.filepath));
        buf.folds_dirty = false;
      }
      if (entry.has_collapsed_folds)
      {
        Folding::apply_collapsed_ranges(buf.fold_ranges, entry.collapsed_folds);
      }
      while (buf.cursor.y > 0 && Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y))
      {
        buf.cursor.y--;
      }
      buf.cursor.x = std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
      buf.preferred_x = buf.cursor.x;
      if (buf.is_preview)
      {
        preview_buffer_index = current_buffer;
      }
    }
  }

  if (restored_paths.empty())
  {
    return false;
  }

  int desired_buffer = -1;
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (buffers[i].filepath == target_current_file)
    {
      desired_buffer = i;
      break;
    }
  }
  if (desired_buffer < 0)
  {
    desired_buffer = std::max(0, (int)buffers.size() - 1);
  }

  current_buffer = desired_buffer;
  if (!panes.empty())
  {
    auto &pane = get_pane();
    pane.buffer_id = current_buffer;
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(), current_buffer)
        == pane.tab_buffer_ids.end())
    {
      pane.tab_buffer_ids.push_back(current_buffer);
    }
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  show_sidebar = restored_show_sidebar;
  active_sidebar_view = kExplorerOnly ? SIDEBAR_VIEW_EXPLORER : restored_sidebar_view;
  sidebar_width = effective_sidebar_width();
  set_message("Workspace restored: " + root_dir);
  return true;
}