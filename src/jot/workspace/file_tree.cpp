// File tree: recursive build, lazy child refresh, and the fs-watch
// baseline / polling that keeps the sidebar in sync with disk.

#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "jot/workspace/workspace_internal.h"
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

  std::string normalize_path_for_tree(const std::string &path)
  {
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    if (ec)
    {
      p = fs::path(path);
    }
    return p.lexically_normal().string();
  }

  void collect_expanded_paths(const std::vector<FileNode> &nodes,
                              std::unordered_set<std::string> &expanded)
  {
    for (const auto &node : nodes)
    {
      if (node.is_dir && node.expanded)
      {
        expanded.insert(normalize_path_for_tree(node.path));
        if (!node.children.empty())
        {
          collect_expanded_paths(node.children, expanded);
        }
      }
    }
  }

  long long file_time_key(fs::file_time_type time)
  {
    return time.time_since_epoch().count();
  }
} // namespace
void Editor::load_file_tree(const std::string &path)
{
  const std::string old_root = normalize_path_for_tree(root_dir);
  std::unordered_set<std::string> old_expanded;
  collect_expanded_paths(file_tree, old_expanded);

  std::string old_selected_path;
  if (!file_tree.empty())
  {
    std::vector<const FileNode *> old_flat;
    flatten_nodes_const(file_tree, old_flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)old_flat.size())
    {
      old_selected_path = normalize_path_for_tree(old_flat[file_tree_selected]->path);
    }
  }
  const int old_scroll = file_tree_scroll;

  file_tree.clear();
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec)
    p = fs::path(path);
  root_dir = p.lexically_normal().string();
  if (!fs::exists(p) || !fs::is_directory(p))
  {
    invalidate_sidebar_tree_cache();
    refresh_file_tree_watch_baseline();
    return;
  }

  const std::string new_root = normalize_path_for_tree(root_dir);
  const bool same_root = !old_root.empty() && old_root == new_root;

  build_tree(root_dir, file_tree, 0);
  invalidate_sidebar_tree_cache();
  arm_file_tree_watch();

  if (!same_root)
  {
    file_tree_selected = 0;
    file_tree_scroll = 0;
    refresh_file_tree_watch_baseline();
    return;
  }

  std::function<void(std::vector<FileNode> &)> restore_expanded = [&](std::vector<FileNode> &nodes)
  {
    for (auto &node : nodes)
    {
      if (!node.is_dir)
      {
        continue;
      }
      const std::string normalized = normalize_path_for_tree(node.path);
      if (old_expanded.find(normalized) != old_expanded.end())
      {
        node.expanded = true;
        if (node.children.empty())
        {
          build_tree(node.path, node.children, node.depth + 1);
        }
        restore_expanded(node.children);
      }
    }
  };
  restore_expanded(file_tree);
  invalidate_sidebar_tree_cache();

  std::vector<FileNode *> refreshed_flat;
  flatten_nodes_mut(file_tree, refreshed_flat);
  file_tree_selected = 0;
  if (!old_selected_path.empty())
  {
    for (int i = 0; i < (int)refreshed_flat.size(); i++)
    {
      if (normalize_path_for_tree(refreshed_flat[i]->path) == old_selected_path)
      {
        file_tree_selected = i;
        break;
      }
    }
  }
  int view_h = std::max(1, sidebar_list_rows());
  int max_scroll = std::max(0, (int)refreshed_flat.size() - view_h);
  file_tree_scroll = std::clamp(old_scroll, 0, max_scroll);
  if (file_tree_selected < file_tree_scroll)
  {
    file_tree_scroll = file_tree_selected;
  }
  else if (file_tree_selected >= file_tree_scroll + view_h)
  {
    file_tree_scroll = std::clamp(file_tree_selected - view_h + 1, 0, max_scroll);
  }
  refresh_file_tree_watch_baseline();
}

