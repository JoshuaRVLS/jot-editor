// The telescope-style file picker overlay.
//
// Two boxes, not one panel with a divider: the list on the left (query field,
// then file rows) and the selected file on the right, drawn like a small editor
// with line numbers and the file name in its own top border. Files only -- where
// a file lives is the dimmed folder path on its row.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_set>

using namespace overlay_internal;

namespace
{
  // Nerd Fonts glyphs the picker draws itself: the open/closed dot on each row
  // (the file's tab state), the magnifier in the list title and the chevron in
  // the query field.
  constexpr const char *kDotOpen = "\uf111";   // nf-fa-circle
  constexpr const char *kDotClosed = "\uf10c"; // nf-fa-circle_o
  constexpr const char *kSearchGlyph = "\uf002";
  constexpr const char *kPromptGlyph = "\uf054";

  std::string telescope_row_icon(const FileMatch &match, bool use_nerd_icons)
  {
    if (match.is_directory)
    {
      return use_nerd_icons ? "\uf07b " : "[D] "; // nf-fa-folder
    }
    if (use_nerd_icons)
    {
      const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(match.name);
      if (fti.glyph && fti.glyph[0])
      {
        return std::string(fti.glyph) + " ";
      }
    }
    return "\U000F0214 "; // 󰈔 generic file outline
  }

  int telescope_icon_fg(const FileMatch &match, bool use_nerd_icons)
  {
    if (match.is_directory || !use_nerd_icons)
    {
      return -1;
    }
    const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(match.name);
    return (fti.glyph && fti.glyph[0]) ? fti.color : -1;
  }

  // The dimmed folder a file sits in, "src/render/", clipped from the left when
  // the row cannot hold all of it (the deepest part is the useful part).
  std::string telescope_row_folder(const FileMatch &match)
  {
    if (match.is_directory || match.parent_path.empty() || match.parent_path == ".")
    {
      return "";
    }
    return match.parent_path + "/";
  }

  // The paths of the files that currently have a tab, normalised once per frame
  // so the per-row "is this file open" question stays a string compare.
  std::vector<std::string> open_buffer_paths(const std::vector<FileBuffer> &buffers)
  {
    std::vector<std::string> out;
    out.reserve(buffers.size());
    for (const FileBuffer &buf : buffers)
    {
      if (!buf.filepath.empty())
      {
        out.push_back(std::filesystem::path(buf.filepath).lexically_normal().string());
      }
    }
    return out;
  }

  bool path_is_open_buffer(const std::vector<std::string> &open_paths, const std::string &path)
  {
    if (path.empty())
    {
      return false;
    }
    const std::string normalised = std::filesystem::path(path).lexically_normal().string();
    for (const std::string &open : open_paths)
    {
      if (open == normalised)
      {
        return true;
      }
    }
    return false;
  }
} // namespace

