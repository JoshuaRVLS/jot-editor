// Tree-sitter status and LSP status modals: install-job progress rows
// and per-server state tables.
#include "editor.h"
#include "jot/lua/api.h"
#include "render/ui_internal.h"

using namespace ui_internal;
#include "tools/lsp/install.h"
#include "tree_sitter/manager.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

namespace
{
  struct TreeSitterStatusRenderRow
  {
    std::string section;
    std::string language;
    std::string detail;
    int color = 7;
  };

  std::string ts_display_name(const std::string &language)
  {
    std::string out = language;
    std::replace(out.begin(), out.end(), '_', '-');
    return out;
  }

  void
  ts_add_section(std::vector<TreeSitterStatusRenderRow> &rows, const std::string &title, int count)
  {
    rows.push_back({title, "", std::to_string(count), 8});
  }

  // Draw `text`, rendering characters whose byte offsets appear in `match`
  // (sorted, ascending) in `match_fg` + bold and the rest in `fg` (bold only
  // when `plain_bold`). Splits only on UTF-8 character boundaries and advances
  // the pen by cell width, so wide glyphs keep column alignment.
} // namespace

void Editor::render_tree_sitter_status_modal()
{
  if (!show_tree_sitter_status_modal)
  {
    return;
  }

  std::set<std::string> active;
  std::set<std::string> installing;
  for (auto &buf : buffers)
  {
    if (buf.syntax_engine == SYNTAX_ENGINE_UNKNOWN && !buf.filepath.empty() && buf.line_count() > 0)
    {
      int line_idx = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
      get_line_syntax_colors(buf, line_idx);
    }
    if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER)
    {
#ifdef JOT_TREESITTER
      if (!buf.ts_language_id.empty())
      {
        active.insert(buf.ts_language_id);
      }
      else
#endif
          if (!buf.syntax_language_label.empty())
      {
        active.insert(buf.syntax_language_label);
      }
    }
  }

  std::vector<TreeSitterStatusRenderRow> installing_rows;
  for (const auto &job : tree_sitter_install_jobs)
  {
    if (job.running)
    {
      installing.insert(job.language);
      installing_rows.push_back({"",
                                 ts_display_name(job.language),
                                 job.progress.empty() ? "running" : job.progress,
                                 theme.fg_status_warning});
    }
    else if (job.failed)
    {
      installing_rows.push_back({"",
                                 ts_display_name(job.language),
                                 job.progress.empty() ? "failed" : job.progress,
                                 theme.fg_status_error});
    }
  }

  std::vector<TreeSitterStatusRenderRow> active_rows;
  std::vector<TreeSitterStatusRenderRow> installed_rows;
  std::vector<TreeSitterStatusRenderRow> uninstalled_rows;

  for (const auto &lang : ts_manager_.language_names())
  {
    if (active.find(lang) != active.end())
    {
      active_rows.push_back(
          {"", ts_display_name(lang), "active in open buffer", theme.fg_status_info});
      continue;
    }
    if (installing.find(lang) != installing.end())
    {
      continue;
    }
#ifdef JOT_TREESITTER
    TreeSitterRuntimeStatus status = ts_manager_.runtime_status_for_language(lang);
    if (status.parser_loaded)
    {
      std::string detail = status.query_loaded ? status.query_message : status.parser_message;
      installed_rows.push_back(
          {"", ts_display_name(lang), detail.empty() ? "parser loaded" : detail, theme.fg_command});
    }
    else
    {
      uninstalled_rows.push_back(
          {"",
           ts_display_name(lang),
           status.parser_message.empty() ? "not installed" : status.parser_message,
           theme.fg_comment});
    }
#else
    uninstalled_rows.push_back(
        {"", ts_display_name(lang), "Tree-sitter runtime not available", theme.fg_comment});
#endif
  }

  // Filetypes open in buffers that are known but have no grammar or rules
  // (catalog languages such as .astro, or a registered grammar whose parser
  // is not installed and which is not otherwise listed).
  std::vector<TreeSitterStatusRenderRow> detected_rows;
  {
    // Materialize once: calling language_names() for begin() and end() would
    // pair iterators from two different temporaries (iterating one buffer
    // toward the other's end is UB and used to crash here).
    const std::vector<std::string> manager_language_names = ts_manager_.language_names();
    std::set<std::string> manager_names(manager_language_names.begin(),
                                        manager_language_names.end());
    std::set<std::string> seen;
    for (const auto &buf : buffers)
    {
      if (buf.syntax_engine != SYNTAX_ENGINE_NONE || buf.syntax_language_label.empty()
          || seen.find(buf.syntax_language_label) != seen.end())
      {
        continue;
      }
      if (manager_names.find(buf.syntax_language_label) != manager_names.end())
      {
        continue; // already listed under Installed / Uninstalled above
      }
      seen.insert(buf.syntax_language_label);
      detected_rows.push_back(
          {"",
           ts_display_name(buf.syntax_language_label),
           "known filetype — no parser or rules",
           theme.fg_status_warning});
    }
  }

  std::vector<TreeSitterStatusRenderRow> rows;
  ts_add_section(rows, "Active", (int)active_rows.size());
  rows.insert(rows.end(), active_rows.begin(), active_rows.end());
  ts_add_section(rows, "Detected", (int)detected_rows.size());
  rows.insert(rows.end(), detected_rows.begin(), detected_rows.end());
  ts_add_section(rows, "Installing", (int)installing_rows.size());
  rows.insert(rows.end(), installing_rows.begin(), installing_rows.end());
  ts_add_section(rows, "Installed", (int)installed_rows.size());
  rows.insert(rows.end(), installed_rows.begin(), installed_rows.end());
  ts_add_section(rows, "Uninstalled", (int)uninstalled_rows.size());
  rows.insert(rows.end(), uninstalled_rows.begin(), uninstalled_rows.end());

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();
  int w = std::min(std::max(48, screen_w - 8), 92);
  int h = std::min(std::max(12, screen_h - 6), 28);
  if (screen_w < 54)
  {
    w = std::max(20, screen_w - 2);
  }
  if (screen_h < 16)
  {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 2);

  // Modal overlay: dim the editor underneath (same as popup / LSP manager).
  ui->dim_rect({0, 0, screen_w, screen_h});

  // Lua UI handler takes over rendering when registered; geometry is passed
  // unchanged so wheel-scroll and click-outside-to-close keep working.
  if (lua_api && lua_api->has_lua_ui_handler("tree_sitter_status"))
  {
    TsStatusView view;
    view.scroll = tree_sitter_status_scroll;
    view.hover = tree_sitter_status_hover_row;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.rows.reserve(rows.size());
    for (const auto &row : rows)
    {
      TsStatusRowView v;
      v.section = !row.section.empty();
      v.label = v.section ? row.section : row.language;
      v.detail = row.detail;
      v.color = row.color;
      view.rows.push_back(std::move(v));
    }
    if (lua_api->emit_tree_sitter_status(view))
    {
      return;
    }
  }

  // Same panel surface convention as the popup / LSP manager / palette.
  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(
      *ui,
      rect,
      {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border, panel_theme.bg_command});
  ui_draw_panel_title(*ui, rect, " Tree-sitter", theme.fg_command, panel_theme.bg_command);

  int list_h = std::max(0, h - 4);
  int max_scroll = std::max(0, (int)rows.size() - list_h);
  tree_sitter_status_scroll = std::clamp(tree_sitter_status_scroll, 0, max_scroll);

  int lang_w = std::max(12, std::min(24, w / 3));
  for (int i = 0; i < list_h; i++)
  {
    int idx = tree_sitter_status_scroll + i;
    if (idx < 0 || idx >= (int)rows.size())
    {
      break;
    }
    const auto &row = rows[idx];
    int row_y = y + 2 + i;
    if (!row.section.empty())
    {
      std::string title = row.section + " (" + row.detail + ")";
      ui->draw_text(x + 1,
                    row_y,
                    ui_truncate_cells(title, w - 2),
                    theme.fg_comment,
                    panel_theme.bg_command,
                    true);
      continue;
    }
    std::string lang = ui_truncate_cells(row.language, lang_w);
    std::string detail = ui_truncate_cells(row.detail, w - lang_w - 5);
    // Language names must stay readable on the flat panel background. The
    // status chip colors (fg_status_info / fg_status_warning) default to 0
    // (black) when a theme does not define them, which is invisible here, so
    // fall back to the panel text color instead of trusting row.color.
    int name_fg = row.color != 0 ? row.color : theme.fg_command;
    ui->draw_text(x + 2, row_y, lang, name_fg, panel_theme.bg_command, true);
    ui->draw_text(x + 2 + lang_w, row_y, detail, theme.fg_comment, panel_theme.bg_command);
  }

  std::string footer = "Esc close  Up/Down scroll";
  if (max_scroll > 0)
  {
    footer +=
        "  " + std::to_string(tree_sitter_status_scroll + 1) + "/" + std::to_string(max_scroll + 1);
  }
  ui_draw_footer(
      *ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

void Editor::render_lsp_status_modal()
{
  if (!show_lsp_status_modal)
  {
    return;
  }

  std::vector<TreeSitterStatusRenderRow> running_rows;
  std::vector<TreeSitterStatusRenderRow> starting_rows;
  std::set<std::string> attached_languages;
  for (const auto &client : lsp_clients)
  {
    if (!client)
    {
      continue;
    }
    const std::string language = client->get_language();
    if (language.empty())
    {
      continue;
    }
    attached_languages.insert(language);
    if (!client->is_running())
    {
      std::string why = client->get_last_error();
      starting_rows.push_back({"",
                               language,
                               why.empty() ? "starting…" : why,
                               theme.fg_status_warning});
      continue;
    }
    int errs = 0, warns = 0, infos = 0, hints = 0;
    lsp_server_diagnostic_counts(language, &errs, &warns, &infos, &hints);
    std::string detail;
    if (errs > 0)
      detail += " E" + std::to_string(errs);
    if (warns > 0)
      detail += " W" + std::to_string(warns);
    if (infos > 0)
      detail += " I" + std::to_string(infos);
    if (hints > 0)
      detail += " H" + std::to_string(hints);
    if (detail.empty())
      detail = " connected";
    else
      detail = detail.substr(1) + " problem(s)";
    const std::string root = client->get_root_path();
    if (!root.empty())
    {
      std::filesystem::path p(root);
      const std::string base = p.filename().string();
      detail += " · " + (base.empty() ? root : base);
    }
    running_rows.push_back({"", language, detail, theme.fg_status_info});
  }

  std::vector<TreeSitterStatusRenderRow> install_rows;
  for (const auto &job : lsp_install_jobs)
  {
    if (job.running)
    {
      install_rows.push_back({"",
                              job.server,
                              (job.removing ? "removing — " : "installing — ")
                                  + (job.progress.empty() ? "running" : job.progress),
                              theme.fg_status_info});
    }
    else if (job.failed)
    {
      install_rows.push_back({"",
                              job.server,
                              job.progress.empty() ? "failed" : job.progress,
                              theme.fg_status_error});
    }
  }

  std::vector<TreeSitterStatusRenderRow> installed_rows;
  for (const std::string &id : LspInstall::installed_ids())
  {
    if (attached_languages.find(id) != attached_languages.end())
    {
      continue;
    }
    installed_rows.push_back({"", id, "installed", theme.fg_command});
  }

  std::vector<TreeSitterStatusRenderRow> rows;
  ts_add_section(rows, "Running", (int)running_rows.size());
  rows.insert(rows.end(), running_rows.begin(), running_rows.end());
  ts_add_section(rows, "Starting", (int)starting_rows.size());
  rows.insert(rows.end(), starting_rows.begin(), starting_rows.end());
  ts_add_section(rows, "Installing", (int)install_rows.size());
  rows.insert(rows.end(), install_rows.begin(), install_rows.end());
  ts_add_section(rows, "Installed", (int)installed_rows.size());
  rows.insert(rows.end(), installed_rows.begin(), installed_rows.end());

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();
  int w = std::min(std::max(48, screen_w - 8), 92);
  int h = std::min(std::max(12, screen_h - 6), 28);
  if (screen_w < 54)
  {
    w = std::max(20, screen_w - 2);
  }
  if (screen_h < 16)
  {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 2);

  ui->dim_rect({0, 0, screen_w, screen_h});

  if (lua_api && lua_api->has_lua_ui_handler("lsp_status"))
  {
    TsStatusView view;
    view.scroll = lsp_status_scroll;
    view.hover = lsp_status_hover_row;
    view.x = x;
    view.y = y;
    view.w = w;
    view.h = h;
    view.rows.reserve(rows.size());
    for (const auto &row : rows)
    {
      TsStatusRowView v;
      v.section = !row.section.empty();
      v.label = v.section ? row.section : row.language;
      v.detail = row.detail;
      v.color = row.color;
      view.rows.push_back(std::move(v));
    }
    if (lua_api->emit_lsp_status(view))
    {
      return;
    }
  }

  const Theme panel_theme = [&]()
  {
    Theme t = theme;
    t.bg_command = theme.bg_panel_border;
    return t;
  }();

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui,
                rect,
                {theme.fg_command, panel_theme.bg_command, theme.fg_panel_border,
                 panel_theme.bg_command});
  ui_draw_panel_title(*ui, rect, " LSP", theme.fg_command, panel_theme.bg_command);

  int list_h = std::max(0, h - 4);
  int max_scroll = std::max(0, (int)rows.size() - list_h);
  lsp_status_scroll = std::clamp(lsp_status_scroll, 0, max_scroll);

  int lang_w = std::max(12, std::min(24, w / 3));
  for (int i = 0; i < list_h; i++)
  {
    int idx = lsp_status_scroll + i;
    if (idx < 0 || idx >= (int)rows.size())
    {
      break;
    }
    const auto &row = rows[idx];
    int row_y = y + 2 + i;
    if (!row.section.empty())
    {
      std::string title = row.section + " (" + row.detail + ")";
      ui->draw_text(x + 1,
                    row_y,
                    ui_truncate_cells(title, w - 2),
                    theme.fg_comment,
                    panel_theme.bg_command,
                    true);
      continue;
    }
    std::string lang = ui_truncate_cells(row.language, lang_w);
    std::string detail = ui_truncate_cells(row.detail, w - lang_w - 5);
    int name_fg = row.color != 0 ? row.color : theme.fg_command;
    ui->draw_text(x + 2, row_y, lang, name_fg, panel_theme.bg_command, true);
    ui->draw_text(x + 2 + lang_w, row_y, detail, theme.fg_comment, panel_theme.bg_command);
  }

  std::string footer = "Esc close  Up/Down scroll";
  if (max_scroll > 0)
  {
    footer += "  " + std::to_string(lsp_status_scroll + 1) + "/" + std::to_string(max_scroll + 1);
  }
  ui_draw_footer(*ui, rect, ui_truncate_cells(footer, w - 2), theme.fg_comment, panel_theme.bg_command);
}

