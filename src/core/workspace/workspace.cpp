#include "editor.h"
#include "cpp_assist.h"
#include "folding.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
void flatten_nodes_mut(std::vector<FileNode> &nodes,
                       std::vector<FileNode *> &flat) {
  for (auto &node : nodes) {
    flat.push_back(&node);
    if (node.is_dir && node.expanded) {
      flatten_nodes_mut(node.children, flat);
    }
  }
}

void flatten_nodes_const(const std::vector<FileNode> &nodes,
                         std::vector<const FileNode *> &flat) {
  for (const auto &node : nodes) {
    flat.push_back(&node);
    if (node.is_dir && node.expanded) {
      flatten_nodes_const(node.children, flat);
    }
  }
}

std::string normalize_path_for_tree(const std::string &path) {
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec) {
    p = fs::path(path);
  }
  return p.lexically_normal().string();
}

void collect_expanded_paths(const std::vector<FileNode> &nodes,
                            std::unordered_set<std::string> &expanded) {
  for (const auto &node : nodes) {
    if (node.is_dir && node.expanded) {
      expanded.insert(normalize_path_for_tree(node.path));
      if (!node.children.empty()) {
        collect_expanded_paths(node.children, expanded);
      }
    }
  }
}

void collapse_all_nodes(std::vector<FileNode> &nodes) {
  for (auto &node : nodes) {
    if (node.is_dir) {
      node.expanded = false;
      if (!node.children.empty()) {
        collapse_all_nodes(node.children);
      }
    }
  }
}

