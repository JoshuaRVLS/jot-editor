#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "sidebar_guides.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>


static int utf8_cell_len(const std::string &text, size_t i)
{
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

static int cell_count(const std::string &text)
{
  int cells = 0;
  size_t i = 0;
  while (i < text.size())
  {
    int len = utf8_cell_len(text, i);
    if (len <= 0 || i + (size_t)len > text.size())
    {
      i++;
    }
    else
    {
      i += (size_t)len;
    }
    cells++;
  }
  return cells;
}

static std::string take_cells(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";

  std::string out;
  int cells = 0;
  size_t i = 0;
  while (i < text.size() && cells < max_cells)
  {
    int len = utf8_cell_len(text, i);
    if (len <= 0 || i + (size_t)len > text.size())
    {
      out += "?";
      i++;
    }
    else
    {
      out.append(text, i, (size_t)len);
      i += (size_t)len;
    }
    cells++;
  }
  return out;
}

static std::string truncate_cells(const std::string &text, int max_cells)
{
  if (max_cells <= 0)
    return "";
  if (cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return take_cells(text, max_cells);
  return take_cells(text, max_cells - 2) + "..";
}

static std::string normalize_sidebar_path(const std::string &path)
{
  if (path.empty())
    return "";
  std::error_code ec;
  std::filesystem::path p = std::filesystem::absolute(path, ec);
  if (ec)
  {
    p = std::filesystem::path(path);
  }
  return p.lexically_normal().string();
}

static bool sidebar_path_is_child_of(const std::string &child, const std::string &parent)
{
  if (child.size() <= parent.size())
    return false;
  if (child.compare(0, parent.size(), parent) != 0)
    return false;
  char sep = child[parent.size()];
  return sep == '/' || sep == '\\';
}

static std::string sidebar_parent_path(const std::string &path)
{
  if (path.empty())
    return "";
  std::filesystem::path p(path);
  std::filesystem::path parent = p.parent_path();
  if (parent == p)
    return "";
  return parent.lexically_normal().string();
}

static std::string root_display_name(const std::string &root)
{
  std::filesystem::path p(root);
  std::string name = p.filename().string();
  if (!name.empty())
    return name;
  name = p.root_path().string();
  return name.empty() ? root : name;
}

static std::string workspace_relative_display(const std::string &abs_path,
                                              const std::string &root_dir)
{
  const std::string norm_path = normalize_sidebar_path(abs_path);
  const std::string norm_root = normalize_sidebar_path(root_dir);
  std::filesystem::path rel = std::filesystem::path(norm_path).lexically_relative(norm_root);
  std::string rel_s = rel.string();
  bool escapes_root = rel_s == ".." || rel_s.rfind("../", 0) == 0 || rel_s.rfind("..\\", 0) == 0;
  if (!rel_s.empty() && rel_s != "." && !escapes_root)
  {
    return rel_s;
  }
  if (rel_s == ".")
  {
    return root_display_name(norm_root);
  }
  return norm_path.empty() ? abs_path : norm_path;
}

static std::string get_file_icon(const FileNode &node)
{
  if (node.is_dir)
  {
    return node.expanded ? " " : " ";
  }
  // Shared per-extension glyph map (core/file_icons.h) so the explorer, the
  // home screen and the status line all agree on what a file looks like.
  return std::string(jot_icons::file_type_icon(node.name).glyph) + " ";
}


static int sidebar_severity_rank(int severity)
{
  switch (severity)
  {
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

static int merge_sidebar_severity(int a, int b)
{
  return sidebar_severity_rank(a) >= sidebar_severity_rank(b) ? a : b;
}

static bool git_conflict_status(const std::string &xy)
{
  return xy == "DD" || xy == "AU" || xy == "UD" || xy == "UA" || xy == "DU" || xy == "AA"
         || xy == "UU" || xy.find('U') != std::string::npos;
}

static int git_status_rank(const std::string &xy)
{
  if (git_conflict_status(xy))
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

static std::string merge_git_status(const std::string &a, const std::string &b)
{
  return git_status_rank(a) >= git_status_rank(b) ? a : b;
}

static std::string git_status_symbol(const std::string &xy)
{
  if (git_conflict_status(xy))
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

static std::pair<int, int> git_status_colors(const Theme &theme, const std::string &xy)
{
  if (git_conflict_status(xy))
    return {theme.fg_git_conflict, theme.bg_git_conflict};
  if (xy.find('D') != std::string::npos)
    return {theme.fg_git_deleted, theme.bg_git_deleted};
  if (xy.find('R') != std::string::npos)
    return {theme.fg_git_renamed, theme.bg_git_renamed};
  if (xy.find('A') != std::string::npos)
    return {theme.fg_git_added, theme.bg_git_added};
  if (xy.find('?') != std::string::npos)
    return {theme.fg_git_untracked, theme.bg_git_untracked};
  if (xy.find('M') != std::string::npos)
    return {theme.fg_git_modified, theme.bg_git_modified};
  return {theme.fg_sidebar, theme.bg_sidebar};
}

void Editor::invalidate_sidebar_tree_cache()
{
  sidebar_render_cache_.tree_dirty = true;
  sidebar_render_cache_.diagnostics_dirty = true;
  sidebar_render_cache_.git_dirty = true;
}

void Editor::invalidate_sidebar_diagnostics_cache()
{
  sidebar_render_cache_.diagnostics_dirty = true;
}

void Editor::invalidate_sidebar_git_cache()
{
  sidebar_render_cache_.git_dirty = true;
}

void Editor::ensure_sidebar_render_cache()
{
  if (sidebar_render_cache_.tree_dirty)
  {
    rebuild_sidebar_tree_cache();
  }
  if (sidebar_render_cache_.diagnostics_dirty)
  {
    rebuild_sidebar_diagnostics_cache();
  }
  if (sidebar_render_cache_.git_dirty)
  {
    rebuild_sidebar_git_cache();
  }
}

void Editor::rebuild_sidebar_tree_cache()
{
  sidebar_render_cache_.rows.clear();
  sidebar_render_cache_.path_to_row.clear();
  sidebar_render_cache_.normalized_root = normalize_sidebar_path(root_dir);
  sidebar_render_cache_.root_label = root_display_name(root_dir);

  sidebar_render_cache_.rows.reserve(file_tree.size());
  // Tree indent guides (neo-tree style): every level owns a two-cell marker
  // column — directories show their expander chevron, files a bare "│ " bar
  // or "└ " foot. Deeper rows reserve the level-0 column, then one slot per
  // ancestor. The connector under an expanded folder comes from the
  // children's own markers, so it never disappears. Row widths match the old
  // indent + chevron layout: 2 cells per level.
  std::function<void(const FileNode &, const std::string &, bool)> append_row =
      [&](const FileNode &node, const std::string &ancestor_guide, bool is_last)
  {
    SidebarRenderRow row;
    row.path = node.path;
    row.normalized_path = normalize_sidebar_path(node.path);
    row.name = node.name;
    row.is_dir = node.is_dir;
    row.expanded = node.expanded;
    row.depth = node.depth;

    const std::string guide =
        sidebar_guides::row_guide(node.is_dir, node.expanded, is_last, node.depth, ancestor_guide);
    row.guide = guide;
    row.guide_cells = cell_count(guide);
    row.label = guide + get_file_icon(node) + node.name;
    if (!node.is_dir)
    {
      // Per-language icon glyph + brand color for file rows (the shared map
      // already drives the status line and home screen). Directories keep
      // their folder glyph in the theme's directory color.
      const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(node.name);
      row.icon = fti.glyph;
      row.icon_fg = fti.color;
    }
    row.footer_label = workspace_relative_display(node.path, root_dir);

    if (!row.normalized_path.empty())
    {
      sidebar_render_cache_.path_to_row[row.normalized_path] =
          (int)sidebar_render_cache_.rows.size();
    }
    sidebar_render_cache_.rows.push_back(std::move(row));

    if (node.is_dir && node.expanded)
    {
      // Children continue the parent's own marker column only below the top
      // level (the level-0 column never continues, matching neo-tree).
      const std::string child_guide = node.depth >= 1
                                          ? sidebar_guides::children_guide(is_last, ancestor_guide)
                                          : ancestor_guide;
      const std::vector<FileNode> &children = node.children;
      for (size_t i = 0; i < children.size(); i++)
      {
        append_row(children[i], child_guide, i + 1 == children.size());
      }
    }
  };

  for (size_t i = 0; i < file_tree.size(); i++)
  {
    append_row(file_tree[i], "", i + 1 == file_tree.size());
  }

  sidebar_render_cache_.tree_dirty = false;
  sidebar_render_cache_.diagnostics_dirty = true;
  sidebar_render_cache_.git_dirty = true;
}

void Editor::rebuild_sidebar_diagnostics_cache()
{
  if (sidebar_render_cache_.tree_dirty)
  {
    rebuild_sidebar_tree_cache();
  }

  for (auto &row : sidebar_render_cache_.rows)
  {
    row.diagnostic_severity = 0;
    row.diagnostic_errors = 0;
    row.diagnostic_warnings = 0;
  }

  auto propagate = [&](const std::string &path, int severity)
  {
    if (severity <= 0)
      return;
    std::string current = normalize_sidebar_path(path);
    if (current.empty())
      return;
    const std::string root = sidebar_render_cache_.normalized_root;
    while (!current.empty())
    {
      auto row_it = sidebar_render_cache_.path_to_row.find(current);
      if (row_it != sidebar_render_cache_.path_to_row.end())
      {
        auto &row = sidebar_render_cache_.rows[(size_t)row_it->second];
        row.diagnostic_severity = merge_sidebar_severity(row.diagnostic_severity, severity);
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

  for (const auto &it : workspace_diagnostic_severity)
  {
    propagate(it.first, it.second);
  }

  for (const auto &buf : buffers)
  {
    if (buf.filepath.empty())
      continue;
    int severity = 0;
    for (const auto &d : buf.diagnostics)
    {
      severity = merge_sidebar_severity(severity, d.severity);
    }
    propagate(buf.filepath, severity);
  }

  // Numeric file badges count errors and warnings per file. The per-server
  // slices are the source refresh_lsp_diagnostics_for merges from, so open and
  // unopened files are counted alike.
  for (const auto &by_client : lsp_diag_slices_)
  {
    for (const auto &file_entry : by_client.second)
    {
      const std::string normalized = normalize_sidebar_path(file_entry.first);
      if (normalized.empty())
        continue;
      const auto row_it = sidebar_render_cache_.path_to_row.find(normalized);
      if (row_it == sidebar_render_cache_.path_to_row.end())
        continue;
      auto &row = sidebar_render_cache_.rows[(size_t)row_it->second];
      if (row.is_dir)
        continue;
      for (const auto &d : file_entry.second)
      {
        if (d.severity == 1)
          row.diagnostic_errors++;
        else if (d.severity == 2)
          row.diagnostic_warnings++;
      }
    }
  }

  sidebar_render_cache_.diagnostics_dirty = false;
}

void Editor::rebuild_sidebar_git_cache()
{
  if (sidebar_render_cache_.tree_dirty)
  {
    rebuild_sidebar_tree_cache();
  }

  for (auto &row : sidebar_render_cache_.rows)
  {
    row.git_status.clear();
  }

  auto propagate = [&](const std::string &path, const std::string &status)
  {
    if (status.empty())
      return;
    std::string current = normalize_sidebar_path(path);
    if (current.empty())
      return;
    const std::string root = sidebar_render_cache_.normalized_root;
    while (!current.empty())
    {
      auto row_it = sidebar_render_cache_.path_to_row.find(current);
      if (row_it != sidebar_render_cache_.path_to_row.end())
      {
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

  for (const auto &entry : git_file_status)
  {
    propagate(entry.first, entry.second);
  }

  sidebar_render_cache_.git_dirty = false;
}

std::vector<Editor::GitSidebarRow> Editor::build_git_sidebar_rows() const
{
  std::vector<GitSidebarRow> rows;
  rows.reserve(git_file_status.size());

  std::string base_root = git_root.empty() ? root_dir : git_root;
  for (const auto &entry : git_file_status)
  {
    GitSidebarRow row;
    row.path = entry.first;
    row.status = entry.second;
    row.relative_path = workspace_relative_display(entry.first, base_root);
    rows.push_back(std::move(row));
  }

  std::sort(rows.begin(),
            rows.end(),
            [](const GitSidebarRow &a, const GitSidebarRow &b)
            {
              int ar = git_status_rank(a.status);
              int br = git_status_rank(b.status);
              if (ar != br)
              {
                return ar > br;
              }
              return a.relative_path < b.relative_path;
            });

  return rows;
}

void Editor::render_sidebar()
{
  if (!show_sidebar)
    return;

  ensure_sidebar_render_cache();

  int w = effective_sidebar_width();
  // Full height, aligned with the editor pane: the buffer-tab strip is
  // pane-local (rendered inside the pane frame), so no row belongs above
  // the explorer's top border. The column also reserves the terminal's real
  // footprint (not the old 50% cap): the panel can grow to nearly the full
  // window, and the explorer must shrink accordingly instead of sliding
  // underneath it.
  const ContentColumn col = content_column();
  int h = col.h;
  int y = col.top;
  if (w < 2 || h < 1)
    return;

  // Hand the full panel model to a Lua UI handler when one is registered; it
  // owns the frame, rail, header, rows and footer. The native paint below
  // remains as the exact fallback when Lua is disabled.
  const int border_fg = sidebar_resize_dragging ? theme.fg_active_border : theme.fg_sidebar_border;
  const int rail_w =
      explorer_only() ? 0 : std::min(sidebar_activity_rail_width(), std::max(1, w - 1));
  SidebarPanelView view;
  view.x = 0;
  view.y = y;
  view.w = w;
  view.h = h;
  view.border_fg = border_fg;
  view.bg = theme.bg_sidebar;
  view.resizing = sidebar_resize_dragging;
  view.git_view = !explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT;
  view.git_panel_active = show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT;
  view.rail_w = rail_w;
  view.rail_explorer_row = explorer_only() ? -1 : 1;
  view.rail_git_row = explorer_only() ? -1 : 3;
  // Content geometry: the Lua handler truncates the baked-in header/footer
  // against content_w, so it must be populated before emit (defaults to 0
  // otherwise, silently clipping the header to nothing).
  view.content_x = std::max(1, rail_w);
  view.content_w = std::max(0, w - view.content_x - 1);

  auto emit_sidebar_view = [&]() -> bool
  {
    if (lua_api && lua_api->has_lua_ui_handler("sidebar"))
    {
      return lua_api->emit_sidebar(view);
    }
    return false;
  };

  for (int i = y; i < y + h; i++)
  {
    ui->draw_text(0, i, std::string(w, ' '), theme.fg_sidebar, theme.bg_sidebar);
  }

  // Full panel frame: top/left/right/bottom borders with connected corners,
  // so the explorer is a closed box (not just a right edge).
  if (h >= 2)
  {
    ui->draw_text(0, y, "╭", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    for (int bx = 1; bx < w - 1; bx++)
    {
      ui->draw_text(bx, y, "─", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    }
    ui->draw_text(w - 1, y, "╮", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    for (int i = y + 1; i < y + h - 1; i++)
    {
      ui->draw_text(0, i, "│", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
      ui->draw_text(w - 1, i, "│", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    }
    ui->draw_text(0, y + h - 1, "╰", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    for (int bx = 1; bx < w - 1; bx++)
    {
      ui->draw_text(bx, y + h - 1, "─", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
    }
    ui->draw_text(w - 1, y + h - 1, "╯", border_fg, theme.bg_sidebar, sidebar_resize_dragging);
  }
  else
  {
    ui->draw_text(0, y, "─", border_fg, theme.bg_sidebar);
    for (int bx = 1; bx < w - 1; bx++)
    {
      ui->draw_text(bx, y, "─", border_fg, theme.bg_sidebar);
    }
    ui->draw_text(w - 1, y, "─", border_fg, theme.bg_sidebar);
  }

  auto draw_rail_item = [&](int row, const std::string &label, bool active)
  {
    if (row < 0 || row >= h)
      return;
    int fg = active ? theme.fg_sidebar_directory : theme.fg_comment;
    // The panel's left border occupies column 0, so the activity rail lives
    // one cell inside the frame.
    ui->draw_text(1, y + row, std::string(std::max(0, rail_w - 2), ' '), fg, theme.bg_sidebar);
    ui->draw_text(1, y + row, active ? "▌" : " ", fg, theme.bg_sidebar, active);
    if (rail_w >= 3)
    {
      ui->draw_text(2, y + row, label, fg, theme.bg_sidebar, active);
    }
  };

  if (!explorer_only())
  {
    draw_rail_item(1, "󰉋 ", active_sidebar_view == SIDEBAR_VIEW_EXPLORER);
    // The git item launches the git panel (":gitpanel"), so its marker
    // follows that panel rather than which sidebar view is showing.
    draw_rail_item(3, " ", view.git_panel_active);
    for (int i = y + 1; i < y + h - 1; i++)
    {
      ui->draw_text(std::max(0, rail_w - 1), i, "│", theme.fg_sidebar_border, theme.bg_sidebar);
    }
  }

  // Content lives inside the frame: at least column 1 (the left border)
  // plus the activity rail when present. Geometry mirrors view.content_x /
  // view.content_w set above so the Lua handler and native paint agree.
  const int content_x = view.content_x;
  const int content_w = view.content_w;
  if (content_w < 2)
  {
    if (emit_sidebar_view())
    {
      return;
    }
    return;
  }

  auto severity_to_color = [&](int severity, bool is_dir)
  {
    switch (severity)
    {
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

  if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
  {
    std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
    int header_y = y;
    int list_y = y + 1;
    // One extra row is reserved at the bottom for the panel border, so the
    // footer moves up to y + h - 2.
    int list_h = std::max(0, h - 3);

    std::string header_label = has_git_repo() ? " " + git_branch : " Git";
    if (has_git_repo())
    {
      if (git_staged_count > 0)
      {
        header_label += " +" + std::to_string(git_staged_count);
      }
      if (git_unstaged_count > 0)
      {
        header_label += " ~" + std::to_string(git_unstaged_count);
      }
      if (git_untracked_count > 0)
      {
        header_label += " ?" + std::to_string(git_untracked_count);
      }
      if (git_conflict_count > 0)
      {
        header_label += " !" + std::to_string(git_conflict_count);
      }
    }
    view.header = truncate_cells(header_label, std::max(0, content_w - 3));
    view.header_x = content_x + 1;
    view.header_y = header_y;
    view.header_fg = theme.fg_sidebar_directory;
    ui->draw_text(
        content_x + 1, header_y, view.header, theme.fg_sidebar_directory, theme.bg_sidebar, true);

    if (!git_rows.empty())
    {
      git_sidebar_selected = std::clamp(git_sidebar_selected, 0, (int)git_rows.size() - 1);
    }
    else
    {
      git_sidebar_selected = 0;
    }
    int max_scroll = std::max(0, (int)git_rows.size() - std::max(1, list_h));
    git_sidebar_scroll = std::clamp(git_sidebar_scroll, 0, max_scroll);

    if (git_rows.empty())
    {
      std::string empty = has_git_repo() ? "No changes" : "Not a Git repo";
      if (list_h > 0)
      {
        ui->draw_text(content_x + 1,
                      list_y,
                      truncate_cells(empty, std::max(0, content_w - 3)),
                      theme.fg_comment,
                      theme.bg_sidebar);
      }
    }
    else
    {
      for (int i = 0; i < list_h; i++)
      {
        int idx = i + git_sidebar_scroll;
        if (idx >= (int)git_rows.size())
          break;

        const auto &row = git_rows[(size_t)idx];
        bool selected = idx == git_sidebar_selected;
        int row_fg = theme.fg_sidebar;
        int row_bg = theme.bg_sidebar;
        auto git_colors = git_status_colors(theme, row.status);
        if (!selected && !row.status.empty())
        {
          row_fg = git_colors.first;
          row_bg = git_colors.second;
          ui->fill_rect(
              {content_x, list_y + i, std::max(1, content_w - 1), 1}, " ", row_fg, row_bg);
        }
        if (selected)
        {
          if (focus_state == FOCUS_SIDEBAR)
          {
            row_fg = theme.fg_sidebar_selected;
            row_bg = theme.bg_sidebar_selected;
          }
          else
          {
            row_fg = theme.fg_sidebar_selected_inactive;
            row_bg = theme.bg_sidebar_selected_inactive;
          }
          ui->draw_text(
              content_x, list_y + i, std::string(std::max(0, content_w - 1), ' '), row_fg, row_bg);
        }

        std::string symbol = git_status_symbol(row.status);
        if (symbol.empty())
        {
          symbol = " ";
        }
        ui->draw_text(content_x + 1, list_y + i, symbol, git_colors.first, row_bg, true);
        std::string label = " " + row.relative_path;
        ui->draw_text(content_x + 3,
                      list_y + i,
                      truncate_cells(label, std::max(0, content_w - 5)),
                      row_fg,
                      row_bg);

        SidebarPanelRowView r;
        r.x = content_x;
        r.y = list_y + i;
        r.w = std::max(1, content_w - 1);
        r.fg = row_fg;
        r.bg = row_bg;
        r.symbol = symbol;
        r.symbol_x = content_x + 1;
        r.symbol_fg = git_colors.first;
        r.symbol_bold = true;
        r.text = truncate_cells(label, std::max(0, content_w - 5));
        r.text_x = content_x + 3;
        view.rows.push_back(std::move(r));
      }
    }

    std::string footer;
    if (!git_rows.empty() && git_sidebar_selected >= 0
        && git_sidebar_selected < (int)git_rows.size())
    {
      footer = " " + git_rows[(size_t)git_sidebar_selected].relative_path;
    }
    else if (has_git_repo())
    {
      footer = std::to_string(git_dirty_count) + " changes";
    }
    else
    {
      footer = "Open a Git workspace";
    }
    if (h >= 3)
    {
      view.footer = truncate_cells(footer, std::max(0, content_w - 3));
      view.footer_x = content_x + 1;
      view.footer_y = y + h - 2;
      view.footer_fg = theme.fg_comment;
      ui->draw_text(content_x + 1, y + h - 2, view.footer, theme.fg_comment, theme.bg_sidebar);
    }

    if (emit_sidebar_view())
    {
      return;
    }
    return;
  }

  std::string header_label = ""
                   + (sidebar_render_cache_.root_label.empty()
                          ? ""
                          : " " + sidebar_render_cache_.root_label);
  int header_w = std::min(std::max(6, cell_count(header_label) + 2), std::max(1, content_w - 1));
  if (header_w > 2)
  {
    view.header = truncate_cells(header_label, header_w - 2);
    view.header_x = content_x + 1;
    view.header_y = y;
    view.header_fg = theme.fg_sidebar_directory;
    ui->draw_text(
        content_x + 1, y, view.header, theme.fg_sidebar_directory, theme.bg_sidebar, true);
  }

  int tree_y = y + 1;
  // One extra row is reserved at the bottom for the panel border, so the
  // footer moves up to y + h - 2 and the tree ends above it.
  int tree_h = sidebar_list_rows();
  const auto &rows = sidebar_render_cache_.rows;

  int max_scroll = std::max(0, (int)rows.size() - std::max(1, tree_h));
  if (file_tree_scroll < 0)
  {
    file_tree_scroll = 0;
  }
  else if (file_tree_scroll > max_scroll)
  {
    file_tree_scroll = max_scroll;
  }

  std::string active_file_path;
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    active_file_path = normalize_sidebar_path(buffers[current_buffer].filepath);
  }

  for (int i = 0; i < tree_h; i++)
  {
    int idx = i + file_tree_scroll;
    if (idx >= (int)rows.size())
      break;

    const auto &row = rows[(size_t)idx];
    const std::string git_xy = row.git_status;
    const std::string git_symbol = git_status_symbol(git_xy);
    const int sev = row.diagnostic_severity;
    const bool has_git = !git_xy.empty();
    const bool selected = idx == file_tree_selected;
    const bool is_active_file =
        !row.is_dir && !active_file_path.empty() && row.normalized_path == active_file_path;

    int row_fg = row.is_dir ? theme.fg_sidebar_directory : theme.fg_sidebar;
    int row_bg = theme.bg_sidebar;
    auto git_colors = git_status_colors(theme, git_xy);
    if (row.is_dir)
    {
      // A folder name is tinted by whatever happened inside it, most severe
      // diagnostic first, then the git state.
      if (sev > 0)
        row_fg = severity_to_color(sev, true);
      else if (has_git)
        row_fg = git_colors.first;
    }
    else if (has_git && !selected)
    {
      // Changed files take the git color on the name only; the row keeps the
      // panel background instead of being filled with a status band.
      row_fg = git_colors.first;
    }

    if (selected)
    {
      if (focus_state == FOCUS_SIDEBAR)
      {
        row_fg = theme.fg_sidebar_selected;
        row_bg = theme.bg_sidebar_selected;
      }
      else
      {
        row_fg = theme.fg_sidebar_selected_inactive;
        row_bg = theme.bg_sidebar_selected_inactive;
      }
      ui->draw_text(
          content_x, tree_y + i, std::string(std::max(0, content_w - 1), ' '), row_fg, row_bg);
    }

    if (is_active_file)
    {
      ui->draw_text(content_x, tree_y + i, "▌", theme.fg_sidebar_directory, row_bg, true);
    }

    const bool show_badges = w >= 16;
    const int border_x = w - 1;
    // Files badge how many errors and warnings they carry, with the git letter
    // appended when the file is also changed ("1, M"). Folders badge a dot.
    // Diagnostics own the color: an error outranks a warning, both outrank git.
    std::string badge;
    int badge_fg = -1;
    if (row.is_dir)
    {
      if (sev > 0 || has_git)
      {
        badge = "●";
        badge_fg = sev > 0 ? severity_to_color(sev, true) : git_colors.first;
      }
    }
    else
    {
      const int diag_total = row.diagnostic_errors + row.diagnostic_warnings;
      if (diag_total > 0)
      {
        badge = std::to_string(diag_total);
        badge_fg = severity_to_color(sev, false);
      }
      if (has_git)
      {
        if (!badge.empty())
          badge += ", ";
        badge += git_symbol;
        if (badge_fg < 0)
          badge_fg = git_colors.first;
      }
    }
    const int badge_cells = show_badges ? cell_count(badge) : 0;
    const int badge_x = border_x - 1 - badge_cells;
    const int label_max = badge_cells > 0 ? std::max(0, badge_x - (content_x + 1) - 1)
                                          : std::max(0, border_x - (content_x + 1));

    // Rows split into tree indent guides (comment color), then the
    // per-language icon glyph in its own brand color (files), then the name.
    // Directories keep their folder glyph baked into the label remainder
    // (theme directory color).
    const bool colored_icon = !row.is_dir && !row.icon.empty() && row.icon_fg >= 0;
    std::string row_label = truncate_cells(row.label, label_max);
    std::string row_name;
    std::string row_icon;
    int row_icon_fg = -1;
    int row_icon_col = -1;
    int row_name_col = content_x + 1;
    const int guide_cells = std::max(0, row.guide_cells);
    if (!row.guide.empty())
    {
      ui->draw_text(content_x + 1,
                    tree_y + i,
                    truncate_cells(row.guide, std::min(guide_cells, label_max)),
                    theme.fg_comment,
                    row_bg);
    }
    if (colored_icon)
    {
      // label layout for files: [guides][glyph][" "][name]
      row_icon = row.icon;
      row_icon_fg = row.icon_fg;
      row_icon_col = content_x + 1 + guide_cells;
      const int glyph_cells = cell_count(row.icon);
      row_name_col = row_icon_col + glyph_cells + 1;
      const int name_budget = std::max(0, label_max - guide_cells - glyph_cells - 1);
      row_name = truncate_cells(row.name, name_budget);
    }
    else
    {
      // Label = guide + folder glyph + name: draw the remainder after the
      // guides (which were painted above in the comment color).
      std::string rest = row_label;
      if (rest.size() >= row.guide.size()
          && rest.compare(0, row.guide.size(), row.guide) == 0)
      {
        rest = rest.substr(row.guide.size());
      }
      ui->draw_text(content_x + 1 + guide_cells, tree_y + i, rest, row_fg, row_bg);
    }
    if (colored_icon)
    {
      ui->draw_text(row_icon_col, tree_y + i, row_icon, row_icon_fg, row_bg);
      if (!row_name.empty())
      {
        ui->draw_text(row_name_col, tree_y + i, row_name, row_fg, row_bg);
      }
    }

    SidebarPanelRowView r;
    r.x = content_x + 1;
    r.y = tree_y + i;
    r.w = std::max(1, content_w - 2);
    if (selected)
    {
      r.x = content_x;
      r.w = std::max(1, content_w - 1);
    }
    r.fg = row_fg;
    r.bg = row_bg;
    std::string text_rest = row_label;
    if (text_rest.size() >= row.guide.size()
        && text_rest.compare(0, row.guide.size(), row.guide) == 0)
    {
      text_rest = text_rest.substr(row.guide.size());
    }
    r.text = colored_icon ? row_name : text_rest;
    r.text_x = colored_icon ? row_name_col : content_x + 1 + guide_cells;
    if (!row.guide.empty())
    {
      r.guide = truncate_cells(row.guide, guide_cells);
      r.guide_x = content_x + 1;
      r.guide_fg = theme.fg_comment;
    }
    if (colored_icon)
    {
      r.icon = row_icon;
      r.icon_x = row_icon_col;
      r.icon_fg = row_icon_fg;
    }
    if (is_active_file)
    {
      r.symbol = "▌";
      r.symbol_x = content_x;
      r.symbol_fg = theme.fg_sidebar_directory;
      r.symbol_bold = true;
    }
    if (badge_cells > 0)
    {
      r.badge = badge;
      r.badge_x = badge_x;
      r.badge_fg = badge_fg;
    }
    view.rows.push_back(std::move(r));
  }

  std::string footer;
  if (!rows.empty() && file_tree_selected >= 0 && file_tree_selected < (int)rows.size())
  {
    footer = " " + rows[(size_t)file_tree_selected].footer_label;
  }
  else if (has_git_repo())
  {
    footer = " " + git_branch;
    if (git_staged_count > 0)
    {
      footer += " +" + std::to_string(git_staged_count);
    }
    if (git_unstaged_count > 0)
    {
      footer += " ~" + std::to_string(git_unstaged_count);
    }
    if (git_untracked_count > 0)
    {
      footer += " ?" + std::to_string(git_untracked_count);
    }
    if (git_conflict_count > 0)
    {
      footer += " !" + std::to_string(git_conflict_count);
    }
  }
  else
  {
    footer = std::to_string(rows.size()) + " items";
  }
  if (h >= 3)
  {
    view.footer = truncate_cells(footer, std::max(0, content_w - 3));
    view.footer_x = content_x + 1;
    view.footer_y = y + h - 2;
    view.footer_fg = theme.fg_comment;
    ui->draw_text(content_x + 1, y + h - 2, view.footer, theme.fg_comment, theme.bg_sidebar);
  }

  if (emit_sidebar_view())
  {
    return;
  }
}

void Editor::render_collapsed_sidebar_handle()
{
  if (!collapsed_sidebar_handle_hit_test(0, topbar_height()))
  {
    return;
  }

  const ContentColumn col = content_column();
  if (col.h <= 0)
  {
    return;
  }

  // The handle draws nothing: its glyph sat on the editor's first column, and
  // the drag-to-open hit area above already covers the gesture. `col` is still
  // read so the column geometry stays the single source of truth for it.
  (void)col;
}
