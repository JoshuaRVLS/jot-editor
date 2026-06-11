#include "editor.h"
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace {
std::string path_relative_to_root(const std::string &path,
                                  const std::string &root_dir) {
  if (path.empty()) {
    return "";
  }
  std::error_code ec;
  fs::path abs = fs::absolute(fs::path(path), ec).lexically_normal();
  if (ec) {
    abs = fs::path(path).lexically_normal();
  }
  fs::path root = fs::absolute(fs::path(root_dir.empty() ? "." : root_dir), ec)
                      .lexically_normal();
  if (ec) {
    root = fs::path(root_dir.empty() ? "." : root_dir).lexically_normal();
  }
  fs::path rel = fs::relative(abs, root, ec);
  if (!ec && !rel.empty() && rel.native().find("..") != 0) {
    return rel.string();
  }
  return abs.string();
}

FileNode *find_file_node_by_path(std::vector<FileNode> &nodes,
                                 const std::string &path) {
  for (auto &node : nodes) {
    if (node.path == path) {
      return &node;
    }
    if (node.is_dir) {
      if (FileNode *child = find_file_node_by_path(node.children, path)) {
        return child;
      }
    }
  }
  return nullptr;
}
} // namespace

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

void Editor::open_context_menu(int x, int y, ContextMenuSurface surface,
                               const std::vector<ContextMenuItem> &items) {
  context_menu_surface = surface;
  context_menu_items = items;
  context_menu_selected = 0;

  int max_label = 0;
  for (int i = 0; i < (int)context_menu_items.size(); i++) {
    max_label = std::max(max_label, (int)context_menu_items[i].label.size());
    if (context_menu_selected == 0 && !context_menu_items[i].enabled) {
      context_menu_selected = i + 1;
    }
  }
  if (context_menu_selected >= (int)context_menu_items.size()) {
    context_menu_selected = 0;
  }
  for (int i = 0; i < (int)context_menu_items.size(); i++) {
    if (context_menu_items[context_menu_selected].enabled) {
      break;
    }
    context_menu_selected = (context_menu_selected + 1) %
                            std::max(1, (int)context_menu_items.size());
  }

  context_menu_w = std::max(16, max_label + 4);
  context_menu_h = (int)context_menu_items.size() + 2;
  int max_x = std::max(0, ui ? ui->get_render_width() - context_menu_w : x);
  int max_y = std::max(0, ui ? ui->get_height() - context_menu_h : y);
  context_menu_x = std::clamp(x, 0, max_x);
  context_menu_y = std::clamp(y, 0, max_y);
  show_context_menu = !context_menu_items.empty();
  hide_lsp_completion();
  needs_redraw = true;
}

void Editor::close_context_menu() {
  show_context_menu = false;
  context_menu_surface = CONTEXT_MENU_NONE;
  context_menu_items.clear();
  context_menu_selected = 0;
  context_menu_w = 0;
  context_menu_h = 0;
  context_menu_target_buffer = -1;
  context_menu_target_pane = -1;
  context_menu_target_terminal = -1;
  context_menu_target_path.clear();
  context_menu_target_is_dir = false;
  needs_redraw = true;
}

bool Editor::handle_context_menu_input(int ch) {
  if (!show_context_menu) {
    return false;
  }

  auto move_selection = [&](int delta) {
    if (context_menu_items.empty()) {
      return;
    }
    int n = (int)context_menu_items.size();
    for (int step = 0; step < n; step++) {
      context_menu_selected = (context_menu_selected + delta + n) % n;
      if (context_menu_items[context_menu_selected].enabled) {
        break;
      }
    }
    needs_redraw = true;
  };

  if (ch == 27) {
    close_context_menu();
    return true;
  }
  if (ch == 1008 || ch == 'k' || ch == 'K') {
    move_selection(-1);
    return true;
  }
  if (ch == 1009 || ch == 'j' || ch == 'J') {
    move_selection(1);
    return true;
  }
  if (ch == '\n' || ch == 13 || ch == ' ') {
    execute_context_menu_item(context_menu_selected);
    return true;
  }
  return true;
}

bool Editor::handle_context_menu_mouse(int x, int y, bool is_click) {
  if (!show_context_menu) {
    return false;
  }

  bool inside = x >= context_menu_x && x < context_menu_x + context_menu_w &&
                y >= context_menu_y && y < context_menu_y + context_menu_h;
  if (!inside) {
    if (is_click) {
      close_context_menu();
      return true;
    }
    return false;
  }

  int row = y - context_menu_y - 1;
  if (row >= 0 && row < (int)context_menu_items.size()) {
    context_menu_selected = row;
    needs_redraw = true;
    if (is_click) {
      execute_context_menu_item(row);
    }
  }
  return true;
}

