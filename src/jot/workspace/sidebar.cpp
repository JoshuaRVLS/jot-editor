// Sidebar interaction: tree navigation, git view, tab / buffer actions,
// context keys, and mouse clicks.

#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "jot/workspace/workspace_internal.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include "cpp_assist.h"
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

using namespace workspace_internal;

namespace
{

  void collapse_all_nodes(std::vector<FileNode> &nodes)
  {
    for (auto &node : nodes)
    {
      if (node.is_dir)
      {
        node.expanded = false;
        if (!node.children.empty())
        {
          collapse_all_nodes(node.children);
        }
      }
    }
  }

  bool is_empty_scratch_buffer(const FileBuffer &buf)
  {
    return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 && buf.line(0).empty();
  }
} // namespace
void Editor::handle_sidebar_input(int ch)
{
  static std::string pending_delete_path;
  static long long pending_delete_deadline_ms = 0;

  auto now_ms = []() -> long long
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  };
  auto normalize_path = [](const std::string &path)
  {
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    if (ec)
    {
      p = fs::path(path);
    }
    return p.lexically_normal().string();
  };
  auto starts_with_path = [](const std::string &child, const std::string &parent)
  {
    if (child.size() < parent.size())
    {
      return false;
    }
    if (child.compare(0, parent.size(), parent) != 0)
    {
      return false;
    }
    return child.size() == parent.size() || child[parent.size()] == '/'
           || child[parent.size()] == '\\';
  };
  auto close_buffers_for_path = [&](const std::string &target_abs, bool is_dir)
  {
    const std::string norm_target = normalize_path(target_abs);
    for (int i = (int)buffers.size() - 1; i >= 0; --i)
    {
      if (buffers[i].filepath.empty())
      {
        continue;
      }
      std::string buf_path = normalize_path(buffers[i].filepath);
      bool match = (!is_dir && buf_path == norm_target)
                   || (is_dir && starts_with_path(buf_path, norm_target));
      if (match)
      {
        close_buffer_at(i);
      }
    }
  };
  auto to_workspace_relative = [&](const std::string &abs_path)
  {
    fs::path rel = fs::path(abs_path).lexically_relative(fs::path(root_dir));
    std::string rel_s = rel.string();
    if (!rel_s.empty() && rel_s != "." && rel_s.find("..") != 0)
    {
      return rel_s;
    }
    return abs_path;
  };
  auto shell_quote_local = [](const std::string &value)
  {
    std::string out = "'";
    out.reserve(value.size() + 8);
    for (char c : value)
    {
      if (c == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out.push_back(c);
      }
    }
    out.push_back('\'');
    return out;
  };
  auto limit_lines_local = [](const std::string &text, int max_lines)
  {
    if (max_lines <= 0)
    {
      return std::string();
    }
    std::istringstream iss(text);
    std::string out;
    std::string line;
    int count = 0;
    while (count < max_lines && std::getline(iss, line))
    {
      if (count > 0)
      {
        out += '\n';
      }
      out += line;
      count++;
    }
    if (std::getline(iss, line))
    {
      out += "\n...";
    }
    return out;
  };

  if (ch == '\t')
  {
    if (kExplorerOnly)
    {
      return;
    }
    active_sidebar_view =
        active_sidebar_view == SIDEBAR_VIEW_EXPLORER ? SIDEBAR_VIEW_GIT : SIDEBAR_VIEW_EXPLORER;
    if (active_sidebar_view == SIDEBAR_VIEW_GIT)
    {
      refresh_git_status(true);
    }
    needs_redraw = true;
    return;
  }

  if (!kExplorerOnly && active_sidebar_view == SIDEBAR_VIEW_GIT)
  {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int reserved_terminal_h = 0;
    if (show_integrated_terminal && !integrated_terminals.empty())
    {
      reserved_terminal_h =
          std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
    }
    const int view_h =
        std::max(1, ui->get_height() - status_height - tab_height - reserved_terminal_h - 2);
    auto clamp_scroll = [&]()
    {
      int max_scroll = std::max(0, (int)git_rows.size() - view_h);
      git_sidebar_scroll = std::clamp(git_sidebar_scroll, 0, max_scroll);
    };
    auto ensure_selected_visible = [&]()
    {
      if (git_sidebar_selected < git_sidebar_scroll)
      {
        git_sidebar_scroll = git_sidebar_selected;
      }
      else if (git_sidebar_selected >= git_sidebar_scroll + view_h)
      {
        git_sidebar_scroll = git_sidebar_selected - view_h + 1;
      }
      clamp_scroll();
    };

    if (!git_rows.empty())
    {
      git_sidebar_selected = std::clamp(git_sidebar_selected, 0, (int)git_rows.size() - 1);
    }
    else
    {
      git_sidebar_selected = 0;
    }
    clamp_scroll();

    auto selected_path = [&]() -> std::string
    {
      if (git_rows.empty() || git_sidebar_selected < 0
          || git_sidebar_selected >= (int)git_rows.size())
      {
        return "";
      }
      return git_rows[(size_t)git_sidebar_selected].path;
    };
    auto selected_rel = [&]() -> std::string
    {
      if (git_rows.empty() || git_sidebar_selected < 0
          || git_sidebar_selected >= (int)git_rows.size())
      {
        return "";
      }
      return git_rows[(size_t)git_sidebar_selected].relative_path;
    };

    if (ch == 1008 || ch == 'k')
    {
      if (git_sidebar_selected > 0)
      {
        git_sidebar_selected--;
        ensure_selected_visible();
        needs_redraw = true;
      }
      return;
    }
    if (ch == 1009 || ch == 'j')
    {
      if (git_sidebar_selected < (int)git_rows.size() - 1)
      {
        git_sidebar_selected++;
        ensure_selected_visible();
        needs_redraw = true;
      }
      return;
    }
    if (ch == 1015)
    {
      git_sidebar_selected = std::max(0, git_sidebar_selected - view_h);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1016)
    {
      git_sidebar_selected =
          std::min(std::max(0, (int)git_rows.size() - 1), git_sidebar_selected + view_h);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1012)
    {
      git_sidebar_selected = 0;
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1013)
    {
      git_sidebar_selected = std::max(0, (int)git_rows.size() - 1);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == '\n' || ch == 13 || ch == 'l' || ch == 1010)
    {
      std::string path = selected_path();
      if (!path.empty())
      {
        open_file(path, false);
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
      return;
    }
    if (ch == 'r' || ch == 'R')
    {
      refresh_git_status(true);
      message = has_git_repo() ? "Git: refreshed" : "Git: not a repository";
      needs_redraw = true;
      return;
    }
    if (ch == 'a')
    {
      refresh_git_status(true);
      if (!has_git_repo())
      {
        message = "Git: not a repository";
      }
      else if (git_stage_all())
      {
        message = "Git staged all changes";
      }
      else
      {
        message = "Git stage all failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 's')
    {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo())
      {
        message = "Git: select a changed file";
      }
      else if (git_stage_path(path))
      {
        message = "Git staged: " + selected_rel();
      }
      else
      {
        message = "Git stage failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 'u')
    {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo())
      {
        message = "Git: select a changed file";
      }
      else if (git_unstage_path(path))
      {
        message = "Git unstaged: " + selected_rel();
      }
      else
      {
        message = "Git unstage failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 'd' || ch == 'D')
    {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo())
      {
        message = "Git: select a changed file";
      }
      else
      {
        std::string rel = to_git_relative_path(path);
        std::string diff = run_git_capture(std::string(ch == 'D' ? "diff --staged -- " : "diff -- ")
                                           + shell_quote_local(rel));
        if (diff.empty())
        {
          message = ch == 'D' ? "Git diff: no staged changes for " + rel
                              : "Git diff: no unstaged changes for " + rel;
        }
        else
        {
          show_popup(limit_lines_local(diff, 18), "Git Diff");
        }
      }
      needs_redraw = true;
      return;
    }
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);

  int view_h = std::max(1, ui->get_height() - status_height - tab_height - 2);
  auto clamp_scroll = [&]()
  {
    int max_scroll = std::max(0, (int)flat.size() - view_h);
    file_tree_scroll = std::clamp(file_tree_scroll, 0, max_scroll);
  };
  auto ensure_selected_visible = [&]()
  {
    if (file_tree_selected < file_tree_scroll)
    {
      file_tree_scroll = file_tree_selected;
    }
    else if (file_tree_selected >= file_tree_scroll + view_h)
    {
      file_tree_scroll = file_tree_selected - view_h + 1;
    }
    clamp_scroll();
  };

  file_tree_selected = std::clamp(file_tree_selected, 0, std::max(0, (int)flat.size() - 1));
  clamp_scroll();

  if (ch == 1008 || ch == 'k')
  { // Up
    if (file_tree_selected > 0)
    {
      file_tree_selected--;
      ensure_selected_visible();
      needs_redraw = true;
    }
    return;
  }

  if (ch == 1009 || ch == 'j')
  { // Down
    if (file_tree_selected < (int)flat.size() - 1)
    {
      file_tree_selected++;
      ensure_selected_visible();
      needs_redraw = true;
    }
    return;
  }

  if (ch == 1015)
  { // Page Up
    file_tree_selected = std::max(0, file_tree_selected - view_h);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1016)
  { // Page Down
    file_tree_selected = std::min(std::max(0, (int)flat.size() - 1), file_tree_selected + view_h);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1012)
  { // Home
    file_tree_selected = 0;
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1013)
  { // End
    file_tree_selected = std::max(0, (int)flat.size() - 1);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == '*' || ch == 'Z')
  {
    std::function<void(std::vector<FileNode> &)> expand_all = [&](std::vector<FileNode> &nodes)
    {
      for (auto &node : nodes)
      {
        if (node.is_dir)
        {
          node.expanded = true;
          refresh_tree_children(node);
          expand_all(node.children);
        }
      }
    };
    expand_all(file_tree);
    invalidate_sidebar_tree_cache();
    message = "Explorer: expanded all";
    needs_redraw = true;
    return;
  }

  if (ch == 'z')
  {
    collapse_all_nodes(file_tree);
    invalidate_sidebar_tree_cache();
    file_tree_selected = 0;
    file_tree_scroll = 0;
    message = "Explorer: collapsed all";
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13 || ch == 'l' || ch == 1010)
  {
    flatten_nodes_mut(file_tree, flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir)
      {
        if (!node->expanded)
        {
          node->expanded = true;
          refresh_tree_children(*node);
          invalidate_sidebar_tree_cache();
        }
        else if (ch == '\n' || ch == 13)
        {
          node->expanded = false;
          invalidate_sidebar_tree_cache();
        }
        needs_redraw = true;
      }
      else
      {
        load_file(node->path);
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
    }
    return;
  }

  if (ch == 'h' || ch == 1011)
  {
    flatten_nodes_mut(file_tree, flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir && node->expanded)
      {
        node->expanded = false;
        invalidate_sidebar_tree_cache();
        needs_redraw = true;
      }
      else if (node->depth > 0)
      {
        int target_depth = node->depth - 1;
        for (int i = file_tree_selected - 1; i >= 0; i--)
        {
          if (flat[i]->depth == target_depth)
          {
            file_tree_selected = i;
            ensure_selected_visible();
            needs_redraw = true;
            break;
          }
        }
      }
    }
    return;
  }

  if (ch == 'r' || ch == 'R')
  {
    if (ch == 'r')
    {
      if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
      {
        FileNode *node = flat[file_tree_selected];
        const std::string old_rel = to_workspace_relative(node->path);
        show_command_palette = true;
        command_palette_query = "rename " + old_rel + " ";
        command_palette_results.clear();
        command_palette_selected = 0;
        command_palette_theme_mode = false;
        command_palette_theme_original.clear();
        refresh_command_palette();
        needs_redraw = true;
      }
      return;
    }

    std::string selected_path;
    if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      selected_path = flat[file_tree_selected]->path;
    }

    load_file_tree(root_dir);

    if (!selected_path.empty())
    {
      std::vector<FileNode *> refreshed;
      flatten_nodes_mut(file_tree, refreshed);
      for (int i = 0; i < (int)refreshed.size(); i++)
      {
        if (refreshed[i]->path == selected_path)
        {
          file_tree_selected = i;
          break;
        }
      }
      int max_scroll = std::max(0, (int)refreshed.size() - view_h);
      file_tree_scroll = std::clamp(file_tree_selected - view_h / 2, 0, max_scroll);
    }

    message = "Explorer: refreshed";
    needs_redraw = true;
    return;
  }

  if (ch == 'a')
  {
    std::string base = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir)
      {
        base = node->path;
      }
      else
      {
        base = fs::path(node->path).parent_path().string();
      }
    }
    std::string rel = to_workspace_relative(base);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\')
    {
      rel += "/";
    }
    else if (rel == ".")
    {
      rel.clear();
    }
    show_command_palette = true;
    command_palette_query = "mkfile " + rel;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
    return;
  }

  if (ch == 'A')
  {
    std::string base = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir)
      {
        base = node->path;
      }
      else
      {
        base = fs::path(node->path).parent_path().string();
      }
    }
    std::string rel = to_workspace_relative(base);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\')
    {
      rel += "/";
    }
    else if (rel == ".")
    {
      rel.clear();
    }
    show_command_palette = true;
    command_palette_query = "mkdir " + rel;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
    return;
  }

  if (ch == 'i')
  {
    if (flat.empty() || file_tree_selected < 0 || file_tree_selected >= (int)flat.size())
    {
      return;
    }
    FileNode *node = flat[file_tree_selected];
    if (node->is_dir)
    {
      message = "Select a C++ header or source file";
      needs_redraw = true;
      return;
    }
    fs::path selected(node->path);
    if (!CppAssist::is_header_path(selected) && !CppAssist::is_source_path(selected))
    {
      message = "Select a C++ header or source file";
      needs_redraw = true;
      return;
    }
    execute_command("cppimpl " + to_workspace_relative(node->path));
    return;
  }

  if (ch == 'C')
  {
    std::string target = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir)
      {
        target = node->path;
      }
      else
      {
        fs::path selected(node->path);
        if (CppAssist::is_header_path(selected) || CppAssist::is_source_path(selected))
        {
          target = selected.replace_extension("").string();
        }
        else
        {
          target = selected.parent_path().string();
        }
      }
    }
    std::string rel = to_workspace_relative(target);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\'
        && (target == root_dir || fs::is_directory(target)))
    {
      rel += "/";
    }
    else if (rel == ".")
    {
      rel.clear();
    }
    show_command_palette = true;
    command_palette_query = "cpppair " + rel;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
    return;
  }

  if (ch == 'd')
  {
    if (flat.empty() || file_tree_selected < 0 || file_tree_selected >= (int)flat.size())
    {
      return;
    }
    FileNode *node = flat[file_tree_selected];
    const std::string node_path = normalize_path(node->path);
    const std::string node_name = node->name;
    const long long now = now_ms();

    if (pending_delete_path == node_path && now <= pending_delete_deadline_ms)
    {
      std::error_code ec;
      bool is_dir = fs::is_directory(node_path, ec);
      close_buffers_for_path(node_path, is_dir);
      if (is_dir)
      {
        fs::remove_all(node_path, ec);
      }
      else
      {
        fs::remove(node_path, ec);
      }
      pending_delete_path.clear();
      pending_delete_deadline_ms = 0;
      if (ec)
      {
        message = "Delete failed: " + ec.message();
      }
      else
      {
        load_file_tree(root_dir);
        file_tree_selected = 0;
        file_tree_scroll = 0;
        message = "Deleted: " + node_name;
      }
      needs_redraw = true;
      return;
    }

    pending_delete_path = node_path;
    pending_delete_deadline_ms = now + 1400;
    message = "Press d again to delete: " + node->name;
    needs_redraw = true;
    return;
  }

  if (ch == 'g' || ch == 'G')
  {
    refresh_git_status(true);
    if (has_git_repo())
    {
      message = "Git: " + git_branch + " (+" + std::to_string(git_staged_count) + " ~"
                + std::to_string(git_unstaged_count) + " ?" + std::to_string(git_untracked_count)
                + ")";
    }
    else
    {
      message = "Git: not a repository";
    }
    needs_redraw = true;
    return;
  }

  if (ch == '.')
  {
    std::string selected_path;
    if (!flat.empty() && file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      selected_path = flat[file_tree_selected]->path;
    }

    sidebar_show_hidden = !sidebar_show_hidden;
    load_file_tree(root_dir);

    if (!selected_path.empty())
    {
      std::vector<FileNode *> refreshed;
      flatten_nodes_mut(file_tree, refreshed);
      for (int i = 0; i < (int)refreshed.size(); i++)
      {
        if (refreshed[i]->path == selected_path)
        {
          file_tree_selected = i;
          break;
        }
      }
    }

    message =
        sidebar_show_hidden ? "Explorer: showing hidden files" : "Explorer: hiding hidden files";
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8)
  {
    fs::path current(root_dir);
    if (current.has_parent_path())
    {
      fs::path parent = current.parent_path();
      if (!parent.empty() && parent != current)
      {
        open_workspace(parent.string(), true);
      }
    }
    return;
  }
}
void Editor::handle_sidebar_mouse(int x, int y, bool is_click, bool is_double_click)
{
  if (!is_click)
    return;

  int rel_y = y - topbar_height();
  if (rel_y < 0)
    return;
  if (!kExplorerOnly && x < sidebar_activity_rail_width())
  {
    if (rel_y == 1)
    {
      active_sidebar_view = SIDEBAR_VIEW_EXPLORER;
      needs_redraw = true;
    }
    else if (rel_y == 3)
    {
      active_sidebar_view = SIDEBAR_VIEW_GIT;
      refresh_git_status(true);
      needs_redraw = true;
    }
    return;
  }

  if (!kExplorerOnly && active_sidebar_view == SIDEBAR_VIEW_GIT)
  {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int sidebar_row = y - topbar_height() - 1;
    if (sidebar_row < 0)
      return;
    int row = sidebar_row + git_sidebar_scroll;
    if (row >= 0 && row < (int)git_rows.size())
    {
      git_sidebar_selected = row;
      open_file(git_rows[(size_t)row].path, !is_double_click);
      if (is_double_click)
      {
        focus_state = FOCUS_EDITOR;
      }
      needs_redraw = true;
    }
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);

  // Sidebar now has 1-line header, so tree rows begin after that.
  int sidebar_row = y - topbar_height() - 1;
  if (sidebar_row < 0)
    return;
  int row = sidebar_row + file_tree_scroll;
  if (row >= 0 && row < (int)flat.size())
  {
    FileNode *node = flat[row];
    file_tree_selected = row;

    if (node->is_dir)
    {
      node->expanded = !node->expanded;
      if (node->expanded)
      {
        refresh_tree_children(*node);
      }
      invalidate_sidebar_tree_cache();
    }
    else
    {
      open_file(node->path, !is_double_click);
      if (is_double_click)
      {
        focus_state = FOCUS_EDITOR;
      }
    }
    needs_redraw = true;
  }
}