std::string escape_field(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '\\') {
      out += "\\\\";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string unescape_field(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      char n = input[i + 1];
      if (n == 't') {
        out.push_back('\t');
        i++;
        continue;
      }
      if (n == 'n') {
        out.push_back('\n');
        i++;
        continue;
      }
      if (n == '\\') {
        out.push_back('\\');
        i++;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

std::vector<std::string> split_tab(const std::string &line) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= line.size()) {
    size_t pos = line.find('\t', start);
    if (pos == std::string::npos) {
      parts.push_back(line.substr(start));
      break;
    }
    parts.push_back(line.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

std::string session_root_dir() {
  const char *override_home = std::getenv("JOT_CONFIG_HOME");
  if (override_home && *override_home) {
    return (fs::path(override_home) / "workspaces").string();
  }
#ifdef _WIN32
  const char *app_data = std::getenv("APPDATA");
  if (app_data && *app_data) {
    return (fs::path(app_data) / "jot" / "workspaces").string();
  }
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    return "";
  }
  return (fs::path(home) / ".config" / "jot" / "workspaces").string();
}

std::string session_file_for_root(const std::string &root) {
  if (root.empty()) {
    return "";
  }
  const std::string base = session_root_dir();
  if (base.empty()) {
    return "";
  }
  std::size_t hv = std::hash<std::string>{}(root);
  std::ostringstream oss;
  oss << base << "/" << std::hex << hv << ".session";
  return oss.str();
}

bool is_empty_scratch_buffer(const FileBuffer &buf) {
  return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 &&
         buf.line(0).empty();
}

long long file_time_key(fs::file_time_type time) {
  return time.time_since_epoch().count();
}
} // namespace

void Editor::toggle_sidebar() {
  show_sidebar = !show_sidebar;
  if (show_sidebar) {
    if (file_tree.empty()) {
      load_file_tree(root_dir);
    }
    focus_state = FOCUS_SIDEBAR;
  } else {
    focus_state = FOCUS_EDITOR;
  }
  needs_redraw = true;
}

void Editor::open_workspace(const std::string &path, bool restore_session) {
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec) {
    p = fs::path(path);
  }
  if (!fs::exists(p) || !fs::is_directory(p)) {
    set_message("Workspace not found: " + path);
    return;
  }

  std::string normalized = p.lexically_normal().string();
  track_recent_workspace(normalized);

  if (workspace_session_enabled && !workspace_session_root.empty() &&
      workspace_session_root != normalized) {
    save_workspace_session();
  }

  load_file_tree(normalized);
  show_sidebar = true;
  focus_state = FOCUS_SIDEBAR;
  workspace_session_enabled = true;
  workspace_session_root = root_dir;
  show_home_menu = false;

  // Reset editor buffers when entering a workspace so sessions do not mix.
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
  for (auto &pane : panes) {
    pane.buffer_id = 0;
    pane.tab_buffer_ids.clear();
    pane.tab_buffer_ids.push_back(0);
    pane.tab_scroll_index = 0;
  }

  bool restored = false;
  if (restore_session) {
    restored = restore_workspace_session();
  }

  if (!restored) {
    set_message("Workspace: " + root_dir);
  }
  refresh_git_status(true);
  needs_redraw = true;
}

bool Editor::resume_last_workspace_session() {
  while (!recent_workspaces.empty()) {
    const std::string candidate = recent_workspaces.front();
    std::error_code ec;
    if (fs::exists(candidate, ec) && !ec && fs::is_directory(candidate, ec)) {
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

void Editor::load_file_tree(const std::string &path) {
  const std::string old_root = normalize_path_for_tree(root_dir);
  std::unordered_set<std::string> old_expanded;
  collect_expanded_paths(file_tree, old_expanded);

  std::string old_selected_path;
  if (!file_tree.empty()) {
    std::vector<const FileNode *> old_flat;
    flatten_nodes_const(file_tree, old_flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)old_flat.size()) {
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
  if (!fs::exists(p) || !fs::is_directory(p)) {
    invalidate_sidebar_tree_cache();
    refresh_file_tree_watch_baseline();
    return;
  }

  const std::string new_root = normalize_path_for_tree(root_dir);
  const bool same_root = !old_root.empty() && old_root == new_root;

  build_tree(root_dir, file_tree, 0);
  invalidate_sidebar_tree_cache();
  arm_file_tree_watch();

  if (!same_root) {
    file_tree_selected = 0;
    file_tree_scroll = 0;
    refresh_file_tree_watch_baseline();
    return;
  }

  std::function<void(std::vector<FileNode> &)> restore_expanded =
      [&](std::vector<FileNode> &nodes) {
        for (auto &node : nodes) {
          if (!node.is_dir) {
            continue;
          }
          const std::string normalized = normalize_path_for_tree(node.path);
          if (old_expanded.find(normalized) != old_expanded.end()) {
            node.expanded = true;
            if (node.children.empty()) {
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
  if (!old_selected_path.empty()) {
    for (int i = 0; i < (int)refreshed_flat.size(); i++) {
      if (normalize_path_for_tree(refreshed_flat[i]->path) == old_selected_path) {
        file_tree_selected = i;
        break;
      }
    }
  }
  int view_h = std::max(1, ui->get_height() - status_height - tab_height - 2);
  int max_scroll = std::max(0, (int)refreshed_flat.size() - view_h);
  file_tree_scroll = std::clamp(old_scroll, 0, max_scroll);
  if (file_tree_selected < file_tree_scroll) {
    file_tree_scroll = file_tree_selected;
  } else if (file_tree_selected >= file_tree_scroll + view_h) {
    file_tree_scroll = std::clamp(file_tree_selected - view_h + 1, 0, max_scroll);
  }
  refresh_file_tree_watch_baseline();
}

void Editor::build_tree(const std::string &path, std::vector<FileNode> &nodes,
                        int depth) {
  try {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(path)) {
      std::string filename = entry.path().filename().string();
      if (!sidebar_show_hidden && !filename.empty() && filename[0] == '.') {
        continue;
      }
      entries.push_back(entry);
    }

    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry &a, const fs::directory_entry &b) {
                if (a.is_directory() != b.is_directory()) {
                  return a.is_directory() > b.is_directory();
                }
                return a.path().filename() < b.path().filename();
              });

    for (const auto &entry : entries) {
      FileNode node;
      node.name = entry.path().filename().string();
      node.path = entry.path().string();
      node.is_dir = entry.is_directory();
      node.expanded = false;
      node.depth = depth;
      nodes.push_back(node);
    }
  } catch (...) {
  }
}

void Editor::refresh_tree_children(FileNode &node) {
  if (!node.is_dir) {
    return;
  }
  node.children.clear();
  build_tree(node.path, node.children, node.depth + 1);
}

std::string Editor::build_file_tree_signature() const {
  if (root_dir.empty()) {
    return "";
  }

  std::error_code ec;
  fs::path root = fs::absolute(root_dir, ec);
  if (ec) {
    root = fs::path(root_dir);
  }
  root = root.lexically_normal();
  if (!fs::exists(root, ec) || ec || !fs::is_directory(root, ec)) {
    return "";
  }

  std::ostringstream sig;
  sig << root.string() << '\n';
  sig << "hidden=" << (sidebar_show_hidden ? 1 : 0) << '\n';

  auto append_path = [&](const fs::path &path, bool expanded) {
    std::error_code stat_ec;
    const bool is_dir = fs::is_directory(path, stat_ec);
    const bool exists = !stat_ec && fs::exists(path, stat_ec);
    uintmax_t size = 0;
    if (exists && !is_dir) {
      size = fs::file_size(path, stat_ec);
      if (stat_ec) {
        size = 0;
      }
    }
    long long mtime = 0;
    if (exists) {
      mtime = file_time_key(fs::last_write_time(path, stat_ec));
      if (stat_ec) {
        mtime = 0;
      }
    }

    fs::path rel = path.lexically_relative(root);
    sig << rel.string() << '\t' << (is_dir ? 'd' : 'f') << '\t'
        << (expanded ? '1' : '0') << '\t' << mtime << '\t' << size << '\n';
  };

  std::function<void(const std::vector<FileNode> &)> append_nodes =
      [&](const std::vector<FileNode> &nodes) {
        for (const auto &node : nodes) {
          fs::path path = fs::path(node.path).lexically_normal();
          append_path(path, node.is_dir && node.expanded);
          if (node.is_dir && node.expanded) {
            append_nodes(node.children);
          }
        }
      };

  append_path(root, true);
  append_nodes(file_tree);

  return sig.str();
}

void Editor::refresh_file_tree_watch_baseline() {
  file_tree_watch_signature_ = build_file_tree_signature();
  file_tree_watch_ready_ = !file_tree_watch_signature_.empty();
}

void Editor::poll_file_tree_changes() {
  if (root_dir.empty() || file_tree.empty()) {
    file_tree_watch_signature_.clear();
    file_tree_watch_ready_ = false;
    return;
  }

  const std::string signature = build_file_tree_signature();
  if (signature.empty()) {
    file_tree_watch_signature_.clear();
    file_tree_watch_ready_ = false;
    return;
  }

  if (!file_tree_watch_ready_) {
    file_tree_watch_signature_ = signature;
    file_tree_watch_ready_ = true;
    return;
  }

  if (signature == file_tree_watch_signature_) {
    return;
  }

  load_file_tree(root_dir);
  needs_redraw = true;
}

void Editor::arm_file_tree_watch() {
  if (root_dir.empty()) {
    if (!file_tree_event_watch_root_.empty()) {
      event_loop_.unwatch_path(file_tree_event_watch_root_);
      file_tree_event_watch_root_.clear();
    }
    return;
  }
  if (!file_tree_event_watch_root_.empty() &&
      file_tree_event_watch_root_ != root_dir) {
    event_loop_.unwatch_path(file_tree_event_watch_root_);
    file_tree_event_watch_root_.clear();
  }
  if (file_tree_event_watch_root_ == root_dir) {
    return;
  }
  if (event_loop_.watch_path(root_dir, [this](const std::string &) {
    poll_file_tree_changes();
  })) {
    file_tree_event_watch_root_ = root_dir;
  }
}

void Editor::save_workspace_session() {
  if (!workspace_session_enabled || workspace_session_root.empty()) {
    return;
  }

  const std::string session_path = session_file_for_root(workspace_session_root);
  if (session_path.empty()) {
    return;
  }

  std::vector<FileBuffer *> persisted;
  persisted.reserve(buffers.size());
  for (auto &buf : buffers) {
    if (!buf.filepath.empty()) {
      persisted.push_back(&buf);
    }
  }

  std::error_code ec;
  fs::create_directories(fs::path(session_path).parent_path(), ec);
  if (ec) {
    return;
  }

  std::ofstream out(session_path, std::ios::trunc);
  if (!out.is_open()) {
    return;
  }

  out << "version\t1\n";
  out << "root\t" << escape_field(workspace_session_root) << "\n";
  out << "show_sidebar\t" << (show_sidebar ? 1 : 0) << "\n";
  out << "sidebar_width\t" << effective_sidebar_width() << "\n";
  out << "sidebar_view\t"
      << (active_sidebar_view == SIDEBAR_VIEW_GIT ? "git" : "explorer")
      << "\n";
  out << "right_panel_width\t" << right_panel_width << "\n";
  out << "sidebar_show_hidden\t" << (sidebar_show_hidden ? 1 : 0) << "\n";

  std::string current_file;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
    current_file = buffers[current_buffer].filepath;
  }
  out << "current_file\t" << escape_field(current_file) << "\n";

  for (FileBuffer *buf : persisted) {
    if (!buf->is_lazy()) {
      Folding::refresh_ranges(buf->fold_ranges, buf->lines,
                              get_file_extension(buf->filepath));
    }
    std::string fold_payload =
        Folding::encode_collapsed_ranges(buf->fold_ranges);
    out << "file\t" << escape_field(buf->filepath) << "\t" << buf->cursor.y
        << "\t" << buf->cursor.x << "\t" << buf->scroll_offset << "\t"
        << buf->scroll_x << "\t" << (buf->is_preview ? 1 : 0) << "\t"
        << escape_field(fold_payload) << "\n";
  }
}

bool Editor::restore_workspace_session() {
  if (!workspace_session_enabled || workspace_session_root.empty()) {
    return false;
  }

  const std::string session_path = session_file_for_root(workspace_session_root);
  if (session_path.empty()) {
    return false;
  }

  std::ifstream in(session_path);
  if (!in.is_open()) {
    return false;
  }

  struct Entry {
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
  std::string target_current_file;
  std::vector<Entry> entries;
  auto clamp_restored_right_panel_width = [&]() {
    int max_w = max_right_panel_width();
    int min_w = std::min(min_right_panel_width(), max_w);
    return std::clamp(restored_right_panel_width, min_w, max_w);
  };

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    std::vector<std::string> parts = split_tab(line);
    if (parts.empty()) {
      continue;
    }
    const std::string key = parts[0];
    if (key == "show_sidebar" && parts.size() >= 2) {
      restored_show_sidebar = (parts[1] == "1");
    } else if (key == "sidebar_width" && parts.size() >= 2) {
      try {
        restored_sidebar_width = std::stoi(parts[1]);
      } catch (...) {
      }
    } else if (key == "sidebar_view" && parts.size() >= 2) {
      restored_sidebar_view =
          parts[1] == "git" ? SIDEBAR_VIEW_GIT : SIDEBAR_VIEW_EXPLORER;
    } else if (key == "right_panel_width" && parts.size() >= 2) {
      try {
        restored_right_panel_width = std::stoi(parts[1]);
      } catch (...) {
      }
    } else if (key == "sidebar_show_hidden" && parts.size() >= 2) {
      restored_hidden = (parts[1] == "1");
    } else if (key == "current_file" && parts.size() >= 2) {
      target_current_file = unescape_field(parts[1]);
    } else if (key == "file" && parts.size() >= 7) {
      Entry e;
      e.path = unescape_field(parts[1]);
      try {
        e.cy = std::stoi(parts[2]);
        e.cx = std::stoi(parts[3]);
        e.scroll = std::stoi(parts[4]);
        e.scroll_x = std::stoi(parts[5]);
        e.preview = (parts[6] == "1");
        if (parts.size() >= 8) {
          e.has_collapsed_folds = true;
          e.collapsed_folds =
              Folding::decode_collapsed_ranges(unescape_field(parts[7]));
        }
        entries.push_back(e);
      } catch (...) {
      }
    }
  }

  if (entries.empty()) {
    show_sidebar = restored_show_sidebar;
    active_sidebar_view = restored_sidebar_view;
    sidebar_width =
        std::clamp(restored_sidebar_width, min_sidebar_width(),
                   max_sidebar_width());
    right_panel_width =
        clamp_restored_right_panel_width();
    sidebar_show_hidden = restored_hidden;
    return false;
  }

  sidebar_width =
      std::clamp(restored_sidebar_width, min_sidebar_width(),
                 max_sidebar_width());
  active_sidebar_view = restored_sidebar_view;
  right_panel_width =
      clamp_restored_right_panel_width();
  sidebar_show_hidden = restored_hidden;
  load_file_tree(workspace_session_root);

  if (buffers.size() == 1 && is_empty_scratch_buffer(buffers[0])) {
    buffers.clear();
    current_buffer = 0;
    for (auto &pane : panes) {
      pane.buffer_id = 0;
      pane.tab_buffer_ids.clear();
      pane.tab_buffer_ids.push_back(0);
      pane.tab_scroll_index = 0;
    }
  }

  preview_buffer_index = -1;

  std::vector<std::string> restored_paths;
  restored_paths.reserve(entries.size());
  for (const auto &entry : entries) {
    std::error_code ec;
    if (!fs::exists(entry.path, ec) || ec || fs::is_directory(entry.path, ec)) {
      continue;
    }

    open_file(entry.path, false);
    restored_paths.push_back(entry.path);

    if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
      FileBuffer &buf = buffers[current_buffer];
      buf.cursor.y = std::clamp(entry.cy, 0, std::max(0, (int)buf.line_count() - 1));
      buf.cursor.x =
          std::clamp(entry.cx, 0, (int)buf.line(buf.cursor.y).size());
      buf.preferred_x = buf.cursor.x;
      buf.scroll_offset = std::max(0, entry.scroll);
      buf.scroll_x = std::max(0, entry.scroll_x);
      buf.is_preview = entry.preview;
      if (!buf.is_lazy()) {
        Folding::refresh_ranges(buf.fold_ranges, buf.lines,
                                get_file_extension(buf.filepath));
      }
      if (entry.has_collapsed_folds) {
        Folding::apply_collapsed_ranges(buf.fold_ranges, entry.collapsed_folds);
      }
      while (buf.cursor.y > 0 &&
             Folding::is_line_hidden(buf.fold_ranges, buf.cursor.y)) {
        buf.cursor.y--;
      }
      buf.cursor.x =
          std::clamp(buf.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
      buf.preferred_x = buf.cursor.x;
      if (buf.is_preview) {
        preview_buffer_index = current_buffer;
      }
    }
  }

  if (restored_paths.empty()) {
    return false;
  }

  int desired_buffer = -1;
  for (int i = 0; i < (int)buffers.size(); i++) {
    if (buffers[i].filepath == target_current_file) {
      desired_buffer = i;
      break;
    }
  }
  if (desired_buffer < 0) {
    desired_buffer = std::max(0, (int)buffers.size() - 1);
  }

  current_buffer = desired_buffer;
  if (!panes.empty()) {
    auto &pane = get_pane();
    pane.buffer_id = current_buffer;
    if (std::find(pane.tab_buffer_ids.begin(), pane.tab_buffer_ids.end(),
                  current_buffer) == pane.tab_buffer_ids.end()) {
      pane.tab_buffer_ids.push_back(current_buffer);
    }
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20) {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    reveal_local_tab(pane, find_local_tab_index(pane, current_buffer), draw_w);
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  show_sidebar = restored_show_sidebar;
  active_sidebar_view = restored_sidebar_view;
  sidebar_width = effective_sidebar_width();
  set_message("Workspace restored: " + root_dir);
  return true;
}

void Editor::handle_sidebar_input(int ch) {
  static std::string pending_delete_path;
  static long long pending_delete_deadline_ms = 0;

  auto now_ms = []() -> long long {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
  };
  auto normalize_path = [](const std::string &path) {
    std::error_code ec;
    fs::path p = fs::absolute(path, ec);
    if (ec) {
      p = fs::path(path);
    }
    return p.lexically_normal().string();
  };
  auto starts_with_path = [](const std::string &child, const std::string &parent) {
    if (child.size() < parent.size()) {
      return false;
    }
    if (child.compare(0, parent.size(), parent) != 0) {
      return false;
    }
    return child.size() == parent.size() || child[parent.size()] == '/' ||
           child[parent.size()] == '\\';
  };
  auto close_buffers_for_path = [&](const std::string &target_abs, bool is_dir) {
    const std::string norm_target = normalize_path(target_abs);
    for (int i = (int)buffers.size() - 1; i >= 0; --i) {
      if (buffers[i].filepath.empty()) {
        continue;
      }
      std::string buf_path = normalize_path(buffers[i].filepath);
      bool match =
          (!is_dir && buf_path == norm_target) ||
          (is_dir && starts_with_path(buf_path, norm_target));
      if (match) {
        close_buffer_at(i);
      }
    }
  };
  auto to_workspace_relative = [&](const std::string &abs_path) {
    fs::path rel = fs::path(abs_path).lexically_relative(fs::path(root_dir));
    std::string rel_s = rel.string();
    if (!rel_s.empty() && rel_s != "." &&
        rel_s.find("..") != 0) {
      return rel_s;
    }
    return abs_path;
  };
  auto shell_quote_local = [](const std::string &value) {
    std::string out = "'";
    out.reserve(value.size() + 8);
    for (char c : value) {
      if (c == '\'') {
        out += "'\\''";
      } else {
        out.push_back(c);
      }
    }
    out.push_back('\'');
    return out;
  };
  auto limit_lines_local = [](const std::string &text, int max_lines) {
    if (max_lines <= 0) {
      return std::string();
    }
    std::istringstream iss(text);
    std::string out;
    std::string line;
    int count = 0;
    while (count < max_lines && std::getline(iss, line)) {
      if (count > 0) {
        out += '\n';
      }
      out += line;
      count++;
    }
    if (std::getline(iss, line)) {
      out += "\n...";
    }
    return out;
  };

  if (ch == '\t') {
    active_sidebar_view = active_sidebar_view == SIDEBAR_VIEW_EXPLORER
                              ? SIDEBAR_VIEW_GIT
                              : SIDEBAR_VIEW_EXPLORER;
    if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
      refresh_git_status(true);
    }
    needs_redraw = true;
    return;
  }

  if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int reserved_terminal_h = 0;
    if (show_integrated_terminal && !integrated_terminals.empty()) {
      reserved_terminal_h =
          std::clamp(integrated_terminal_height, 5,
                     std::max(5, ui->get_height() / 2));
    }
    const int view_h = std::max(1, ui->get_height() - status_height -
                                       tab_height - reserved_terminal_h - 2);
    auto clamp_scroll = [&]() {
      int max_scroll = std::max(0, (int)git_rows.size() - view_h);
      git_sidebar_scroll = std::clamp(git_sidebar_scroll, 0, max_scroll);
    };
    auto ensure_selected_visible = [&]() {
      if (git_sidebar_selected < git_sidebar_scroll) {
        git_sidebar_scroll = git_sidebar_selected;
      } else if (git_sidebar_selected >= git_sidebar_scroll + view_h) {
        git_sidebar_scroll = git_sidebar_selected - view_h + 1;
      }
      clamp_scroll();
    };

    if (!git_rows.empty()) {
      git_sidebar_selected =
          std::clamp(git_sidebar_selected, 0, (int)git_rows.size() - 1);
    } else {
      git_sidebar_selected = 0;
    }
    clamp_scroll();

    auto selected_path = [&]() -> std::string {
      if (git_rows.empty() || git_sidebar_selected < 0 ||
          git_sidebar_selected >= (int)git_rows.size()) {
        return "";
      }
      return git_rows[(size_t)git_sidebar_selected].path;
    };
    auto selected_rel = [&]() -> std::string {
      if (git_rows.empty() || git_sidebar_selected < 0 ||
          git_sidebar_selected >= (int)git_rows.size()) {
        return "";
      }
      return git_rows[(size_t)git_sidebar_selected].relative_path;
    };

    if (ch == 1008 || ch == 'k') {
      if (git_sidebar_selected > 0) {
        git_sidebar_selected--;
        ensure_selected_visible();
        needs_redraw = true;
      }
      return;
    }
    if (ch == 1009 || ch == 'j') {
      if (git_sidebar_selected < (int)git_rows.size() - 1) {
        git_sidebar_selected++;
        ensure_selected_visible();
        needs_redraw = true;
      }
      return;
    }
    if (ch == 1015) {
      git_sidebar_selected = std::max(0, git_sidebar_selected - view_h);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1016) {
      git_sidebar_selected = std::min(std::max(0, (int)git_rows.size() - 1),
                                      git_sidebar_selected + view_h);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1012) {
      git_sidebar_selected = 0;
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == 1013) {
      git_sidebar_selected = std::max(0, (int)git_rows.size() - 1);
      ensure_selected_visible();
      needs_redraw = true;
      return;
    }
    if (ch == '\n' || ch == 13 || ch == 'l' || ch == 1010) {
      std::string path = selected_path();
      if (!path.empty()) {
        open_file(path, false);
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
      return;
    }
    if (ch == 'r' || ch == 'R') {
      refresh_git_status(true);
      message = has_git_repo() ? "Git: refreshed" : "Git: not a repository";
      needs_redraw = true;
      return;
    }
    if (ch == 'a') {
      refresh_git_status(true);
      if (!has_git_repo()) {
        message = "Git: not a repository";
      } else if (git_stage_all()) {
        message = "Git staged all changes";
      } else {
        message = "Git stage all failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 's') {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo()) {
        message = "Git: select a changed file";
      } else if (git_stage_path(path)) {
        message = "Git staged: " + selected_rel();
      } else {
        message = "Git stage failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 'u') {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo()) {
        message = "Git: select a changed file";
      } else if (git_unstage_path(path)) {
        message = "Git unstaged: " + selected_rel();
      } else {
        message = "Git unstage failed";
      }
      needs_redraw = true;
      return;
    }
    if (ch == 'd' || ch == 'D') {
      std::string path = selected_path();
      refresh_git_status(true);
      if (path.empty() || !has_git_repo()) {
        message = "Git: select a changed file";
      } else {
        std::string rel = to_git_relative_path(path);
        std::string diff =
            run_git_capture(std::string(ch == 'D' ? "diff --staged -- "
                                                   : "diff -- ") +
                            shell_quote_local(rel));
        if (diff.empty()) {
          message = ch == 'D' ? "Git diff: no staged changes for " + rel
                              : "Git diff: no unstaged changes for " + rel;
        } else {
          show_popup(limit_lines_local(diff, 18), 2, tab_height + 1);
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
  auto clamp_scroll = [&]() {
    int max_scroll = std::max(0, (int)flat.size() - view_h);
    file_tree_scroll = std::clamp(file_tree_scroll, 0, max_scroll);
  };
  auto ensure_selected_visible = [&]() {
    if (file_tree_selected < file_tree_scroll) {
      file_tree_scroll = file_tree_selected;
    } else if (file_tree_selected >= file_tree_scroll + view_h) {
      file_tree_scroll = file_tree_selected - view_h + 1;
    }
    clamp_scroll();
  };

  file_tree_selected =
      std::clamp(file_tree_selected, 0, std::max(0, (int)flat.size() - 1));
  clamp_scroll();

  if (ch == 1008 || ch == 'k') { // Up
    if (file_tree_selected > 0) {
      file_tree_selected--;
      ensure_selected_visible();
      needs_redraw = true;
    }
    return;
  }

  if (ch == 1009 || ch == 'j') { // Down
    if (file_tree_selected < (int)flat.size() - 1) {
      file_tree_selected++;
      ensure_selected_visible();
      needs_redraw = true;
    }
    return;
  }

  if (ch == 1015) { // Page Up
    file_tree_selected = std::max(0, file_tree_selected - view_h);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1016) { // Page Down
    file_tree_selected =
        std::min(std::max(0, (int)flat.size() - 1), file_tree_selected + view_h);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1012) { // Home
    file_tree_selected = 0;
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == 1013) { // End
    file_tree_selected = std::max(0, (int)flat.size() - 1);
    ensure_selected_visible();
    needs_redraw = true;
    return;
  }

  if (ch == '*' || ch == 'Z') {
    std::function<void(std::vector<FileNode> &)> expand_all =
        [&](std::vector<FileNode> &nodes) {
          for (auto &node : nodes) {
            if (node.is_dir) {
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

  if (ch == 'z') {
    collapse_all_nodes(file_tree);
    invalidate_sidebar_tree_cache();
    file_tree_selected = 0;
    file_tree_scroll = 0;
    message = "Explorer: collapsed all";
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13 || ch == 'l' || ch == 1010) {
    flatten_nodes_mut(file_tree, flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size()) {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir) {
        if (!node->expanded) {
          node->expanded = true;
          refresh_tree_children(*node);
          invalidate_sidebar_tree_cache();
        } else if (ch == '\n' || ch == 13) {
          node->expanded = false;
          invalidate_sidebar_tree_cache();
        }
        needs_redraw = true;
      } else {
        load_file(node->path);
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
    }
    return;
  }

  if (ch == 'h' || ch == 1011) {
    flatten_nodes_mut(file_tree, flat);
    if (file_tree_selected >= 0 && file_tree_selected < (int)flat.size()) {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir && node->expanded) {
        node->expanded = false;
        invalidate_sidebar_tree_cache();
        needs_redraw = true;
      } else if (node->depth > 0) {
        int target_depth = node->depth - 1;
        for (int i = file_tree_selected - 1; i >= 0; i--) {
          if (flat[i]->depth == target_depth) {
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

  if (ch == 'r' || ch == 'R') {
    if (ch == 'r') {
      if (!flat.empty() && file_tree_selected >= 0 &&
          file_tree_selected < (int)flat.size()) {
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
    if (!flat.empty() && file_tree_selected >= 0 &&
        file_tree_selected < (int)flat.size()) {
      selected_path = flat[file_tree_selected]->path;
    }

    load_file_tree(root_dir);

    if (!selected_path.empty()) {
      std::vector<FileNode *> refreshed;
      flatten_nodes_mut(file_tree, refreshed);
      for (int i = 0; i < (int)refreshed.size(); i++) {
        if (refreshed[i]->path == selected_path) {
          file_tree_selected = i;
          break;
        }
      }
      int max_scroll = std::max(0, (int)refreshed.size() - view_h);
      file_tree_scroll =
          std::clamp(file_tree_selected - view_h / 2, 0, max_scroll);
    }

    message = "Explorer: refreshed";
    needs_redraw = true;
    return;
  }

  if (ch == 'a') {
    std::string base = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 &&
        file_tree_selected < (int)flat.size()) {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir) {
        base = node->path;
      } else {
        base = fs::path(node->path).parent_path().string();
      }
    }
    std::string rel = to_workspace_relative(base);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\') {
      rel += "/";
    } else if (rel == ".") {
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

  if (ch == 'A') {
    std::string base = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 &&
        file_tree_selected < (int)flat.size()) {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir) {
        base = node->path;
      } else {
        base = fs::path(node->path).parent_path().string();
      }
    }
    std::string rel = to_workspace_relative(base);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\') {
      rel += "/";
    } else if (rel == ".") {
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

  if (ch == 'i') {
    if (flat.empty() || file_tree_selected < 0 ||
        file_tree_selected >= (int)flat.size()) {
      return;
    }
    FileNode *node = flat[file_tree_selected];
    if (node->is_dir) {
      message = "Select a C++ header or source file";
      needs_redraw = true;
      return;
    }
    fs::path selected(node->path);
    if (!CppAssist::is_header_path(selected) &&
        !CppAssist::is_source_path(selected)) {
      message = "Select a C++ header or source file";
      needs_redraw = true;
      return;
    }
    execute_command("cppimpl " + to_workspace_relative(node->path));
    return;
  }

  if (ch == 'C') {
    std::string target = root_dir;
    if (!flat.empty() && file_tree_selected >= 0 &&
        file_tree_selected < (int)flat.size()) {
      FileNode *node = flat[file_tree_selected];
      if (node->is_dir) {
        target = node->path;
      } else {
        fs::path selected(node->path);
        if (CppAssist::is_header_path(selected) ||
            CppAssist::is_source_path(selected)) {
          target = selected.replace_extension("").string();
        } else {
          target = selected.parent_path().string();
        }
      }
    }
    std::string rel = to_workspace_relative(target);
    if (!rel.empty() && rel != "." && rel.back() != '/' && rel.back() != '\\' &&
        (target == root_dir || fs::is_directory(target))) {
      rel += "/";
    } else if (rel == ".") {
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

  if (ch == 'd') {
    if (flat.empty() || file_tree_selected < 0 ||
        file_tree_selected >= (int)flat.size()) {
      return;
    }
    FileNode *node = flat[file_tree_selected];
    const std::string node_path = normalize_path(node->path);
    const std::string node_name = node->name;
    const long long now = now_ms();

    if (pending_delete_path == node_path && now <= pending_delete_deadline_ms) {
      std::error_code ec;
      bool is_dir = fs::is_directory(node_path, ec);
      close_buffers_for_path(node_path, is_dir);
      if (is_dir) {
        fs::remove_all(node_path, ec);
      } else {
        fs::remove(node_path, ec);
      }
      pending_delete_path.clear();
      pending_delete_deadline_ms = 0;
      if (ec) {
        message = "Delete failed: " + ec.message();
      } else {
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

  if (ch == 'g' || ch == 'G') {
    refresh_git_status(true);
    if (has_git_repo()) {
      message = "Git: " + git_branch + " (+" +
                std::to_string(git_staged_count) + " ~" +
                std::to_string(git_unstaged_count) + " ?" +
                std::to_string(git_untracked_count) + ")";
    } else {
      message = "Git: not a repository";
    }
    needs_redraw = true;
    return;
  }

  if (ch == '.') {
    std::string selected_path;
    if (!flat.empty() && file_tree_selected >= 0 &&
        file_tree_selected < (int)flat.size()) {
      selected_path = flat[file_tree_selected]->path;
    }

    sidebar_show_hidden = !sidebar_show_hidden;
    load_file_tree(root_dir);

    if (!selected_path.empty()) {
      std::vector<FileNode *> refreshed;
      flatten_nodes_mut(file_tree, refreshed);
      for (int i = 0; i < (int)refreshed.size(); i++) {
        if (refreshed[i]->path == selected_path) {
          file_tree_selected = i;
          break;
        }
      }
    }

    message = sidebar_show_hidden ? "Explorer: showing hidden files"
                                  : "Explorer: hiding hidden files";
    needs_redraw = true;
    return;
  }

  if (ch == 127 || ch == 8) {
    fs::path current(root_dir);
    if (current.has_parent_path()) {
      fs::path parent = current.parent_path();
      if (!parent.empty() && parent != current) {
        open_workspace(parent.string(), true);
      }
    }
    return;
  }
}

void Editor::handle_sidebar_mouse(int x, int y, bool is_click,
                                  bool is_double_click) {
  if (!is_click)
    return;

  int rel_y = y - tab_height;
  if (rel_y < 0)
    return;
  if (x < sidebar_activity_rail_width()) {
    if (rel_y == 1) {
      active_sidebar_view = SIDEBAR_VIEW_EXPLORER;
      needs_redraw = true;
    } else if (rel_y == 3) {
      active_sidebar_view = SIDEBAR_VIEW_GIT;
      refresh_git_status(true);
      needs_redraw = true;
    }
    return;
  }

  if (active_sidebar_view == SIDEBAR_VIEW_GIT) {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int sidebar_row = y - tab_height - 1;
    if (sidebar_row < 0)
      return;
    int row = sidebar_row + git_sidebar_scroll;
    if (row >= 0 && row < (int)git_rows.size()) {
      git_sidebar_selected = row;
      open_file(git_rows[(size_t)row].path, !is_double_click);
      if (is_double_click) {
        focus_state = FOCUS_EDITOR;
      }
      needs_redraw = true;
    }
    return;
  }

  std::vector<FileNode *> flat;
  flatten_nodes_mut(file_tree, flat);

  // Sidebar now has 1-line header, so tree rows begin after that.
  int sidebar_row = y - tab_height - 1;
  if (sidebar_row < 0)
    return;
  int row = sidebar_row + file_tree_scroll;
  if (row >= 0 && row < (int)flat.size()) {
    FileNode *node = flat[row];
    file_tree_selected = row;

    if (node->is_dir) {
      node->expanded = !node->expanded;
      if (node->expanded) {
        refresh_tree_children(*node);
      }
      invalidate_sidebar_tree_cache();
    } else {
      open_file(node->path, !is_double_click);
      if (is_double_click) {
        focus_state = FOCUS_EDITOR;
      }
    }
    needs_redraw = true;
  }
}