void Editor::render_telescope()
{
  TelescopeLayout layout = telescope_layout_for(
      ui->get_render_width(), ui->get_height(), 1, std::max(1, ui->get_height() - status_height));
  if (!layout.valid)
  {
    return;
  }

  const bool use_nerd_icons = config.get_bool("lsp_completion_nerd_icons", true);
  const auto &results = telescope.get_results();
  const int selected = telescope.get_selected_index();
  const int result_count = telescope.get_result_count();
  const bool scan_pending = telescope.scan_pending();
  telescope.ensure_selected_visible(layout.list_h);

  const std::vector<std::string> open_paths = open_buffer_paths(buffers);
  const std::string folder = telescope.get_relative_root();
  const std::string selected_path = telescope.get_selected_path();
  const std::string selected_name =
      !selected_path.empty() ? std::filesystem::path(selected_path).filename().string()
                             : std::string();

  // A registered Lua UI handler renders the whole picker from this state; the
  // native geometry is passed through unchanged so mouse hit-testing (row
  // clicks, wheel regions, query focus) keeps working.
  if (lua_api && lua_api->has_lua_ui_handler("telescope"))
  {
    TelescopeView view;
    view.x = layout.x;
    view.y = layout.y;
    // The handler's float covers the whole region -- both boxes and the column
    // between them -- and draws each box's frame itself (the two borders stay
    // separate because neither is the float's own border).
    view.w = layout.region_w;
    view.h = layout.h;
    view.region_w = layout.region_w;
    view.inner_x = layout.inner_x;
    view.inner_y = layout.inner_y;
    view.inner_w = layout.inner_w;
    view.inner_h = layout.inner_h;
    view.query_x = layout.query_x;
    view.query_y = layout.query_y;
    view.query_w = layout.query_w;
    view.body_y = layout.body_y;
    view.body_h = layout.body_h;
    view.list_x = layout.list_x;
    view.list_y = layout.list_y;
    view.list_w = layout.list_w;
    view.list_h = layout.list_h;
    view.preview_x = layout.preview_x;
    view.preview_y = layout.preview_y;
    view.preview_w = layout.preview_w;
    view.preview_h = layout.preview_h;
    view.preview_inner_x = layout.preview_inner_x;
    view.preview_inner_y = layout.preview_inner_y;
    view.preview_inner_w = layout.preview_inner_w;
    view.preview_inner_h = layout.preview_inner_h;
    view.preview_text_y = layout.preview_text_y;
    view.preview_status_y = layout.preview_status_y;
    view.footer_y = layout.footer_y;
    view.show_preview = layout.show_preview;
    view.query = telescope.get_query();
    view.root = telescope.get_root_dir();
    view.folder = folder;
    view.title = " Find Files ";
    view.selected = selected;
    view.list_scroll = telescope.get_list_scroll_offset();
    view.result_count = result_count;
    view.scan_pending = scan_pending;
    view.scan_error = telescope.scan_error();
    view.focus = telescope.focus() == TelescopeFocus::Query     ? "query"
                 : telescope.focus() == TelescopeFocus::Preview ? "preview"
                                                                : "results";
    const int start_idx = telescope.get_list_scroll_offset();
    const int end_idx = std::min((int)results.size(), start_idx + layout.list_h);
    for (int i = start_idx; i < end_idx; i++)
    {
      const FileMatch &match = results[(size_t)i];
      TelescopeResultView rv;
      rv.name = match.name;
      rv.parent_path = telescope_row_folder(match);
      rv.is_directory = match.is_directory;
      rv.opened = path_is_open_buffer(open_paths, match.path);
      rv.match = match.match;
      if (!match.is_directory)
      {
        // Per-language file glyph in its brand color, like the sidebar.
        const jot_icons::FileTypeIcon fti = jot_icons::file_type_icon(match.name);
        rv.icon = std::string(fti.glyph);
        rv.icon_fg = fti.color;
      }
      view.results.push_back(std::move(rv));
    }
    if (layout.show_preview && selected >= 0 && selected < (int)results.size())
    {
      auto preview = telescope.get_selected_preview();
      view.preview.title = preview.title;
      view.preview.detail = preview.detail;
      view.preview.is_directory = preview.is_directory;
      view.preview.skipped = preview.skipped;
      view.preview.is_binary = preview.is_binary;
      view.preview.truncated = preview.truncated;
      view.preview.size_bytes = preview.size_bytes;
      view.preview.extension = std::filesystem::path(selected_path).extension().string();
      const int preview_scroll = telescope.get_preview_scroll_offset();
      const int preview_lines_h =
          std::max(0, layout.preview_inner_y + layout.preview_inner_h - layout.preview_text_y);
      view.preview.start_line = preview_scroll;
      for (int i = 0; i < preview_lines_h && preview_scroll + i < (int)preview.lines.size(); i++)
      {
        view.preview.lines.push_back(preview.lines[(size_t)(preview_scroll + i)]);
      }
    }
    if (lua_api->emit_telescope(view))
    {
      return;
    }
  }

  const int border_fg = theme.fg_panel_border;
  const int list_bg = theme.bg_telescope;
  const int view_bg = theme.bg_telescope_preview;

  // Backdrop for the whole picker: the two boxes sit in it, so the column
  // between them is part of the picker instead of dimmed editor content showing
  // through. The Lua kit's float fills the same region with the same colour
  // (panel_bg = the float colour), which keeps the two renders identical.
  ui->fill_rect({layout.x, layout.y, layout.region_w, layout.h},
                " ",
                theme.fg_default,
                theme.bg_panel_border);

  // ---- the list box ------------------------------------------------------
  UIRect list_rect = {layout.x, layout.y, layout.w, layout.h};
  ui->fill_rect(list_rect, " ", theme.fg_telescope, list_bg);
  ui->draw_border(list_rect, border_fg, list_bg);

  // Title sits in the top border: the magnifier, "Find Files", and the folder
  // the picker is scoped to (nothing to show while it is at the root).
  std::string list_title = std::string(" ") + kSearchGlyph + " Find Files ";
  if (!folder.empty())
  {
    list_title += "\u00b7 " + folder + " ";
  }
  ui->draw_text(layout.x + 1, layout.y, clip_text(list_title, layout.w - 4), theme.fg_telescope, list_bg);

  // Match count in the bottom border -- the box's own status line.
  std::string count = scan_pending ? "Scanning..." : std::to_string(result_count) + " file";
  if (!scan_pending && result_count != 1)
  {
    count += "s";
  }
  if (!scan_pending && !results.empty())
  {
    count += "  " + std::to_string(std::clamp(selected + 1, 1, result_count)) + "/"
             + std::to_string(result_count);
  }
  const std::string count_text = " " + count + " ";
  if (ui_cell_count(count_text) < layout.w - 3)
  {
    ui->draw_text(layout.x + layout.w - 1 - ui_cell_count(count_text),
                  layout.footer_y,
                  count_text,
                  theme.fg_comment,
                  list_bg);
  }

  // Query field: its own band, so the typed text reads as an input rather than
  // another row of the list.
  const bool query_focus = telescope.focus() == TelescopeFocus::Query;
  const std::string query_text =
      std::string(kPromptGlyph) + " " + telescope.get_query();
  const int query_bg = query_focus ? theme.bg_telescope_query : list_bg;
  const int query_fg = query_focus ? theme.fg_telescope_query : theme.fg_comment;
  UIRect query_rect = {layout.query_x, layout.query_y, layout.query_w, 1};
  ui->fill_rect(query_rect, " ", query_fg, query_bg);
  ui->draw_text(layout.query_x,
                layout.query_y,
                clip_text(query_text, layout.query_w),
                query_fg,
                query_bg);
  if (telescope.get_query().empty())
  {
    const int hint_x = layout.query_x + ui_cell_count(std::string(kPromptGlyph) + " ");
    ui->draw_text(hint_x,
                  layout.query_y,
                  clip_text("type to filter files", std::max(1, layout.query_w - (hint_x - layout.query_x))),
                  theme.fg_comment,
                  query_bg);
  }
  if (query_focus)
  {
    const int caret_x =
        layout.query_x
        + std::min(layout.query_w - 1,
                   std::max(0, ui_cell_count(std::string(kPromptGlyph) + " " + telescope.get_query())));
    ui->set_cursor(caret_x, layout.query_y);
  }

  // ---- the result rows ---------------------------------------------------
  const int start_idx = telescope.get_list_scroll_offset();
  const int end_idx = std::min((int)results.size(), start_idx + layout.list_h);
  if (results.empty())
  {
    std::string empty;
    if (scan_pending)
    {
      empty = "Scanning files...";
    }
    else if (!telescope.scan_error().empty())
    {
      empty = telescope.scan_error();
    }
    else if (telescope.get_query().empty())
    {
      empty = "No files found in this workspace.";
    }
    else
    {
      empty = "No files match the current query.";
    }
    ui->draw_text(layout.list_x + 1,
                  layout.list_y + std::max(0, (layout.list_h - 1) / 2),
                  clip_text(empty, std::max(1, layout.list_w - 2)),
                  theme.fg_comment,
                  list_bg);
  }

  for (int i = start_idx; i < end_idx; i++)
  {
    const FileMatch &match = results[(size_t)i];
    const int row_y = layout.list_y + (i - start_idx);
    const bool selected_row = (i == selected);

    // Selection (and hover, which moves the selection) is a filled band across
    // the row -- no gutter marker, so the row itself is the highlight.
    int name_fg = theme.fg_telescope;
    int row_bg = list_bg;
    if (selected_row)
    {
      row_bg = theme.bg_telescope_selected;
      name_fg = theme.fg_telescope_selected;
      UIRect row_rect = {layout.list_x, row_y, layout.list_w, 1};
      ui->fill_rect(row_rect, " ", name_fg, row_bg);
    }

    const bool opened = path_is_open_buffer(open_paths, match.path);
    const std::string dot = opened ? std::string(kDotOpen) + " " : std::string(kDotClosed) + " ";
    const int dot_fg = opened ? theme.fg_status_info : theme.fg_comment;
    const std::string icon = telescope_row_icon(match, use_nerd_icons);
    const int icon_fg = telescope_icon_fg(match, use_nerd_icons);
    const std::string folder_text = telescope_row_folder(match);

    const int dot_cells = ui_cell_count(dot);
    const int icon_cells = ui_cell_count(icon);
    const int fixed = dot_cells + icon_cells;
    const int row_w = std::max(1, layout.list_w);
    // The dimmed folder keeps at most a third of the row, so a deep path can
    // never squeeze the file name out.
    const int folder_w = std::min(ui_cell_count(folder_text), std::max(0, row_w / 3));
    const int name_w = std::max(1, row_w - fixed - folder_w - 1);

    ui->draw_text(layout.list_x, row_y, dot, selected_row ? theme.fg_telescope_selected : dot_fg, row_bg);
    ui->draw_text(layout.list_x + dot_cells,
                  row_y,
                  icon,
                  selected_row ? theme.fg_telescope_selected : (icon_fg >= 0 ? icon_fg : name_fg),
                  row_bg);
    ui->draw_text(layout.list_x + fixed,
                  row_y,
                  clip_text(match.name, name_w),
                  name_fg,
                  row_bg);
    if (folder_w > 0)
    {
      ui->draw_text(layout.list_x + row_w - folder_w,
                    row_y,
                    clip_path_left(folder_text, folder_w),
                    selected_row ? theme.fg_telescope_selected : theme.fg_comment,
                    row_bg);
    }
  }

  // ---- the file view box -------------------------------------------------
  if (!layout.show_preview)
  {
    return;
  }

  UIRect view_rect = {layout.preview_x, layout.preview_y, layout.preview_w, layout.preview_h};
  ui->fill_rect(view_rect, " ", theme.fg_telescope_preview, view_bg);
  ui->draw_border(view_rect, border_fg, view_bg);

  const bool has_selection = selected >= 0 && selected < (int)results.size();
  const TelescopePreview preview = has_selection ? telescope.get_selected_preview() : TelescopePreview();

  // The file name is the box's title, in its top border, with the folder it
  // lives in dimmed behind it -- this is the "which file am I looking at" line.
  if (has_selection)
  {
    const std::string glyph = telescope_row_icon(results[(size_t)selected], use_nerd_icons);
    const std::string view_title = " " + glyph + selected_name;
    ui->draw_text(layout.preview_x + 1, layout.preview_y, view_title, theme.fg_telescope_preview, view_bg);
    const int used = 1 + ui_cell_count(glyph + selected_name);
    const std::string sub = folder.empty() ? std::string() : "\u00b7 " + folder + " ";
    if (!sub.empty() && used + ui_cell_count(sub) < layout.preview_w - 3)
    {
      ui->draw_text(layout.preview_x + 1 + used, layout.preview_y, sub, theme.fg_comment, view_bg);
    }
  }

  // Content: line numbers in the gutter, the file's own syntax colors after
  // them, exactly like the editor paints a buffer.
  const int preview_scroll = telescope.get_preview_scroll_offset();
  const int lines_h = layout.preview_inner_h;
  const bool plain = preview.is_directory || preview.skipped || preview.is_binary;
  SyntaxHighlighter preview_highlighter;
  if (has_selection && !plain)
  {
    preview_highlighter.set_language(std::filesystem::path(selected_path).extension().string());
  }
  if (has_selection)
  {
    for (int i = 0; i < lines_h && preview_scroll + i < (int)preview.lines.size(); i++)
    {
      const int line_idx = preview_scroll + i;
      const std::string &line = preview.lines[(size_t)line_idx];
      const int row_y = layout.preview_text_y + i;
      if (plain)
      {
        ui->draw_text(layout.preview_inner_x,
                      row_y,
                      clip_text(line, layout.preview_inner_w),
                      theme.fg_comment,
                      view_bg);
        continue;
      }
      char line_no[24];
      std::snprintf(line_no, sizeof(line_no), "%5d ", line_idx + 1);
      ui->draw_text(layout.preview_inner_x, row_y, line_no, theme.fg_line_num, view_bg);
      const int text_col = 6;
      const int text_w = std::max(1, layout.preview_inner_w - text_col);
      const std::string clipped = clip_text(line, text_w);
      auto colors = preview_highlighter.get_colors(clipped);
      int chunk_start = 0;
      int chunk_token = 0;
      for (int c = 0; c <= (int)clipped.size(); c++)
      {
        int token = 0;
        if (c < (int)colors.size() && colors[c].first == 1)
        {
          token = colors[c].second;
        }
        if (c == 0)
        {
          chunk_token = token;
        }
        if (c == (int)clipped.size() || token != chunk_token)
        {
          if (c > chunk_start)
          {
            ui->draw_text(layout.preview_inner_x + text_col + chunk_start,
                          row_y,
                          clipped.substr(chunk_start, c - chunk_start),
                          syntax_preview_color(theme, chunk_token),
                          view_bg);
          }
          chunk_start = c;
          chunk_token = token;
        }
      }
    }
  }

  // Bottom border: size and line count for the file being shown, or why there
  // is nothing to show.
  std::string status;
  if (!has_selection)
  {
    status = "No selection";
  }
  else if (preview.is_directory)
  {
    status = "Folder";
  }
  else if (preview.skipped)
  {
    status = "Not previewed";
  }
  else if (preview.is_binary)
  {
    status = "Binary file";
  }
  else
  {
    status = std::to_string(preview.lines.size()) + " lines";
    // Bytes while the file is under a kilobyte, so a small file does not read
    // as "0.0 KB".
    if (preview.size_bytes > 0 && preview.size_bytes < 1024)
    {
      status += "  " + std::to_string(preview.size_bytes) + " B";
    }
    else if (preview.size_bytes >= 1024)
    {
      const double kb = (double)preview.size_bytes / 1024.0;
      char size_buf[32];
      std::snprintf(size_buf, sizeof(size_buf), kb >= 1024.0 ? "  %.1f MB" : "  %.1f KB",
                    kb >= 1024.0 ? kb / 1024.0 : kb);
      status += size_buf;
    }
    if (preview.truncated)
    {
      status += "  truncated";
    }
  }
  const std::string status_text = " " + status + " ";
  if (ui_cell_count(status_text) < layout.preview_w - 3)
  {
    ui->draw_text(layout.preview_x + layout.preview_w - 1 - ui_cell_count(status_text),
                  layout.preview_status_y,
                  status_text,
                  theme.fg_comment,
                  view_bg);
  }
}
