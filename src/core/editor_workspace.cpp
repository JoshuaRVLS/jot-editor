#include "editor.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

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
  const char *home = std::getenv("HOME");
  if (!home || !*home) {
    return "";
  }
  return std::string(home) + "/.config/jot/workspaces";
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
  return buf.filepath.empty() && !buf.modified && buf.lines.size() == 1 &&
         buf.lines[0].empty();
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
  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.preferred_x = 0;
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  fb.is_preview = false;
  buffers.push_back(fb);
  current_buffer = 0;
  tab_scroll_index = 0;
  preview_buffer_index = -1;
  for (auto &pane : panes) {
    pane.buffer_id = 0;
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

void Editor::load_file_tree(const std::string &path) {
  file_tree.clear();
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec)
    p = fs::path(path);
  root_dir = p.lexically_normal().string();
  if (!fs::exists(p) || !fs::is_directory(p))
    return;
  file_tree_selected = 0;
  file_tree_scroll = 0;
  build_tree(root_dir, file_tree, 0);
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

void Editor::save_workspace_session() {
  if (!workspace_session_enabled || workspace_session_root.empty()) {
    return;
  }

  const std::string session_path = session_file_for_root(workspace_session_root);
  if (session_path.empty()) {
    return;
  }

  std::vector<const FileBuffer *> persisted;
  persisted.reserve(buffers.size());
  for (const auto &buf : buffers) {
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
  out << "sidebar_show_hidden\t" << (sidebar_show_hidden ? 1 : 0) << "\n";

  std::string current_file;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
    current_file = buffers[current_buffer].filepath;
  }
  out << "current_file\t" << escape_field(current_file) << "\n";

  for (const FileBuffer *buf : persisted) {
    out << "file\t" << escape_field(buf->filepath) << "\t" << buf->cursor.y
        << "\t" << buf->cursor.x << "\t" << buf->scroll_offset << "\t"
        << buf->scroll_x << "\t" << (buf->is_preview ? 1 : 0) << "\n";
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
  };

  bool restored_show_sidebar = true;
  bool restored_hidden = sidebar_show_hidden;
  std::string target_current_file;
  std::vector<Entry> entries;

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
        entries.push_back(e);
      } catch (...) {
      }
    }
  }

  if (entries.empty()) {
    show_sidebar = restored_show_sidebar;
    sidebar_show_hidden = restored_hidden;
    return false;
  }

  sidebar_show_hidden = restored_hidden;
  load_file_tree(workspace_session_root);

  if (buffers.size() == 1 && is_empty_scratch_buffer(buffers[0])) {
    buffers.clear();
    current_buffer = 0;
    for (auto &pane : panes) {
      pane.buffer_id = 0;
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
      buf.cursor.y = std::clamp(entry.cy, 0, std::max(0, (int)buf.lines.size() - 1));
      buf.cursor.x =
          std::clamp(entry.cx, 0, (int)buf.lines[buf.cursor.y].size());
      buf.preferred_x = buf.cursor.x;
      buf.scroll_offset = std::max(0, entry.scroll);
      buf.scroll_x = std::max(0, entry.scroll_x);
      buf.is_preview = entry.preview;
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
    get_pane().buffer_id = current_buffer;
  }

  clamp_cursor(get_pane().buffer_id);
  ensure_cursor_visible();
  show_sidebar = restored_show_sidebar;
  set_message("Workspace restored: " + root_dir);
  return true;
}

void Editor::handle_sidebar_input(int ch) {
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
              if (node.children.empty()) {
                build_tree(node.path, node.children, node.depth + 1);
              }
              expand_all(node.children);
            }
          }
        };
    expand_all(file_tree);
    message = "Explorer: expanded all";
    needs_redraw = true;
    return;
  }

  if (ch == 'z') {
    collapse_all_nodes(file_tree);
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
          build_tree(node->path, node->children, node->depth + 1);
        } else if (ch == '\n' || ch == 13) {
          node->expanded = false;
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

  if (ch == 'g' || ch == 'G') {
    refresh_git_status(true);
    if (has_git_repo()) {
      message = "Git: " + git_branch + " (" + std::to_string(git_dirty_count) +
                " changes)";
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

  (void)x;

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
      if (node->expanded && node->children.empty()) {
        build_tree(node->path, node->children, node->depth + 1);
      }
    } else {
      open_file(node->path, !is_double_click);
      if (is_double_click) {
        focus_state = FOCUS_EDITOR;
      }
    }
    needs_redraw = true;
  }
}