std::string Editor::telescope_launch_root() const
{
  // A real workspace root always wins.
  if (!root_dir.empty() && root_dir != ".")
  {
    return root_dir;
  }
  // No workspace: prefer the detected git root when this session is inside a
  // repository (the git status refresh already computed it).
  if (!git_root.empty())
  {
    return git_root;
  }
  // Single file opened outside a repo (or before git status ran): walk up
  // from the current file's directory looking for a project marker.
  std::error_code ec;
  const std::string file = buffers.empty() ? std::string() : buffers[current_buffer].filepath;
  fs::path dir = file.empty() ? fs::current_path(ec) : fs::path(file).parent_path();
  if (ec || dir.empty())
  {
    dir = fs::current_path();
  }
  for (;;)
  {
    for (const char *marker : {".git", ".hg", ".svn"})
    {
      const fs::path probe = dir / marker;
      if (fs::exists(probe, ec))
      {
        return dir.lexically_normal().string();
      }
    }
    const fs::path parent = dir.parent_path();
    if (parent == dir)
    {
      break;
    }
    dir = parent;
  }
  return dir.lexically_normal().string();
}
void Editor::build_tree(const std::string &path, std::vector<FileNode> &nodes, int depth)
{
  try
  {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(path))
    {
      std::string filename = entry.path().filename().string();
      if (!sidebar_show_hidden && !filename.empty() && filename[0] == '.')
      {
        continue;
      }
      entries.push_back(entry);
    }

    std::sort(entries.begin(),
              entries.end(),
              [](const fs::directory_entry &a, const fs::directory_entry &b)
              {
                if (a.is_directory() != b.is_directory())
                {
                  return a.is_directory() > b.is_directory();
                }
                return a.path().filename() < b.path().filename();
              });

    for (const auto &entry : entries)
    {
      FileNode node;
      node.name = entry.path().filename().string();
      node.path = entry.path().string();
      node.is_dir = entry.is_directory();
      node.expanded = false;
      node.depth = depth;
      nodes.push_back(node);
    }
  }
  catch (...)
  {
  }
}
void Editor::refresh_tree_children(FileNode &node)
{
  if (!node.is_dir)
  {
    return;
  }
  node.children.clear();
  build_tree(node.path, node.children, node.depth + 1);
}

std::string Editor::build_file_tree_signature() const
{
  if (root_dir.empty())
  {
    return "";
  }

  std::error_code ec;
  fs::path root = fs::absolute(root_dir, ec);
  if (ec)
  {
    root = fs::path(root_dir);
  }
  root = root.lexically_normal();
  if (!fs::exists(root, ec) || ec || !fs::is_directory(root, ec))
  {
    return "";
  }

  std::ostringstream sig;
  sig << root.string() << '\n';
  sig << "hidden=" << (sidebar_show_hidden ? 1 : 0) << '\n';

  auto append_path = [&](const fs::path &path, bool expanded)
  {
    std::error_code stat_ec;
    const bool is_dir = fs::is_directory(path, stat_ec);
    const bool exists = !stat_ec && fs::exists(path, stat_ec);
    uintmax_t size = 0;
    if (exists && !is_dir)
    {
      size = fs::file_size(path, stat_ec);
      if (stat_ec)
      {
        size = 0;
      }
    }
    long long mtime = 0;
    if (exists)
    {
      mtime = file_time_key(fs::last_write_time(path, stat_ec));
      if (stat_ec)
      {
        mtime = 0;
      }
    }

    fs::path rel = path.lexically_relative(root);
    sig << rel.string() << '\t' << (is_dir ? 'd' : 'f') << '\t' << (expanded ? '1' : '0') << '\t'
        << mtime << '\t' << size << '\n';
  };

  std::function<void(const std::vector<FileNode> &)> append_nodes =
      [&](const std::vector<FileNode> &nodes)
  {
    for (const auto &node : nodes)
    {
      fs::path path = fs::path(node.path).lexically_normal();
      append_path(path, node.is_dir && node.expanded);
      if (node.is_dir && node.expanded)
      {
        append_nodes(node.children);
      }
    }
  };

  append_path(root, true);
  append_nodes(file_tree);

  return sig.str();
}
void Editor::refresh_file_tree_watch_baseline()
{
  file_tree_watch_signature_ = build_file_tree_signature();
  file_tree_watch_ready_ = !file_tree_watch_signature_.empty();
}
void Editor::poll_file_tree_changes()
{
  if (root_dir.empty() || file_tree.empty())
  {
    file_tree_watch_signature_.clear();
    file_tree_watch_ready_ = false;
    return;
  }

  const std::string signature = build_file_tree_signature();
  if (signature.empty())
  {
    file_tree_watch_signature_.clear();
    file_tree_watch_ready_ = false;
    return;
  }

  if (!file_tree_watch_ready_)
  {
    file_tree_watch_signature_ = signature;
    file_tree_watch_ready_ = true;
    return;
  }

  if (signature == file_tree_watch_signature_)
  {
    return;
  }

  load_file_tree(root_dir);
  needs_redraw = true;
}
void Editor::arm_file_tree_watch()
{
  if (root_dir.empty())
  {
    if (!file_tree_event_watch_root_.empty())
    {
      event_loop_.unwatch_path(file_tree_event_watch_root_);
      file_tree_event_watch_root_.clear();
    }
    return;
  }
  if (!file_tree_event_watch_root_.empty() && file_tree_event_watch_root_ != root_dir)
  {
    event_loop_.unwatch_path(file_tree_event_watch_root_);
    file_tree_event_watch_root_.clear();
  }
  if (file_tree_event_watch_root_ == root_dir)
  {
    return;
  }
  if (event_loop_.watch_path(root_dir, [this](const std::string &) { poll_file_tree_changes(); }))
  {
    file_tree_event_watch_root_ = root_dir;
  }
}