#include "editor.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
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
  int h = std::max(0, ui->get_height() - status_height - tab_height);
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

  // Header
  std::string root_name = root_dir;
  size_t last_sep = root_name.find_last_of("/");
  if (last_sep != std::string::npos && last_sep + 1 < root_name.size()) {
    root_name = root_name.substr(last_sep + 1);
  }
  if (root_name.empty())
    root_name = "/";

  std::string header = "  " + root_name;
  if ((int)header.size() > w - 2) {
    header = header.substr(0, std::max(0, w - 5)) + "...";
  }
  ui->draw_text(x + 1, y, header, theme.fg_sidebar_directory, theme.bg_sidebar,
                true);

  int tree_y = y + 1;
  int tree_h = std::max(0, h - 2);

  // File Tree
  std::vector<const FileNode *> flat;
  flatten_nodes_render(file_tree, flat);

  auto normalize_path = [](const std::string &path) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(path, ec);
    if (ec) {
      p = std::filesystem::path(path);
    }
    return p.lexically_normal().string();
  };

  auto severity_rank = [](int severity) {
    switch (severity) {
    case 1:
      return 4; // error
    case 2:
      return 3; // warning
    case 3:
      return 2; // info
    case 4:
      return 1; // hint
    default:
      return 0;
    }
  };

  auto merge_severity = [&](int a, int b) {
    return severity_rank(a) >= severity_rank(b) ? a : b;
  };

  auto severity_to_color = [&](int severity, bool is_dir) {
    switch (severity) {
    case 1:
      return theme.fg_diagnostic_error;
    case 2:
      return theme.fg_diagnostic_warning;
    case 3:
      return theme.fg_diagnostic_info;
    case 4:
      return theme.fg_diagnostic_hint;
    default:
      return is_dir ? theme.fg_sidebar_directory : theme.fg_sidebar;
    }
  };

  std::unordered_map<std::string, int> path_severity;
  path_severity.reserve(workspace_diagnostic_severity.size() + flat.size());
  for (const auto &it : workspace_diagnostic_severity) {
    if (it.second > 0) {
      path_severity[it.first] = merge_severity(path_severity[it.first], it.second);
    }
  }

  // Include opened buffers diagnostics too, so explorer color updates even if
  // diagnostics arrived through buffer-local state first.
  for (const auto &buf : buffers) {
    if (buf.filepath.empty()) {
      continue;
    }
    int sev = 0;
    for (const auto &d : buf.diagnostics) {
      sev = merge_severity(sev, d.severity);
    }
    const std::string norm = normalize_path(buf.filepath);
    if (!norm.empty() && sev > 0) {
      path_severity[norm] = merge_severity(path_severity[norm], sev);
    }
  }

  std::unordered_map<std::string, int> node_severity;
  node_severity.reserve(flat.size());
  for (const FileNode *node : flat) {
    const std::string node_path = normalize_path(node->path);
    int sev = path_severity[node_path];
    if (node->is_dir) {
      const std::string prefix = node_path + std::string(1, std::filesystem::path::preferred_separator);
      for (const auto &entry : path_severity) {
        if (entry.first.size() > prefix.size() &&
            entry.first.compare(0, prefix.size(), prefix) == 0) {
          sev = merge_severity(sev, entry.second);
        }
      }
    }
    node_severity[node_path] = sev;
  }

  auto git_status_rank = [](const std::string &xy) {
    if (xy.find('U') != std::string::npos) {
      return 6; // conflict
    }
    if (xy.find('D') != std::string::npos) {
      return 5; // deleted
    }
    if (xy.find('M') != std::string::npos) {
      return 4; // modified
    }
    if (xy.find('A') != std::string::npos) {
      return 3; // added
    }
    if (xy.find('R') != std::string::npos) {
      return 2; // renamed
    }
    if (xy.find('?') != std::string::npos) {
      return 1; // untracked
    }
    return 0;
  };

  auto git_more_important = [&](const std::string &a, const std::string &b) {
    return git_status_rank(a) >= git_status_rank(b) ? a : b;
  };

  auto git_status_symbol = [](const std::string &xy) {
    if (xy.find('U') != std::string::npos) {
      return "!";
    }
    if (xy.find('D') != std::string::npos) {
      return "D";
    }
    if (xy.find('M') != std::string::npos) {
      return "M";
    }
    if (xy.find('A') != std::string::npos) {
      return "A";
    }
    if (xy.find('R') != std::string::npos) {
      return "R";
    }
    if (xy.find('?') != std::string::npos) {
      return "?";
    }
    return "";
  };

  auto git_status_color = [&](const std::string &xy, bool is_dir) {
    if (xy.find('U') != std::string::npos || xy.find('D') != std::string::npos) {
      return theme.fg_diagnostic_error;
    }
    if (xy.find('M') != std::string::npos) {
      return theme.fg_diagnostic_warning;
    }
    if (xy.find('A') != std::string::npos) {
      return theme.fg_diagnostic_hint;
    }
    if (xy.find('R') != std::string::npos || xy.find('?') != std::string::npos) {
      return theme.fg_diagnostic_info;
    }
    return is_dir ? theme.fg_sidebar_directory : theme.fg_sidebar;
  };

  std::unordered_map<std::string, std::string> node_git_status;
  node_git_status.reserve(flat.size());
  for (const FileNode *node : flat) {
    const std::string node_path = normalize_path(node->path);
    std::string status = "";
    auto file_it = git_file_status.find(node_path);
    if (file_it != git_file_status.end()) {
      status = file_it->second;
    }
    if (node->is_dir) {
      const std::string prefix =
          node_path + std::string(1, std::filesystem::path::preferred_separator);
      for (const auto &entry : git_file_status) {
        if (entry.first.size() > prefix.size() &&
            entry.first.compare(0, prefix.size(), prefix) == 0) {
          status = git_more_important(status, entry.second);
        }
      }
    }
    node_git_status[node_path] = status;
  }

  int max_scroll = std::max(0, (int)flat.size() - std::max(1, tree_h));
  if (file_tree_scroll > max_scroll) {
    file_tree_scroll = max_scroll;
  }

  for (int i = 0; i < tree_h; i++) {
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
    const std::string node_path = normalize_path(node->path);
    const std::string git_xy = node_git_status[node_path];
    const std::string git_symbol = git_status_symbol(git_xy);
    const std::string git_badge = git_symbol.empty() ? "  " : (git_symbol + " ");
    int max_len = std::max(0, w - 8 - (int)indent.length() - (int)git_badge.length());
    if ((int)name.length() > max_len)
      name = name.substr(0, max_len) + "..";

    std::string display = indent + git_badge + icon + name;
    const int sev = node_severity[node_path];
    int fg = severity_to_color(sev, node->is_dir);
    if (sev == 0) {
      fg = git_status_color(git_xy, node->is_dir);
    }
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

    ui->draw_text(x + 1, tree_y + i, display, fg, bg);
  }

  // Footer hints
  std::string footer = " a:file A:dir r:rename d:delete R:refresh g:git ";
  if ((int)footer.size() > w - 2) {
    footer = " a A r d R g ";
  }
  ui->draw_text(x + 1, y + h - 1, footer, theme.fg_comment, theme.bg_sidebar);
}
