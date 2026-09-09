// The telescope-style file picker overlay.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace overlay_internal;

  std::string telescope_icon(bool directory, bool use_nerd_icons)
  {
    if (!use_nerd_icons)
    {
      return directory ? "[D] " : "[F] ";
    }
    return directory ? " " : "󰈔 ";
  }

void Editor::render_telescope()
{
  TelescopeLayout layout = telescope_layout_for(
      ui->get_render_width(), ui->get_height(), 1, std::max(1, ui->get_height() - status_height));
  if (!layout.valid)
    return;

  const bool use_nerd_icons = config.get_bool("lsp_completion_nerd_icons", true);

  const auto &results = telescope.get_results();
  int selected = telescope.get_selected_index();
  int result_count = telescope.get_result_count();
  telescope.ensure_selected_visible(layout.list_h);

  // A registered Lua UI handler renders the whole telescope from this state;
  // the native layout geometry is passed through unchanged so mouse
  // hit-testing (row clicks, wheel regions, query focus) keeps working.
  if (lua_api && lua_api->has_lua_ui_handler("telescope"))
  {
    TelescopeView view;
    view.x = layout.x;
    view.y = layout.y;
    view.w = layout.w;
    view.h = layout.h;
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
    view.footer_y = layout.footer_y;
    view.show_preview = layout.show_preview;
    view.query = telescope.get_query();
    view.root = telescope.get_root_dir();
    view.title = " Find Files ";
    view.selected = selected;
    view.list_scroll = telescope.get_list_scroll_offset();
    view.result_count = result_count;
    view.scan_pending = telescope.scan_pending();
    view.scan_error = telescope.scan_error();
    view.focus = telescope.focus() == TelescopeFocus::Query     ? "query"
                 : telescope.focus() == TelescopeFocus::Preview ? "preview"
                                                                : "results";
    const int start_idx = telescope.get_list_scroll_offset();
    const int end_idx = std::min((int)results.size(), start_idx + layout.list_h);
    for (int i = start_idx; i < end_idx; i++)
    {
      TelescopeResultView rv;
      rv.name = results[(size_t)i].name;
      rv.parent_path = results[(size_t)i].parent_path;
      rv.is_directory = results[(size_t)i].is_directory;
      view.results.push_back(std::move(rv));
    }
    if (layout.show_preview && selected >= 0 && selected < (int)results.size())
    {
      auto preview = telescope.get_selected_preview();
      view.preview.title = preview.title;
      view.preview.detail = preview.detail;
      view.preview.is_directory = preview.is_directory;
      view.preview.skipped = preview.skipped;
      view.preview.extension =
          std::filesystem::path(telescope.get_selected_path()).extension().string();
      const int preview_scroll = telescope.get_preview_scroll_offset();
      const int line_start_y = layout.preview_y + 4;
      const int preview_lines_h = std::max(0, layout.preview_y + layout.preview_h - line_start_y);
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

  UIRect rect = {layout.x, layout.y, layout.w, layout.h};
  ui->fill_rect(rect, " ", theme.fg_telescope, theme.bg_telescope);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_telescope);

  std::string title = " Find Files ";
  std::string count =
      telescope.scan_pending() ? "Scanning..." : std::to_string(result_count) + " match";
  if (!telescope.scan_pending() && result_count != 1)
    count += "es";
  if (!telescope.scan_pending() && !results.empty())
  {
    count += "  " + std::to_string(std::clamp(selected + 1, 1, result_count)) + "/"
             + std::to_string(result_count);
  }
  std::string root = telescope.get_root_dir();
  if (ui_cell_count(root) > layout.inner_w - 2)
  {
    root = clip_path_left(root, layout.inner_w - 2);
  }
  ui->draw_text(layout.inner_x + 1, layout.y, title, theme.fg_telescope, theme.bg_telescope, true);
  ui->draw_text(std::max(layout.inner_x + 1, layout.x + layout.w - (int)count.size() - 2),
                layout.y,
                count,
                theme.fg_comment,
                theme.bg_telescope);
  ui->draw_text(layout.inner_x + 1,
                layout.inner_y,
                clip_text(root, layout.inner_w - 2),
                theme.fg_comment,
                theme.bg_telescope);

  std::string query = "  > " + telescope.get_query();
  if (telescope.get_query().empty())
  {
    query += "type to filter files";
  }
  UIRect query_rect = {layout.query_x, layout.query_y, layout.query_w, 1};
  if (layout.inner_h >= 3)
  {
    const bool query_focus = telescope.focus() == TelescopeFocus::Query;
    ui->fill_rect(
        query_rect, " ", theme.fg_telescope, query_focus ? theme.bg_selection : theme.bg_command);
    ui->draw_text(layout.query_x,
                  layout.query_y,
                  clip_text(query, layout.query_w),
                  theme.fg_telescope,
                  query_focus ? theme.bg_selection : theme.bg_command,
                  true);
    if (query_focus)
    {
      const int caret_x = layout.query_x
                          + std::min(layout.query_w - 1,
                                     std::max(0, ui_cell_count("  > " + telescope.get_query())));
      ui->set_cursor(caret_x, layout.query_y);
    }
  }

  if (layout.show_preview)
  {
    int separator_x = layout.preview_x - 2;
    for (int row = layout.body_y; row < layout.body_y + layout.body_h; row++)
    {
      ui->draw_text(separator_x, row, "│", theme.fg_panel_border, theme.bg_telescope);
    }
    ui->draw_text(layout.preview_x,
                  layout.preview_y,
                  "Preview",
                  telescope.focus() == TelescopeFocus::Preview ? theme.fg_telescope_selected
                                                               : theme.fg_telescope,
                  theme.bg_telescope,
                  true);
  }

  int start_idx = telescope.get_list_scroll_offset();
  int end_idx = std::min((int)results.size(), start_idx + layout.list_h);

  if (results.empty())
  {
    std::string empty;
    if (telescope.scan_pending())
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
    ui->draw_text(layout.list_x,
                  layout.list_y + std::max(0, layout.list_h / 2),
                  clip_text(empty, layout.list_w),
                  theme.fg_comment,
                  theme.bg_telescope);
  }

  for (int i = start_idx; i < end_idx; i++)
  {
    int row_y = layout.list_y + (i - start_idx);
    int fg = theme.fg_telescope, bg = theme.bg_telescope;

    if (i == selected)
    {
      fg = theme.fg_telescope_selected;
      bg = theme.bg_telescope_selected;
      UIRect row_rect = {layout.list_x - 1, row_y, layout.list_w + 1, 1};
      ui->fill_rect(row_rect, " ", fg, bg);
    }

    std::string icon = telescope_icon(results[i].is_directory, use_nerd_icons);
    std::string name = results[i].name;
    std::string parent = results[i].parent_path == "." ? "" : "  " + results[i].parent_path;
    int parent_w = std::min(ui_cell_count(parent), std::max(0, layout.list_w / 2));
    int name_w = std::max(1, layout.list_w - ui_cell_count(icon) - parent_w);
    std::string row = icon + clip_text(name, name_w);
    if (parent_w > 0)
    {
      row += clip_path_left(parent, parent_w);
    }
    ui->draw_text(layout.list_x, row_y, clip_text(row, layout.list_w), fg, bg, i == selected);
  }

  if (layout.show_preview)
  {
    int preview_inner_w = layout.preview_w;
    if (!results.empty() && selected >= 0 && selected < (int)results.size())
    {
      auto preview = telescope.get_selected_preview();
      int preview_scroll = telescope.get_preview_scroll_offset();
      std::string title_line = clip_path_left(preview.title, preview_inner_w);
      ui->draw_text(layout.preview_x,
                    layout.preview_y + 1,
                    title_line,
                    theme.fg_telescope_preview,
                    theme.bg_telescope_preview,
                    true);
      if (!preview.detail.empty() && layout.body_h > 2)
      {
        ui->draw_text(layout.preview_x,
                      layout.preview_y + 2,
                      clip_text(preview.detail, preview_inner_w),
                      theme.fg_comment,
                      theme.bg_telescope_preview);
      }

      int line_start_y = layout.preview_y + 4;
      int preview_lines_h = std::max(0, layout.preview_y + layout.preview_h - line_start_y);
      SyntaxHighlighter preview_highlighter;
      preview_highlighter.set_language(
          std::filesystem::path(telescope.get_selected_path()).extension().string());
      for (int i = 0; i < preview_lines_h && preview_scroll + i < (int)preview.lines.size(); i++)
      {
        int line_idx = preview_scroll + i;
        std::string line = preview.lines[line_idx];
        if (preview.is_directory || preview.skipped)
        {
          ui->draw_text(layout.preview_x,
                        line_start_y + i,
                        clip_text(line, preview_inner_w),
                        theme.fg_comment,
                        theme.bg_telescope_preview);
        }
        else
        {
          char line_no[16];
          std::snprintf(line_no, sizeof(line_no), "%3d ", line_idx + 1);
          int text_w = std::max(1, preview_inner_w - 4);
          ui->draw_text(layout.preview_x,
                        line_start_y + i,
                        line_no,
                        theme.fg_comment,
                        theme.bg_telescope_preview);
          std::string clipped = clip_text(line, text_w);
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
                ui->draw_text(layout.preview_x + 4 + chunk_start,
                              line_start_y + i,
                              clipped.substr(chunk_start, c - chunk_start),
                              syntax_preview_color(theme, chunk_token),
                              theme.bg_telescope_preview);
              }
              chunk_start = c;
              chunk_token = token;
            }
          }
        }
      }
    }
    else
    {
      ui->draw_text(layout.preview_x,
                    layout.preview_y + 2,
                    "Select a file to preview.",
                    theme.fg_comment,
                    theme.bg_telescope);
    }
  }

  std::string footer = telescope.scan_pending() ? "Searching"
                       : results.empty()        ? "No selection"
                                                : telescope.get_selected_relative_path();
  if (!results.empty() && layout.show_preview)
  {
    auto preview = telescope.get_selected_preview();
    if (!preview.detail.empty())
    {
      footer += "  " + preview.detail;
    }
  }
  ui->draw_text(layout.inner_x + 1,
                layout.footer_y,
                clip_text(footer, layout.inner_w - 2),
                theme.fg_comment,
                theme.bg_telescope);
}
