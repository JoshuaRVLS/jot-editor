// The telescope-style file picker overlay.
//
// Two boxes, not one panel with a divider: the list on the left (query field,
// then file rows) and the selected file on the right, drawn like a small editor
// with line numbers and the file name in its own top border. Files only -- where
// a file lives is the dimmed folder path on its row.
#include "bracket.h"
#include "column_utils.h"
#include "core/editor.h"
#include "core/file_icons.h"
#include "folding.h"
#include "lua/api/api.h"
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
  // The prompt's own marker: the plain `>` the picker's author knows, not a
  // Nerd Font chevron -- the row is a pointer into the list, and a glyph that
  // only renders on a patched font is not worth the ambiguity.
  constexpr const char *kPrompt = "> ";

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

  const std::string folder = telescope.get_relative_root();
  const std::string selected_path = telescope.get_selected_path();

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
      rv.path = match.relative_path;
      rv.is_directory = match.is_directory;
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

  // The picker's own background (one colour, no per-pane tint) lives inside the
  // borders and nowhere else: each box paints it, and the column between the two
  // boxes keeps the float's colour -- the same backdrop every other float sits
  // on, so the panes read as two surfaces rather than one wide panel with two
  // frames drawn in it.
  const int picker_bg = theme.bg_telescope;
  const int border_fg = theme.fg_panel_border;
  ui->fill_rect({layout.x, layout.y, layout.region_w, layout.h},
                " ",
                theme.fg_telescope,
                theme.bg_panel_border);

  // A label centred in a border run: `──── Results ────`. The rule is already
  // drawn, so this only overwrites the cells the label covers.
  auto draw_border_label = [&](int x, int w, int y, const std::string &label, int fg)
  {
    if (label.empty())
    {
      return;
    }
    const std::string text = " " + label + " ";
    const int cells = ui_cell_count(text);
    if (cells <= w - 2)
    {
      ui->draw_text(x + (w - cells) / 2, y, text, fg, picker_bg);
    }
  };

  // ---- the list box ------------------------------------------------------
  UIRect list_rect = {layout.x, layout.y, layout.w, layout.h};
  ui->fill_rect(list_rect, " ", theme.fg_telescope, picker_bg);
  ui->draw_border(list_rect, border_fg, picker_bg);
  // The box's own name on top, what the finder is doing at its foot -- plus the
  // folder it is scoped to, so `:find src` says where it is looking.
  draw_border_label(layout.x, layout.w, layout.y, "Results", theme.fg_telescope);
  draw_border_label(layout.x,
                    layout.w,
                    layout.footer_y,
                    folder.empty() ? "Find Files" : "Find Files \u00b7 " + folder,
                    theme.fg_telescope);

  // The prompt is the box's last interior row: `> what was typed` on the left,
  // the match count on the right. It shares the picker's background rather than
  // sitting on an inset field, and the row above it is the last result.
  const bool query_focus = telescope.focus() == TelescopeFocus::Query;
  const std::string prompt = kPrompt;
  const int prompt_cells = ui_cell_count(prompt);
  // Which match of how many, or the size of an empty list -- and whether the
  // scan that fills it is still running.
  std::string count;
  if (scan_pending)
  {
    count = "scanning...";
  }
  else if (!results.empty())
  {
    count = std::to_string(std::clamp(selected + 1, 1, result_count)) + " / "
            + std::to_string(result_count);
  }
  else
  {
    count = std::to_string(result_count) + " file";
    if (result_count != 1)
    {
      count += "s";
    }
  }
  const int count_cells = ui_cell_count(count);
  // Two cells of air at each end, and the count never crowds the query out.
  const int query_budget =
      std::max(1, layout.x + layout.w - 3 - count_cells - (layout.query_x + prompt_cells));
  ui->draw_text(layout.query_x, layout.query_y, prompt, theme.fg_keyword, picker_bg);
  const std::string query_text = clip_text(telescope.get_query(), query_budget);
  ui->draw_text(layout.query_x + prompt_cells,
                layout.query_y,
                query_text,
                theme.fg_telescope,
                picker_bg);
  if (telescope.get_query().empty())
  {
    ui->draw_text(layout.query_x + prompt_cells,
                  layout.query_y,
                  clip_text("type to filter files", query_budget),
                  theme.fg_comment,
                  picker_bg);
  }
  if (count_cells <= layout.x + layout.w - 3 - (layout.query_x + prompt_cells))
  {
    // Right-aligned against the box's last interior cell, exactly where the Lua
    // kit puts it.
    ui->draw_text(layout.x + layout.w - 1 - count_cells,
                  layout.query_y,
                  count,
                  theme.fg_comment,
                  picker_bg);
  }
  if (query_focus)
  {
    const int caret_x = std::min(layout.x + layout.w - 2,
                                 layout.query_x + prompt_cells
                                     + ui_cell_count(query_text));
    ui->set_cursor(caret_x, layout.query_y);
  }

  // ---- the result rows ---------------------------------------------------
  const int start_idx = telescope.get_list_scroll_offset();
  const int end_idx = std::min((int)results.size(), start_idx + layout.list_h);
  // The pointer's two cells are reserved on every row, so the names stay in one
  // column as the selection moves instead of shifting with its marker.
  const int pointer_cells = ui_cell_count(kPrompt);
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
    ui->draw_text(layout.list_x + pointer_cells,
                  layout.list_y + std::max(0, (layout.list_h - 1) / 2),
                  clip_text(empty, std::max(1, layout.list_w - pointer_cells)),
                  theme.fg_comment,
                  picker_bg);
  }

  for (int i = start_idx; i < end_idx; i++)
  {
    const FileMatch &match = results[(size_t)i];
    const int row_y = layout.list_y + (i - start_idx);
    const bool selected_row = (i == selected);

    // The selection is the pointer plus the row's band: the band gives the row
    // presence under the eye, the pointer says where the keyboard is.
    int row_fg = theme.fg_telescope;
    int row_bg = picker_bg;
    if (selected_row)
    {
      row_bg = theme.bg_telescope_selected;
      row_fg = theme.fg_telescope_selected;
      UIRect row_rect = {layout.list_x, row_y, layout.list_w, 1};
      ui->fill_rect(row_rect, " ", row_fg, row_bg);
    }

    const std::string icon = telescope_row_icon(match, use_nerd_icons);
    const int icon_fg = telescope_icon_fg(match, use_nerd_icons);
    const int icon_cells = ui_cell_count(icon);
    // The label is the file's path from the picker's scope, clipped from the
    // left: a name alone cannot tell src/render/frame.cpp from the one in
    // src/input, and the name is the half of the path worth keeping.
    const int path_w = std::max(1, layout.list_w - pointer_cells - icon_cells);

    ui->draw_text(layout.list_x,
                  row_y,
                  selected_row ? kPrompt : "  ",
                  selected_row ? row_fg : theme.fg_comment,
                  row_bg);
    ui->draw_text(layout.list_x + pointer_cells,
                  row_y,
                  icon,
                  selected_row ? row_fg : (icon_fg >= 0 ? icon_fg : row_fg),
                  row_bg);
    ui->draw_text(layout.list_x + pointer_cells + icon_cells,
                  row_y,
                  clip_path_left(match.relative_path, path_w),
                  row_fg,
                  row_bg);
  }

  // ---- the file view box -------------------------------------------------
  if (!layout.show_preview)
  {
    return;
  }

  UIRect view_rect = {layout.preview_x, layout.preview_y, layout.preview_w, layout.preview_h};
  ui->fill_rect(view_rect, " ", theme.fg_telescope, picker_bg);
  ui->draw_border(view_rect, border_fg, picker_bg);
  draw_border_label(layout.preview_x, layout.preview_w, layout.preview_y, "Preview", theme.fg_telescope);

  const bool has_selection = selected >= 0 && selected < (int)results.size();
  const TelescopePreview preview = has_selection ? telescope.get_selected_preview() : TelescopePreview();

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
                      picker_bg);
        continue;
      }
      char line_no[24];
      std::snprintf(line_no, sizeof(line_no), "%5d ", line_idx + 1);
      ui->draw_text(layout.preview_inner_x, row_y, line_no, theme.fg_line_num, picker_bg);
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
                          picker_bg);
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
  draw_border_label(layout.preview_x,
                    layout.preview_w,
                    layout.preview_status_y,
                    status,
                    theme.fg_comment);
}
