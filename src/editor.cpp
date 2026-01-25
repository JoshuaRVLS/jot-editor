#include "editor.h"
#include "python_api.h"

Editor::Editor() {
  config.load();

  running = true;
  current_pane = 0;
  waiting_for_space_f = false;
  show_minimap = false;
  minimap_width = 10; // Fixed width for now
  show_search = false;
  show_command_palette = false;
  command_palette_selected = 0;
  search_result_index = -1;
  search_case_sensitive = false;
  show_save_prompt = false;
  show_quit_prompt = false;

  popup.visible = false;
  popup.x = 0;
  popup.y = 0;
  popup.w = 0;
  popup.h = 0;
  popup.text = "";

  show_sidebar = false;
  sidebar_width = 30;
  root_dir = ".";
  file_tree_selected = 0;
  file_tree_scroll = 0;
  focus_state = FOCUS_EDITOR;

  status_height = 2;
  tab_height = 1;
  tab_size = config.get_int("tab_size", 4);
  auto_indent = config.get_bool("auto_indent", true);
  needs_redraw = true;
  mouse_selecting = false;
  idle_frame_count = 0;
  cursor_blink_frame = 0;
  cursor_visible = true;
  show_context_menu = false;
  context_menu_x = 0;
  context_menu_y = 0;
  context_menu_selected = 0;

  // Mode initialization removed
  // mode = MODE_NORMAL;
  // has_pending_key = false;
  // pending_key = 0;
  // visual_start = {0, 0}; // Removed

  python_api = new PythonAPI(this);
  python_api->init();

  terminal.init();
  ui = new UI(&terminal);
  ui->resize(terminal.get_width(), terminal.get_height());

  int h = terminal.get_height();
  int w = terminal.get_width();
  create_pane(0, tab_height, w - minimap_width, h - tab_height - status_height,
              -1);

  FileBuffer fb;
  fb.lines.push_back("");
  fb.cursor = {0, 0};
  fb.selection = {{0, 0}, {0, 0}, false};
  fb.scroll_offset = 0;
  fb.scroll_x = 0;
  fb.modified = false;
  buffers.push_back(fb);
  panes[0].buffer_id = 0;
}

Editor::~Editor() {
  if (python_api) {
    python_api->cleanup();
    delete python_api;
  }
  delete ui;
  terminal.cleanup();
}

void Editor::set_message(const std::string &msg) {
  message = msg;
  needs_redraw = true;
}

void Editor::set_diagnostics(const std::string &filepath,
                             const std::vector<Diagnostic> &diagnostics) {
  for (auto &buf : buffers) {
    if (fs::equivalent(buf.filepath, filepath) || buf.filepath == filepath) {
      buf.diagnostics = diagnostics;
      needs_redraw = true;
      return;
    }
  }
}

void Editor::add_diagnostic(const std::string &filepath,
                            const Diagnostic &diagnostic) {
  for (auto &buf : buffers) {
    if (fs::equivalent(buf.filepath, filepath) || buf.filepath == filepath) {
      buf.diagnostics.push_back(diagnostic);
      needs_redraw = true;
      return;
    }
  }
}

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
  fs::path p(path);
  root_dir = path;
  if (!fs::exists(p) || !fs::is_directory(p))
    return;
  build_tree(path, file_tree, 0);
}

void Editor::build_tree(const std::string &path, std::vector<FileNode> &nodes,
                        int depth) {
  try {
    std::vector<fs::directory_entry> entries;
    for (const auto &entry : fs::directory_iterator(path)) {
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
  if (ch == 1008) { // Up
    if (file_tree_selected > 0) {
      file_tree_selected--;
      // Auto scroll?
      if (file_tree_selected < file_tree_scroll) {
        file_tree_scroll = file_tree_selected;
      }
      needs_redraw = true;
    }
  } else if (ch == 1009) { // Down
    // Need to know max items.
    // We can flatten temporarily to check size.
    // Or track size in build_tree (unreliable if expanded changes).
    // Let's count visible items efficiently or just standard max (e.g. 1000?
    // no). Best approach: flatten to get size.
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
      // Auto scroll
      int h = ui->get_height() - status_height - 2; // Approx visible area
      if (file_tree_selected >= file_tree_scroll + h) {
        file_tree_scroll = file_tree_selected - h + 1;
      }
      needs_redraw = true;
    }
  } else if (ch == '\n' || ch == 13) { // Enter (\n or \r)
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
        node->expanded = !node->expanded;
        if (node->expanded && node->children.empty()) {
          build_tree(node->path, node->children, node->depth + 1);
        }
        needs_redraw = true;
      } else {
        load_file(node->path);
        focus_state = FOCUS_EDITOR;
        needs_redraw = true;
      }
    }
  }
}

void Editor::handle_sidebar_mouse(int x, int y, bool is_click) {
  if (!is_click)
    return;

  // Simple flatten to find node at y
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

  int row = y + file_tree_scroll;
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

void Editor::show_popup(const std::string &text, int x, int y) {
  popup.text = text;
  popup.x = x;
  popup.y = y;

  int max_w = 0;
  int h = 0;
  std::stringstream ss(text);
  std::string line;
  while (std::getline(ss, line)) {
    if ((int)line.length() > max_w)
      max_w = line.length();
    h++;
  }
  popup.w = max_w + 2;
  popup.h = h + 2;
  popup.visible = true;
  needs_redraw = true;
}

void Editor::hide_popup() {
  popup.visible = false;
  needs_redraw = true;
}
