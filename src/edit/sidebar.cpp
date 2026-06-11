#include "editor.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

static std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

static int utf8_cell_len(const std::string &text, size_t i) {
  if (i >= text.size())
    return 0;
  const unsigned char c = (unsigned char)text[i];
  if ((c & 0x80) == 0)
    return 1;
  if ((c & 0xE0) == 0xC0)
    return 2;
  if ((c & 0xF0) == 0xE0)
    return 3;
  if ((c & 0xF8) == 0xF0)
    return 4;
  return 0;
}

static int cell_count(const std::string &text) {
  int cells = 0;
  size_t i = 0;
  while (i < text.size()) {
    int len = utf8_cell_len(text, i);
    if (len <= 0 || i + (size_t)len > text.size()) {
      i++;
    } else {
      i += (size_t)len;
    }
    cells++;
  }
  return cells;
}

static std::string take_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";

  std::string out;
  int cells = 0;
  size_t i = 0;
  while (i < text.size() && cells < max_cells) {
    int len = utf8_cell_len(text, i);
    if (len <= 0 || i + (size_t)len > text.size()) {
      out += "?";
      i++;
    } else {
      out.append(text, i, (size_t)len);
      i += (size_t)len;
    }
    cells++;
  }
  return out;
}

static std::string truncate_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";
  if (cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return take_cells(text, max_cells);
  return take_cells(text, max_cells - 2) + "..";
}

static std::string normalize_sidebar_path(const std::string &path) {
  if (path.empty())
    return "";
  std::error_code ec;
  std::filesystem::path p = std::filesystem::absolute(path, ec);
  if (ec) {
    p = std::filesystem::path(path);
  }
  return p.lexically_normal().string();
}

static bool sidebar_path_is_child_of(const std::string &child,
                                     const std::string &parent) {
  if (child.size() <= parent.size())
    return false;
  if (child.compare(0, parent.size(), parent) != 0)
    return false;
  char sep = child[parent.size()];
  return sep == '/' || sep == '\\';
}

static std::string sidebar_parent_path(const std::string &path) {
  if (path.empty())
    return "";
  std::filesystem::path p(path);
  std::filesystem::path parent = p.parent_path();
  if (parent == p)
    return "";
  return parent.lexically_normal().string();
}

static std::string root_display_name(const std::string &root) {
  std::filesystem::path p(root);
  std::string name = p.filename().string();
  if (!name.empty())
    return name;
  name = p.root_path().string();
  return name.empty() ? root : name;
}

