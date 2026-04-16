#include "editor.h"
#include <vector>

// Helper to flatten the file tree for rendering
static void flatten_nodes_render(const std::vector<FileNode> &nodes,
                                 std::vector<const FileNode *> &flat_list) {
  for (const auto &node : nodes) {
    flat_list.push_back(&node);
    if (node.is_dir && node.expanded) {
      flatten_nodes_render(node.children, flat_list);
    }
  }
}

void Editor::render_sidebar() {
  if (!show_sidebar)
    return;

  int w = std::min(sidebar_width, std::max(0, ui->get_width() - 20));
  int h = std::max(0, ui->get_height() - status_height -
                          tab_height); // Full height minus components
  int x = 0;
  int y = tab_height;
  if (w < 2 || h < 1)
    return;

  // Background
  UIRect rect = {x, y, w, h};
  // ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  // Loop filling to be safe similar to original
  for (int i = y; i < y + h; i++) {
    ui->draw_text(0, i, std::string(w, ' '), theme.fg_command,
                  theme.bg_command);
  }

  // Border line
  for (int i = y; i < y + h; i++) {
    ui->draw_text(w - 1, i, "|", theme.fg_panel_border, theme.bg_command);
  }

  // File Tree
  std::vector<const FileNode *> flat;
  flatten_nodes_render(file_tree, flat);

  for (int i = 0; i < h; i++) {
    int idx = i + file_tree_scroll;
    if (idx >= (int)flat.size())
      break;

    const FileNode *node = flat[idx];
    std::string icon = node->is_dir ? (node->expanded ? "v " : "> ") : "  ";
    std::string indent(node->depth * 2, ' ');

    std::string name = node->name;
    // Truncate name
    int max_len = std::max(0, w - 4 - (int)indent.length());
    if ((int)name.length() > max_len)
      name = name.substr(0, max_len) + "..";

    std::string display = indent + icon + name;

    int fg = theme.fg_command;
    int bg = theme.bg_command;

    if (idx == file_tree_selected) {
      fg = 0;                                      // Black text
      bg = (focus_state == FOCUS_SIDEBAR) ? 4 : 8; // Blue or Grey

      if (focus_state != FOCUS_SIDEBAR)
        bg = 5; // Distinct inactive selection
    }

    ui->draw_text(x + 1, y + i, display, fg, bg);
  }
}
