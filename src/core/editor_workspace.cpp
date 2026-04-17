#include "editor.h"
#include <algorithm>
#include <functional>

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

void Editor::handle_sidebar_input(int ch) {
  if (ch == 1008 || ch == 'k') { // Up
    if (file_tree_selected > 0) {
      file_tree_selected--;
      if (file_tree_selected < file_tree_scroll) {
        file_tree_scroll = file_tree_selected;
      }
      needs_redraw = true;
    }
  } else if (ch == 1009 || ch == 'j') { // Down
    std::vector<FileNode *> flat;
    std::function<void(std::vector<FileNode> &)> flatten =
        [&](std::vector<FileNode> &nodes) {
          for (auto &node : nodes) {
            flat.push_back(&node);
            if (node.is_dir && node.expanded) {
              flatten(node.children);
            }
          }
        };
    flatten(file_tree);

    if (file_tree_selected < (int)flat.size() - 1) {
      file_tree_selected++;
      int h = ui->get_height() - status_height - 2;
      if (file_tree_selected >= file_tree_scroll + h) {
        file_tree_scroll = file_tree_selected - h + 1;
      }
      needs_redraw = true;
    }
  } else if (ch == '\n' || ch == 13 || ch == 'l' || ch == 1010) {
    std::vector<FileNode *> flat;
    std::function<void(std::vector<FileNode> &)> flatten =
        [&](std::vector<FileNode> &nodes) {
          for (auto &node : nodes) {
            flat.push_back(&node);
            if (node.is_dir && node.expanded) {
              flatten(node.children);
            }
          }
        };
    flatten(file_tree);

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
  } else if (ch == 'h' || ch == 1011) {
    std::vector<FileNode *> flat;
    std::function<void(std::vector<FileNode> &)> flatten =
        [&](std::vector<FileNode> &nodes) {
          for (auto &node : nodes) {
            flat.push_back(&node);
            if (node.is_dir && node.expanded) {
              flatten(node.children);
            }
          }
        };
    flatten(file_tree);

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
            if (file_tree_selected < file_tree_scroll) {
              file_tree_scroll = file_tree_selected;
            }
            needs_redraw = true;
            break;
          }
        }
      }
    }
  } else if (ch == 'r' || ch == 'R') {
    load_file_tree(root_dir);
    needs_redraw = true;
  } else if (ch == '.') {
    sidebar_show_hidden = !sidebar_show_hidden;
    load_file_tree(root_dir);
    message = sidebar_show_hidden ? "Explorer: showing hidden files"
                                  : "Explorer: hiding hidden files";
    needs_redraw = true;
  } else if (ch == 127 || ch == 8) {
    fs::path current(root_dir);
    if (current.has_parent_path()) {
      fs::path parent = current.parent_path();
      if (!parent.empty() && parent != current) {
        load_file_tree(parent.string());
        message = "Workspace: " + root_dir;
        needs_redraw = true;
      }
    }
  }
}

void Editor::handle_sidebar_mouse(int x, int y, bool is_click) {
  if (!is_click)
    return;

  (void)x;

  std::vector<FileNode *> flat;
  std::function<void(std::vector<FileNode> &)> flatten =
      [&](std::vector<FileNode> &nodes) {
        for (auto &node : nodes) {
          flat.push_back(&node);
          if (node.is_dir && node.expanded) {
            flatten(node.children);
          }
        }
      };
  flatten(file_tree);

  int sidebar_row = y - tab_height;
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
      load_file(node->path);
    }
    needs_redraw = true;
  }
}