static std::string workspace_relative_display(const std::string &abs_path,
                                              const std::string &root_dir) {
  const std::string norm_path = normalize_sidebar_path(abs_path);
  const std::string norm_root = normalize_sidebar_path(root_dir);
  std::filesystem::path rel =
      std::filesystem::path(norm_path).lexically_relative(norm_root);
  std::string rel_s = rel.string();
  bool escapes_root = rel_s == ".." || rel_s.rfind("../", 0) == 0 ||
                      rel_s.rfind("..\\", 0) == 0;
  if (!rel_s.empty() && rel_s != "." && !escapes_root) {
    return rel_s;
  }
  if (rel_s == ".") {
    return root_display_name(norm_root);
  }
  return norm_path.empty() ? abs_path : norm_path;
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

static int sidebar_severity_rank(int severity) {
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
}

static int merge_sidebar_severity(int a, int b) {
  return sidebar_severity_rank(a) >= sidebar_severity_rank(b) ? a : b;
}

static int git_status_rank(const std::string &xy) {
  if (xy.find('U') != std::string::npos)
    return 6; // conflict
  if (xy.find('D') != std::string::npos)
    return 5; // deleted
  if (xy.find('M') != std::string::npos)
    return 4; // modified
  if (xy.find('A') != std::string::npos)
    return 3; // added
  if (xy.find('R') != std::string::npos)
    return 2; // renamed
  if (xy.find('?') != std::string::npos)
    return 1; // untracked
  return 0;
}

static std::string merge_git_status(const std::string &a,
                                    const std::string &b) {
  return git_status_rank(a) >= git_status_rank(b) ? a : b;
}

static std::string git_status_symbol(const std::string &xy) {
  if (xy.find('U') != std::string::npos)
    return "!";
  if (xy.find('D') != std::string::npos)
    return "D";
  if (xy.find('M') != std::string::npos)
    return "M";
  if (xy.find('A') != std::string::npos)
    return "A";
  if (xy.find('R') != std::string::npos)
    return "R";
  if (xy.find('?') != std::string::npos)
    return "?";
  return "";
}

static const char *diagnostic_symbol(int severity) {
  switch (severity) {
  case 1:
    return "E";
  case 2:
    return "W";
  case 3:
    return "I";
  case 4:
    return "H";
  default:
    return "";
  }
}

void Editor::invalidate_sidebar_tree_cache() {
  sidebar_render_cache_.tree_dirty = true;
  sidebar_render_cache_.diagnostics_dirty = true;
  sidebar_render_cache_.git_dirty = true;
}

void Editor::invalidate_sidebar_diagnostics_cache() {
  sidebar_render_cache_.diagnostics_dirty = true;
}

void Editor::invalidate_sidebar_git_cache() {
  sidebar_render_cache_.git_dirty = true;
}

void Editor::ensure_sidebar_render_cache() {
  if (sidebar_render_cache_.tree_dirty) {
    rebuild_sidebar_tree_cache();
  }
  if (sidebar_render_cache_.diagnostics_dirty) {
    rebuild_sidebar_diagnostics_cache();
  }
  if (sidebar_render_cache_.git_dirty) {
    rebuild_sidebar_git_cache();
  }
}

void Editor::rebuild_sidebar_tree_cache() {
  sidebar_render_cache_.rows.clear();
  sidebar_render_cache_.path_to_row.clear();
  sidebar_render_cache_.normalized_root = normalize_sidebar_path(root_dir);
  sidebar_render_cache_.root_label = root_display_name(root_dir);

  sidebar_render_cache_.rows.reserve(file_tree.size());
  std::function<void(const FileNode &)> append_row = [&](const FileNode &node) {
    SidebarRenderRow row;
    row.path = node.path;
    row.normalized_path = normalize_sidebar_path(node.path);
    row.name = node.name;
    row.is_dir = node.is_dir;
    row.expanded = node.expanded;
    row.depth = node.depth;

    std::string indent(node.depth * 2, ' ');
    std::string chevron = node.is_dir ? (node.expanded ? " " : " ") : "  ";
    row.label = indent + chevron + get_file_icon(node) + node.name;
    row.footer_label = workspace_relative_display(node.path, root_dir);

    if (!row.normalized_path.empty()) {
      sidebar_render_cache_.path_to_row[row.normalized_path] =
          (int)sidebar_render_cache_.rows.size();
    }
    sidebar_render_cache_.rows.push_back(std::move(row));

    if (node.is_dir && node.expanded) {
      for (const auto &child : node.children) {
        append_row(child);
      }
    }
  };

  for (const auto &node : file_tree) {
    append_row(node);
  }

  sidebar_render_cache_.tree_dirty = false;
  sidebar_render_cache_.diagnostics_dirty = true;
  sidebar_render_cache_.git_dirty = true;
}

void Editor::rebuild_sidebar_diagnostics_cache() {
  if (sidebar_render_cache_.tree_dirty) {
    rebuild_sidebar_tree_cache();
  }

  for (auto &row : sidebar_render_cache_.rows) {
    row.diagnostic_severity = 0;
  }

  auto propagate = [&](const std::string &path, int severity) {
    if (severity <= 0)
      return;
    std::string current = normalize_sidebar_path(path);
    if (current.empty())
      return;
    const std::string root = sidebar_render_cache_.normalized_root;
    while (!current.empty()) {
      auto row_it = sidebar_render_cache_.path_to_row.find(current);
      if (row_it != sidebar_render_cache_.path_to_row.end()) {
        auto &row = sidebar_render_cache_.rows[(size_t)row_it->second];
        row.diagnostic_severity =
            merge_sidebar_severity(row.diagnostic_severity, severity);
      }
      if (!root.empty() && current == root)
        break;
      if (!root.empty() && !sidebar_path_is_child_of(current, root))
        break;
      std::string parent = sidebar_parent_path(current);
      if (parent.empty() || parent == current)
        break;
      current = parent;
    }
  };

  for (const auto &it : workspace_diagnostic_severity) {
    propagate(it.first, it.second);
  }

  for (const auto &buf : buffers) {
    if (buf.filepath.empty())
      continue;
    int severity = 0;
    for (const auto &d : buf.diagnostics) {
      severity = merge_sidebar_severity(severity, d.severity);
    }
    propagate(buf.filepath, severity);
  }

  sidebar_render_cache_.diagnostics_dirty = false;
}

void Editor::rebuild_sidebar_git_cache() {
  if (sidebar_render_cache_.tree_dirty) {
    rebuild_sidebar_tree_cache();
  }

  for (auto &row : sidebar_render_cache_.rows) {
    row.git_status.clear();
  }

  auto propagate = [&](const std::string &path, const std::string &status) {
    if (status.empty())
      return;
    std::string current = normalize_sidebar_path(path);
    if (current.empty())
      return;
    const std::string root = sidebar_render_cache_.normalized_root;
    while (!current.empty()) {
      auto row_it = sidebar_render_cache_.path_to_row.find(current);
      if (row_it != sidebar_render_cache_.path_to_row.end()) {
        auto &row = sidebar_render_cache_.rows[(size_t)row_it->second];
        row.git_status = merge_git_status(row.git_status, status);
      }
      if (!root.empty() && current == root)
        break;
      if (!root.empty() && !sidebar_path_is_child_of(current, root))
        break;
      std::string parent = sidebar_parent_path(current);
      if (parent.empty() || parent == current)
        break;
      current = parent;
    }
  };

  for (const auto &entry : git_file_status) {
    propagate(entry.first, entry.second);
  }

  sidebar_render_cache_.git_dirty = false;
}

void Editor::render_sidebar() {
  if (!show_sidebar)
    return;

  ensure_sidebar_render_cache();

  int render_w = ui->get_render_width();
  int w = std::min(sidebar_width, std::max(0, render_w - 20));
  int reserved_terminal_h = 0;
  if (show_integrated_terminal && !integrated_terminals.empty()) {
    reserved_terminal_h =
        std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
  }
  int h = std::max(0, ui->get_height() - status_height - tab_height -
                          reserved_terminal_h);
  int x = 0;
  int y = tab_height;
  if (w < 2 || h < 1)
    return;

  for (int i = y; i < y + h; i++) {
    ui->draw_text(0, i, std::string(w, ' '), theme.fg_sidebar, theme.bg_sidebar);
  }

  std::string header_label = "  " + sidebar_render_cache_.root_label + " ";
  int header_w = std::min(std::max(6, cell_count(header_label) + 2),
                          std::max(1, w - 1));
  ui->draw_text(x, y, "╭", theme.fg_sidebar_border, theme.bg_sidebar, true);
  if (header_w > 2) {
    ui->draw_text(x + 1, y, truncate_cells(header_label, header_w - 2),
                  theme.fg_sidebar_directory, theme.bg_sidebar, true);
  }
  if (header_w > 1) {
    ui->draw_text(x + header_w - 1, y, "╮", theme.fg_sidebar_border,
                  theme.bg_sidebar, true);
  }

  int tree_y = y + 1;
  int tree_h = std::max(0, h - 2);
  const auto &rows = sidebar_render_cache_.rows;

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

  int max_scroll = std::max(0, (int)rows.size() - std::max(1, tree_h));
  if (file_tree_scroll < 0) {
    file_tree_scroll = 0;
  } else if (file_tree_scroll > max_scroll) {
    file_tree_scroll = max_scroll;
  }

  std::string active_file_path;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
    active_file_path = normalize_sidebar_path(buffers[current_buffer].filepath);
  }

  for (int i = 0; i < tree_h; i++) {
    int idx = i + file_tree_scroll;
    if (idx >= (int)rows.size())
      break;

    const auto &row = rows[(size_t)idx];
    const std::string git_xy = row.git_status;
    const std::string git_symbol = git_status_symbol(git_xy);
    const int sev = row.diagnostic_severity;
    const std::string diag_symbol = diagnostic_symbol(sev);
    const bool selected = idx == file_tree_selected;
    const bool is_active_file =
        !row.is_dir && !active_file_path.empty() &&
        row.normalized_path == active_file_path;

    int row_fg = severity_to_color(sev, row.is_dir);
    if (sev == 0) {
      row_fg = git_status_color(git_xy, row.is_dir);
    }
    int row_bg = theme.bg_sidebar;

    if (selected) {
      if (focus_state == FOCUS_SIDEBAR) {
        row_fg = theme.fg_sidebar_selected;
        row_bg = theme.bg_sidebar_selected;
      } else {
        row_fg = theme.fg_sidebar_selected_inactive;
        row_bg = theme.bg_sidebar_selected_inactive;
      }
      ui->draw_text(0, tree_y + i, std::string(std::max(0, w - 1), ' '),
                    row_fg, row_bg);
    }

    if (is_active_file) {
      ui->draw_text(0, tree_y + i, "▌", theme.fg_sidebar_directory, row_bg,
                    true);
    }

    const bool show_badges = w >= 16;
    const int border_x = w - 1;
    const int diag_x = border_x - 5;
    const int git_x = border_x - 3;
    const int label_max =
        show_badges ? std::max(0, diag_x - (x + 1) - 1)
                    : std::max(0, border_x - (x + 1));

    ui->draw_text(x + 1, tree_y + i, truncate_cells(row.label, label_max),
                  row_fg, row_bg);

    if (show_badges) {
      if (!diag_symbol.empty()) {
        ui->draw_text(diag_x, tree_y + i, diag_symbol,
                      severity_to_color(sev, false), row_bg, true);
      }
      if (!git_symbol.empty()) {
        ui->draw_text(git_x, tree_y + i, git_symbol,
                      git_status_color(git_xy, false), row_bg, true);
      }
    }
  }

  std::string footer;
  if (!rows.empty() && file_tree_selected >= 0 &&
      file_tree_selected < (int)rows.size()) {
    footer = " " + rows[(size_t)file_tree_selected].footer_label;
  } else if (has_git_repo()) {
    footer = " " + git_branch + " " + std::to_string(git_dirty_count);
  } else {
    footer = std::to_string(rows.size()) + " items";
  }
  ui->draw_text(x + 1, y + h - 1, truncate_cells(footer, w - 2),
                theme.fg_comment, theme.bg_sidebar);

  for (int i = y; i < y + h; i++) {
    ui->draw_text(w - 1, i, "│", theme.fg_sidebar_border, theme.bg_sidebar);
  }
}
