// Sidebar interaction: tree navigation, git view, tab / buffer actions,
// context keys, and mouse clicks.

#include "base/shell_util.h"
#include "base/string_util.h"
#include "cpp_assist.h"
#include "core/editor.h"
#include "folding.h"
#include "lua/api/api.h"
#include "workspace/workspace_internal.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
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
  if (ch == '\t')
  {
    if (explorer_only())
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

  if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
  {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    const int view_h = std::max(1, sidebar_list_rows());
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
                                           + shell_util::shell_quote(rel));
        if (diff.empty())
        {
          message = ch == 'D' ? "Git diff: no staged changes for " + rel
                              : "Git diff: no unstaged changes for " + rel;
        }
        else
        {
          show_popup(string_util::limit_lines(diff, 18), "Git Diff");
        }
      }
      needs_redraw = true;
      return;
    }
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);

  int view_h = std::max(1, sidebar_list_rows());
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

  file_tree_selected = std::clamp(file_tree_selected, -1, std::max(-1, (int)flat.size() - 1));
  clamp_scroll();

  // Cursor keys walk the column's rows. Its ends stop the cursor (no wrap).
  if (ch == 1008 || ch == 'k' || ch == 1009 || ch == 'j')
  {
    sidebar_move_cursor((ch == 1008 || ch == 'k') ? -1 : 1);
    return;
  }

  // Page / Home / End stay inside the list.
  if (ch == 1012 || ch == 1013 || ch == 1015 || ch == 1016)
  {
    const int count = sidebar_section_row_count();
    if (count <= 0)
    {
      return;
    }
    int index = std::clamp(sidebar_section_cursor(), 0, count - 1);
    const int page = std::max(1, sidebar_sections_layout().explorer.h);
    if (ch == 1012)
    {
      index = 0;
    }
    else if (ch == 1013)
    {
      index = count - 1;
    }
    else if (ch == 1015)
    {
      index = std::max(0, index - page);
    }
    else
    {
      index = std::min(count - 1, index + page);
    }
    sidebar_set_cursor(index);
    needs_redraw = true;
    return;
  }

  if (ch == '*' || ch == 'Z')
  {
    // A symlinked directory is followed once per level and the tree keeps no
    // record of where it has already been, so a link that leads back to an
    // ancestor (build trees make these: `_deps/<name>-src`) expands into an
    // endless chain of rows and an unbounded amount of work. The depth cap is
    // the same guard for a real tree deep enough to matter.
    constexpr int kMaxExpandDepth = 16;
    std::function<void(std::vector<FileNode> &)> expand_all = [&](std::vector<FileNode> &nodes)
    {
      for (auto &node : nodes)
      {
        if (!node.is_dir || node.depth >= kMaxExpandDepth)
        {
          continue;
        }
        std::error_code link_ec;
        if (fs::is_symlink(fs::path(node.path), link_ec) || link_ec)
        {
          continue;
        }
        node.expanded = true;
        refresh_tree_children(node);
        expand_all(node.children);
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

  if (ch == 'E')
  {
    // Reveal the active buffer's file: expand every directory along the
    // path from the workspace root, then select and scroll to the row.
    if (current_buffer < 0 || current_buffer >= (int)buffers.size())
    {
      return;
    }
    const std::string target = normalize_path(buffers[current_buffer].filepath);
    if (target.empty())
    {
      return;
    }
    std::error_code ec;
    fs::path rel = fs::path(target).lexically_relative(fs::path(root_dir));
    const std::string rel_s = rel.string();
    if (rel.empty() || rel_s == "." || rel_s == ".." || rel_s.rfind("../", 0) == 0
        || rel_s.rfind("..\\", 0) == 0)
    {
      message = "Not under the workspace root";
      needs_redraw = true;
      return;
    }

    std::vector<FileNode> *level = &file_tree;
    fs::path walk = fs::path(root_dir);
    for (const auto &part : rel)
    {
      const std::string comp = part.string();
      walk /= comp;
      auto it = std::find_if(
          level->begin(), level->end(), [&](const FileNode &n) { return n.name == comp; });
      if (it == level->end())
      {
        message = "Not in the tree (hidden?): " + comp;
        needs_redraw = true;
        return;
      }
      if (!it->is_dir)
      {
        // Found the file: select and reveal it.
        invalidate_sidebar_tree_cache();
        std::vector<FileNode *> refreshed;
        flatten_nodes_mut(file_tree, refreshed);
        for (int i = 0; i < (int)refreshed.size(); i++)
        {
          if (normalize_path(refreshed[i]->path) == target)
          {
            file_tree_selected = i;
            break;
          }
        }
        flat = refreshed;
        clamp_scroll();
        if (file_tree_selected < file_tree_scroll)
        {
          file_tree_scroll = file_tree_selected;
        }
        else if (file_tree_selected >= file_tree_scroll + view_h)
        {
          file_tree_scroll = std::max(0, file_tree_selected - view_h + 1);
          clamp_scroll();
        }
        message = "Revealed: " + to_workspace_relative(target);
        needs_redraw = true;
        return;
      }
      if (!it->expanded)
      {
        it->expanded = true;
        refresh_tree_children(*it);
      }
      level = &it->children;
    }
    return;
  }

  if (ch == '\n' || ch == 13)
  {
    sidebar_activate_cursor();
    return;
  }

  if (ch == 'l' || ch == 1010)
  {
    flatten_nodes_mut(file_tree, flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
    {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir && !node->expanded)
      {
        node->expanded = true;
        refresh_tree_children(*node);
        invalidate_sidebar_tree_cache();
        needs_redraw = true;
      }
      else if (!node->is_dir)
      {
        load_file(node->path);
        focus_state = FOCUS_EDITOR;
        record_jump();
        needs_redraw = true;
      }
    }
    return;
  }

  if (ch == 'h' || ch == 1011)
  {
    if (file_tree_selected == -1)
    {
      // The root row is the explorer's own folder: Left closes the section.
      sidebar_toggle_explorer();
      return;
    }
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
      else if (node->depth == 0)
      {
        file_tree_selected = -1;
        file_tree_scroll = 0;
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
    std::string rel = base == root_dir ? "" : to_workspace_relative(base);
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
    std::string rel = base == root_dir ? "" : to_workspace_relative(base);
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
Editor::SidebarHit Editor::sidebar_hit_test(int y) const
{
  SidebarHit hit;
  const jot_sidebar::Layout sections = sidebar_sections_layout();
  bool header = false;
  const int index = jot_sidebar::index_in_section(sections.explorer, y, &header);
  if (index == -2)
  {
    return hit;
  }
  hit.index = index;
  hit.header = header;
  return hit;
}

int Editor::sidebar_data_index(const SidebarHit &hit) const
{
  return hit.index - 1 + file_tree_scroll; // row 0 is the pinned root
}

int Editor::sidebar_content_x() const
{
  const int w = effective_sidebar_width();
  const int rail_w =
      explorer_only() ? 0 : std::min(sidebar_activity_rail_width(), std::max(1, w - 1));
  return std::max(1, rail_w);
}

void Editor::sidebar_update_hover(int x, int y)
{
  int index = -2;
  if (show_sidebar && !sidebar_hidden_for_width_ && x >= 0 && x < effective_sidebar_width())
  {
    const ContentColumn col = side_column();
    // The activity rail is not a row: it switches views, so it never takes a
    // row highlight (the click path reads it the same way).
    const bool on_rail = !explorer_only() && x < sidebar_activity_rail_width();
    if (!on_rail && y >= col.top && y < col.bottom)
    {
      index = sidebar_hit_test(y).index;
    }
  }
  if (sidebar_hover_index == index)
  {
    return;
  }
  sidebar_hover_index = index;
  needs_redraw = true;
}

int Editor::sidebar_section_row_count() const
{
  return sidebar_explorer_open ? 1 + (int)sidebar_render_cache_.rows.size() : 0;
}

int Editor::sidebar_section_cursor() const
{
  return file_tree_selected + 1; // display space: 0 is the pinned root row
}

void Editor::sidebar_set_cursor(int index)
{
  file_tree_selected = index - 1;
}

void Editor::sidebar_move_cursor(int delta)
{
  if (delta == 0 || !show_sidebar)
  {
    return;
  }
  // One list: the cursor walks its rows and stops at either end (no wrap, and
  // no neighbouring section to step into).
  const int count = sidebar_section_row_count();
  if (count <= 0)
  {
    return;
  }
  sidebar_set_cursor(std::clamp(sidebar_section_cursor() + delta, 0, count - 1));
  focus_state = FOCUS_SIDEBAR;
  needs_redraw = true;
}

void Editor::sidebar_toggle_explorer()
{
  sidebar_explorer_open = !sidebar_explorer_open;
  needs_redraw = true;
}

void Editor::sidebar_activate_cursor()
{
  // The pinned root row toggles the whole section, a directory opens or closes,
  // and a file opens in the editor.
  if (file_tree_selected == -1)
  {
    sidebar_toggle_explorer();
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);
  if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size())
  {
    FileNode *node = flat[(size_t)file_tree_selected];
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
      load_file(node->path);
      focus_state = FOCUS_EDITOR;
      record_jump();
    }
  }
  needs_redraw = true;
}

void Editor::handle_sidebar_mouse(int x, int y, bool is_click, bool is_double_click)
{
  if (!is_click)
    return;

  int rel_y = y - side_column_top();
  if (rel_y < 0)
    return;
  if (!explorer_only() && x < sidebar_activity_rail_width())
  {
    if (rel_y == 1)
    {
      active_sidebar_view = SIDEBAR_VIEW_EXPLORER;
      needs_redraw = true;
    }
    else if (rel_y == 3)
    {
      // The rail's git item is the git panel launcher (":gitpanel"), not a
      // switch of the sidebar's own view. The toggle refreshes the panel.
      toggle_git_panel();
    }
    return;
  }

  if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
  {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int sidebar_row = y - side_column_top() - 1;
    // Header above, footer on the column's last row: only the rows between them
    // hold list entries, so a click on either must not select one.
    if (sidebar_row < 0 || sidebar_row >= sidebar_list_rows())
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

  // The column's header toggles the section; the rows under it are the tree,
  // with the root row pinned on top.
  const SidebarHit hit = sidebar_hit_test(y);
  if (hit.index == -2)
  {
    return;
  }
  focus_state = FOCUS_SIDEBAR;
  if (hit.header)
  {
    sidebar_toggle_explorer();
    return;
  }
  if (hit.index == 0)
  {
    file_tree_selected = -1;
    file_tree_scroll = 0;
    needs_redraw = true;
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);
  const int row = sidebar_data_index(hit);
  if (row >= 0 && row < (int)flat.size())
  {
    FileNode *node = flat[(size_t)row];
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
