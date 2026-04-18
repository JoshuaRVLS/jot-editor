#include "editor.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>
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

static std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

static std::string get_file_icon(const FileNode &node) {
  if (node.is_dir) {
    return node.expanded ? " " : " ";
  }

  static const std::unordered_map<std::string, std::string> ext_icons = {
      {".cpp", " "}, {".cc", " "},   {".cxx", " "}, {".c", " "},
      {".h", " "},   {".hpp", " "},  {".py", " "},  {".js", " "},
      {".ts", " "},  {".jsx", " "},  {".tsx", " "}, {".json", " "},
      {".md", " "},  {".toml", " "}, {".yaml", " "}, {".yml", " "},
      {".html", " "}, {".css", " "},  {".scss", " "}, {".sh", " "},
      {".go", " "},  {".rs", " "},   {".java", " "}, {".php", " "},
      {".rb", " "},  {".xml", "󰗀 "},  {".txt", "󰈙 "}, {".lock", "󰌾 "}};

  std::string name = lower_copy(node.name);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) {
    std::string ext = name.substr(dot);
    auto it = ext_icons.find(ext);
    if (it != ext_icons.end())
      return it->second;
  }
  return "󰈔 ";
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
  // ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  // Loop filling to be safe similar to original
  for (int i = y; i < y + h; i++) {
    ui->draw_text(0, i, std::string(w, ' '), theme.fg_sidebar, theme.bg_sidebar);
  }

  // Border line
  for (int i = y; i < y + h; i++) {
    ui->draw_text(w - 1, i, "|", theme.fg_sidebar_border, theme.bg_sidebar);
  }

  // File Tree
  std::vector<const FileNode *> flat;
  flatten_nodes_render(file_tree, flat);

  for (int i = 0; i < h; i++) {
    int idx = i + file_tree_scroll;
    if (idx >= (int)flat.size())
      break;

    const FileNode *node = flat[idx];
    std::string icon;
    if (node->is_dir) {
      icon = std::string(node->expanded ? "▾ " : "▸ ") + get_file_icon(*node);
    } else {
      icon = get_file_icon(*node);
    }
    std::string indent(node->depth * 2, ' ');

    std::string name = node->name;
    // Truncate name
    int max_len = std::max(0, w - 6 - (int)indent.length());
    if ((int)name.length() > max_len)
      name = name.substr(0, max_len) + "..";

    std::string display = indent + icon + name;

    int fg = node->is_dir ? theme.fg_sidebar_directory : theme.fg_sidebar;
    int bg = theme.bg_sidebar;

    if (idx == file_tree_selected) {
      if (focus_state == FOCUS_SIDEBAR) {
        fg = theme.fg_sidebar_selected;
        bg = theme.bg_sidebar_selected;
      } else {
        fg = theme.fg_sidebar_selected_inactive;
        bg = theme.bg_sidebar_selected_inactive;
      }
    }

    ui->draw_text(x + 1, y + i, display, fg, bg);
  }
}