void Editor::execute_context_menu_item(int index) {
  if (index < 0 || index >= (int)context_menu_items.size() ||
      !context_menu_items[index].enabled) {
    return;
  }

  ContextMenuAction action = context_menu_items[index].action;
  int target_buffer = context_menu_target_buffer;
  int target_terminal = context_menu_target_terminal;
  std::string target_path = context_menu_target_path;
  bool target_is_dir = context_menu_target_is_dir;
  close_context_menu();

  switch (action) {
  case CONTEXT_ACTION_COPY:
    copy();
    set_message("Copied");
    break;
  case CONTEXT_ACTION_CUT:
    cut();
    set_message("Cut");
    break;
  case CONTEXT_ACTION_PASTE:
    paste();
    break;
  case CONTEXT_ACTION_SAVE_BUFFER:
    if (target_buffer >= 0 && target_buffer < (int)buffers.size()) {
      if (!buffers[target_buffer].filepath.empty()) {
        save_buffer_at(target_buffer, true);
      } else if (target_buffer == current_buffer) {
        save_file();
      } else {
        set_message("Save As unavailable for inactive untitled buffer");
      }
    }
    break;
  case CONTEXT_ACTION_CLOSE_BUFFER:
    close_buffer_at(target_buffer);
    break;
  case CONTEXT_ACTION_SIDEBAR_OPEN:
    if (!target_path.empty()) {
      if (target_is_dir) {
        if (FileNode *node = find_file_node_by_path(file_tree, target_path)) {
          node->expanded = !node->expanded;
          if (node->expanded && node->children.empty()) {
            build_tree(node->path, node->children, node->depth + 1);
          }
          invalidate_sidebar_tree_cache();
          needs_redraw = true;
          return;
        }
        open_workspace(target_path, true);
      } else {
        open_file(target_path, false);
        focus_state = FOCUS_EDITOR;
      }
    }
    break;
  case CONTEXT_ACTION_SIDEBAR_NEW_FILE: {
    fs::path base = target_path.empty() ? fs::path(root_dir) : fs::path(target_path);
    if (!target_is_dir) {
      base = base.parent_path();
    }
    std::string rel = path_relative_to_root(base.lexically_normal().string(), root_dir);
    if (rel == ".") {
      rel.clear();
    }
    if (!rel.empty() && rel.back() != '/' && rel.back() != '\\') {
      rel += "/";
    }
    show_command_palette = true;
    command_palette_query = "mkfile " + rel;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
    break;
  }
  case CONTEXT_ACTION_SIDEBAR_NEW_FOLDER: {
    fs::path base = target_path.empty() ? fs::path(root_dir) : fs::path(target_path);
    if (!target_is_dir) {
      base = base.parent_path();
    }
    std::string rel = path_relative_to_root(base.lexically_normal().string(), root_dir);
    if (rel == ".") {
      rel.clear();
    }
    if (!rel.empty() && rel.back() != '/' && rel.back() != '\\') {
      rel += "/";
    }
    show_command_palette = true;
    command_palette_query = "mkdir " + rel;
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    refresh_command_palette();
    needs_redraw = true;
    break;
  }
  case CONTEXT_ACTION_SIDEBAR_RENAME:
    if (!target_path.empty()) {
      show_command_palette = true;
      command_palette_query =
          "rename " + path_relative_to_root(target_path, root_dir) + " ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
    }
    break;
  case CONTEXT_ACTION_SIDEBAR_REFRESH:
    load_file_tree(root_dir);
    set_message("Explorer: refreshed");
    break;
  case CONTEXT_ACTION_SIDEBAR_COPY_PATH:
    if (!target_path.empty()) {
      std::error_code ec;
      fs::path p = fs::absolute(fs::path(target_path), ec);
      clipboard = (ec ? fs::path(target_path) : p).lexically_normal().string();
      set_message("Copied path");
    }
    break;
  case CONTEXT_ACTION_TERMINAL_FOCUS:
    if (target_terminal >= 0 &&
        target_terminal < (int)integrated_terminals.size()) {
      show_integrated_terminal = true;
      activate_integrated_terminal(target_terminal, true);
      if (auto *term = get_integrated_terminal(target_terminal)) {
        term->poll_output();
      }
      set_message("Focused terminal " + std::to_string(target_terminal + 1));
    }
    break;
  case CONTEXT_ACTION_TERMINAL_NEW:
    create_integrated_terminal();
    break;
  case CONTEXT_ACTION_TERMINAL_CLOSE:
    close_integrated_terminal(target_terminal);
    break;
  case CONTEXT_ACTION_TERMINAL_RESET_SCROLL:
    if (auto *term = get_integrated_terminal(target_terminal)) {
      term->reset_scroll();
      needs_redraw = true;
    }
    break;
  case CONTEXT_ACTION_NONE:
    break;
  }
  needs_redraw = true;
}

bool Editor::close_active_floating_ui() {
  if (popup.visible) {
    hide_popup();
    return true;
  }

  if (show_context_menu) {
    close_context_menu();
    return true;
  }

  if (show_command_palette) {
    show_command_palette = false;
    command_palette_query.clear();
    command_palette_results.clear();
    command_palette_selected = 0;
    command_palette_theme_mode = false;
    command_palette_theme_original.clear();
    needs_redraw = true;
    return true;
  }

  if (show_search) {
    show_search = false;
    needs_redraw = true;
    return true;
  }

  if (show_save_prompt) {
    show_save_prompt = false;
    save_prompt_input.clear();
    needs_redraw = true;
    return true;
  }

  if (show_quit_prompt) {
    show_quit_prompt = false;
    needs_redraw = true;
    return true;
  }

  if (telescope.is_active()) {
    telescope.close();
    waiting_for_space_f = false;
    needs_redraw = true;
    return true;
  }

  if (image_viewer.is_active()) {
    image_viewer.close();
    needs_redraw = true;
    return true;
  }

  if (lsp_completion_visible) {
    hide_lsp_completion();
    return true;
  }

  return false;
}
