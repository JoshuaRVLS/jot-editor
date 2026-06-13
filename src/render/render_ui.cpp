#include "editor.h"
#include "python_api.h"
#include "tree_sitter_catalog.h"
#include "ui_components.h"
#include "ui_text.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

namespace {
std::string status_path_basename(const std::string &path,
                                 const std::string &fallback) {
  if (path.empty())
    return fallback;
  std::filesystem::path p(path);
  std::string name = p.filename().string();
  if (!name.empty())
    return name;
  name = p.root_path().string();
  return name.empty() ? path : name;
}

std::string status_workspace_label(const std::string &root_dir) {
  if (root_dir.empty() || root_dir == ".")
    return "No workspace";
  return status_path_basename(root_dir, root_dir);
}

struct StatusSegment {
  std::string text;
  int fg = 7;
  int bg = 0;
  bool bold = false;
  bool optional = false;
  int priority = 0;
};

struct TreeSitterStatusRenderRow {
  std::string section;
  std::string language;
  std::string detail;
  int color = 7;
};

int status_layout_width(const std::vector<StatusSegment> &segments) {
  if (segments.empty())
    return 0;
  int width = 0;
  for (const auto &segment : segments) {
    width += ui_cell_count(segment.text);
  }
  width += std::max(0, (int)segments.size() - 1);
  return width;
}

int status_draw_segmented_at(UI *ui, int x, int y, int w,
                             const std::vector<StatusSegment> &segments) {
  int pos = x;
  const int end = x + std::max(0, w);
  for (size_t i = 0; i < segments.size() && pos < end; i++) {
    const auto &segment = segments[i];
    const int remaining = end - pos;
    std::string text = ui_take_cells(segment.text, remaining);
    ui->draw_text(pos, y, text, segment.fg, segment.bg, segment.bold);
    pos += ui_cell_count(text);

    if (pos < end && i + 1 < segments.size()) {
      ui->draw_text(pos, y, "", segment.bg, segments[i + 1].bg, true);
      pos++;
    }
  }
  return pos;
}

void status_drop_optional_to_fit(std::vector<StatusSegment> &segments,
                                 int max_w) {
  while (status_layout_width(segments) > max_w) {
    auto removable = segments.end();
    for (auto it = segments.begin(); it != segments.end(); ++it) {
      if (!it->optional)
        continue;
      if (removable == segments.end() || it->priority < removable->priority) {
        removable = it;
      }
    }
    if (removable == segments.end())
      break;
    segments.erase(removable);
  }
}

void status_draw_clipped(UI *ui, int x, int y, int w,
                         const std::string &text, int fg, int bg,
                         bool bold = false) {
  if (w <= 0)
    return;
  ui->draw_text(x, y, ui_take_cells(text, w), fg, bg, bold);
}

std::string ts_display_name(const std::string &language) {
  std::string out = language;
  std::replace(out.begin(), out.end(), '_', '-');
  return out;
}

void ts_add_section(std::vector<TreeSitterStatusRenderRow> &rows,
                    const std::string &title, int count) {
  rows.push_back({title, "", std::to_string(count), 8});
}
} // namespace

void Editor::render_tree_sitter_status_modal() {
  if (!show_tree_sitter_status_modal) {
    return;
  }

  std::set<std::string> active;
  std::set<std::string> installing;
  for (auto &buf : buffers) {
    if (buf.syntax_engine == SYNTAX_ENGINE_UNKNOWN && !buf.filepath.empty() &&
        buf.line_count() > 0) {
      int line_idx = std::clamp(buf.cursor.y, 0, (int)buf.line_count() - 1);
      get_line_syntax_colors(buf, line_idx);
    }
    if (buf.syntax_engine == SYNTAX_ENGINE_TREESITTER) {
#ifdef JOT_TREESITTER
      if (!buf.ts_language_id.empty()) {
        active.insert(buf.ts_language_id);
      } else
#endif
      if (!buf.syntax_language_label.empty()) {
        active.insert(TreeSitterCatalog::normalize_language_name(
            buf.syntax_language_label));
      }
    }
  }

  std::vector<TreeSitterStatusRenderRow> installing_rows;
  for (const auto &job : tree_sitter_install_jobs) {
    if (job.running) {
      installing.insert(job.language);
      installing_rows.push_back({"", ts_display_name(job.language),
                                 job.progress.empty() ? "running"
                                                      : job.progress,
                                 theme.fg_status_warning});
    } else if (job.failed) {
      installing_rows.push_back({"", ts_display_name(job.language),
                                 job.progress.empty() ? "failed"
                                                      : job.progress,
                                 theme.fg_status_error});
    }
  }

  std::vector<TreeSitterStatusRenderRow> active_rows;
  std::vector<TreeSitterStatusRenderRow> installed_rows;
  std::vector<TreeSitterStatusRenderRow> uninstalled_rows;

  for (const auto &lang : TreeSitterCatalog::language_names()) {
    if (active.find(lang) != active.end()) {
      active_rows.push_back({"", ts_display_name(lang),
                             "active in open buffer", theme.fg_status_info});
      continue;
    }
    if (installing.find(lang) != installing.end()) {
      continue;
    }
#ifdef JOT_TREESITTER
    TreeSitterRuntimeStatus status = ts_manager_.runtime_status_for_language(lang);
    if (status.parser_loaded) {
      std::string detail = status.query_loaded ? status.query_message
                                               : status.parser_message;
      installed_rows.push_back({"", ts_display_name(lang),
                                detail.empty() ? "parser loaded" : detail,
                                theme.fg_command});
    } else {
      uninstalled_rows.push_back(
          {"", ts_display_name(lang),
           status.parser_message.empty() ? "not installed"
                                         : status.parser_message,
           theme.fg_comment});
    }
#else
    uninstalled_rows.push_back({"", ts_display_name(lang),
                                "Tree-sitter runtime not available",
                                theme.fg_comment});
#endif
  }

  std::vector<TreeSitterStatusRenderRow> rows;
  ts_add_section(rows, "Active", (int)active_rows.size());
  rows.insert(rows.end(), active_rows.begin(), active_rows.end());
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
  if (screen_w < 54) {
    w = std::max(20, screen_w - 2);
  }
  if (screen_h < 16) {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 2);
  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                            theme.fg_panel_border, theme.bg_command});
  ui_draw_panel_title(*ui, rect, " Tree-sitter", theme.fg_command,
                      theme.bg_command);

  int list_h = std::max(0, h - 4);
  int max_scroll = std::max(0, (int)rows.size() - list_h);
  tree_sitter_status_scroll =
      std::clamp(tree_sitter_status_scroll, 0, max_scroll);

  int lang_w = std::max(12, std::min(24, w / 3));
  for (int i = 0; i < list_h; i++) {
    int idx = tree_sitter_status_scroll + i;
    if (idx < 0 || idx >= (int)rows.size()) {
      break;
    }
    const auto &row = rows[idx];
    int row_y = y + 2 + i;
    if (!row.section.empty()) {
      std::string title = row.section + " (" + row.detail + ")";
      ui->draw_text(x + 1, row_y, ui_truncate_cells(title, w - 2),
                    theme.fg_comment, theme.bg_command, true);
      continue;
    }
    std::string lang = ui_truncate_cells(row.language, lang_w);
    std::string detail = ui_truncate_cells(row.detail, w - lang_w - 5);
    ui->draw_text(x + 2, row_y, lang, row.color, theme.bg_command, true);
    ui->draw_text(x + 2 + lang_w, row_y, detail, theme.fg_comment,
                  theme.bg_command);
  }

  std::string footer = "Esc close  Up/Down scroll";
  if (max_scroll > 0) {
    footer += "  " + std::to_string(tree_sitter_status_scroll + 1) + "/" +
              std::to_string(max_scroll + 1);
  }
  ui_draw_footer(*ui, rect, ui_truncate_cells(footer, w - 2),
                 theme.fg_comment, theme.bg_command);
}

void Editor::render_status_line() {
  int y = ui->get_height() - status_height;
  int w = ui->get_render_width();

  UIRect rect = {0, y, w, status_height};
  ui->fill_rect(rect, " ", theme.fg_status, theme.bg_status);

  if (w <= 0) {
    return;
  }

  const bool draw_border_rails = w >= 4;
  const int content_x = draw_border_rails ? 1 : 0;
  const int content_w = draw_border_rails ? std::max(0, w - 2) : w;
  if (draw_border_rails) {
    for (int row = 0; row < status_height; row++) {
      ui->draw_text(0, y + row, "│", theme.fg_panel_border, theme.bg_status);
      ui->draw_text(w - 1, y + row, "│", theme.fg_panel_border,
                    theme.bg_status);
    }
  }

  std::vector<StatusSegment> left_segments;
  std::vector<StatusSegment> right_segments;
  if (content_w >= 28) {
    left_segments.push_back({" 󰚩 JOT ", theme.fg_status_logo,
                             theme.bg_status_logo, true, true, 10});
  }

  FileBuffer *active_buf = nullptr;
  if (!buffers.empty() && current_buffer >= 0 &&
      current_buffer < (int)buffers.size()) {
    active_buf = &buffers[current_buffer];
  }

  std::string file_label = "Home";
  std::string file_icon = "󰋜";
  bool modified = false;
  if (!show_home_menu && active_buf) {
    file_label = status_path_basename(active_buf->filepath, "[No Name]");
    modified = active_buf->modified;
    file_icon = active_buf->filepath.empty() ? "󰈔" : "󰈙";
  }
  left_segments.push_back({" " + file_icon + " " + file_label +
                               (modified ? " ●" : "") + " ",
                           theme.fg_status_file, theme.bg_status_file, true,
                           false, 100});

  std::string cursor_label = " Ready ";
  if (!show_home_menu && active_buf) {
    cursor_label = " Ln " + std::to_string(active_buf->cursor.y + 1) +
                   ", Col " + std::to_string(active_buf->cursor.x + 1) + " ";
  }
  left_segments.push_back({cursor_label, theme.fg_status, theme.bg_status, true,
                           false, 100});

  if (!show_home_menu && active_buf && active_buf->selection.active) {
    int lines = std::abs(active_buf->selection.end.y -
                         active_buf->selection.start.y) +
                1;
    int cols = std::abs(active_buf->selection.end.x -
                        active_buf->selection.start.x);
    std::string sel = lines > 1 ? " Sel " + std::to_string(lines) + "L "
                                : " Sel " + std::to_string(cols) + "C ";
    left_segments.push_back({sel, theme.fg_status_info, theme.bg_status_info,
                             true, true, 90});
  }

  int diag_error = 0;
  int diag_warning = 0;
  int diag_info = 0;
  int diag_hint = 0;
  if (active_buf) {
    for (const auto &diag : active_buf->diagnostics) {
      if (diag.severity == 1)
        diag_error++;
      else if (diag.severity == 2)
        diag_warning++;
      else if (diag.severity == 3)
        diag_info++;
      else if (diag.severity == 4)
        diag_hint++;
    }
  } else {
    for (const auto &entry : workspace_diagnostic_severity) {
      if (entry.second == 1)
        diag_error++;
      else if (entry.second == 2)
        diag_warning++;
      else if (entry.second == 3)
        diag_info++;
      else if (entry.second == 4)
        diag_hint++;
    }
  }
  if (diag_error || diag_warning || diag_info || diag_hint) {
    std::string diag_text;
    if (diag_error)
      diag_text += "  " + std::to_string(diag_error);
    if (diag_warning)
      diag_text += "  " + std::to_string(diag_warning);
    if (diag_info)
      diag_text += "  " + std::to_string(diag_info);
    if (diag_hint)
      diag_text += "  " + std::to_string(diag_hint);
    diag_text += " ";
    int diag_fg = diag_error ? theme.fg_status_error
                             : (diag_warning ? theme.fg_status_warning
                                             : theme.fg_status_info);
    int diag_bg = diag_error ? theme.bg_status_error
                             : (diag_warning ? theme.bg_status_warning
                                             : theme.bg_status_info);
    right_segments.push_back(
        {diag_text, diag_fg, diag_bg, true, true, 80});
  }

  if (has_git_repo()) {
    std::string git = "  " + ui_truncate_cells(git_branch, 18);
    if (git_staged_count > 0) {
      git += " +" + std::to_string(git_staged_count);
    }
    if (git_unstaged_count > 0) {
      git += " ~" + std::to_string(git_unstaged_count);
    }
    if (git_untracked_count > 0) {
      git += " ?" + std::to_string(git_untracked_count);
    }
    if (git_conflict_count > 0) {
      git += " !" + std::to_string(git_conflict_count);
    }
    git += " ";
    right_segments.push_back(
        {git, theme.fg_status_info, theme.bg_status_info, true, true, 70});
  }

  if (!lsp_clients.empty()) {
    int running_clients = 0;
    std::string first_running;
    for (const auto &client : lsp_clients) {
      if (client && client->is_running()) {
        running_clients++;
        if (first_running.empty()) {
          first_running = client->describe();
        }
      }
    }
    std::string lsp_text;
    int lsp_fg = theme.fg_status_muted;
    int lsp_bg = theme.bg_status_muted;
    if (running_clients > 0) {
      lsp_text = "  " +
                 ui_truncate_cells(first_running.empty() ? "LSP"
                                                         : first_running,
                                   18);
      if (running_clients > 1) {
        lsp_text += " x" + std::to_string(running_clients);
      }
      lsp_text += " ";
      lsp_fg = theme.fg_status_info;
      lsp_bg = theme.bg_status_info;
    } else {
      lsp_text = "  LSP off ";
    }
    right_segments.push_back({lsp_text, lsp_fg, lsp_bg, false, true, 60});
  }

  if (!show_home_menu && active_buf) {
    SyntaxEngine engine = active_buf->syntax_engine;
    if (engine == SYNTAX_ENGINE_UNKNOWN && !active_buf->filepath.empty() &&
        active_buf->line_count() > 0) {
      int line_idx = std::clamp(active_buf->cursor.y, 0,
                                (int)active_buf->line_count() - 1);
      get_line_syntax_colors(*active_buf, line_idx);
      engine = active_buf->syntax_engine;
    }

    std::string syntax_text;
    int syntax_fg = theme.fg_status_muted;
    int syntax_bg = theme.bg_status_muted;
    if (engine == SYNTAX_ENGINE_TREESITTER) {
      std::string label = active_buf->syntax_language_label.empty()
                              ? "tree-sitter"
                              : active_buf->syntax_language_label;
      syntax_text = " TS " + ui_truncate_cells(label, 12) + " ";
      syntax_fg = theme.fg_status_info;
      syntax_bg = theme.bg_status_info;
    } else if (engine == SYNTAX_ENGINE_REGEX) {
      std::string label = active_buf->syntax_language_label.empty()
                              ? "regex"
                              : active_buf->syntax_language_label;
      syntax_text = " Regex " + ui_truncate_cells(label, 8) + " ";
    } else {
      syntax_text = " Syntax off ";
    }
    right_segments.push_back(
        {syntax_text, syntax_fg, syntax_bg, false, true, 55});
  }

  if (!integrated_terminals.empty()) {
    std::string term_label = "terminal";
    IntegratedTerminal *term = get_integrated_terminal();
    if (term && !term->get_label().empty()) {
      term_label = term->get_label();
    } else if ((int)integrated_terminals.size() > 1) {
      term_label = std::to_string(integrated_terminals.size()) + " terms";
    }
    std::string term_text = "  " + ui_truncate_cells(term_label, 16);
    if (term && term->is_focused()) {
      term_text += " ●";
    }
    term_text += " ";
    right_segments.push_back({term_text, theme.fg_status_info,
                              theme.bg_status_info, true, true, 50});
  }

  if (auto_save_enabled) {
    right_segments.push_back(
        {" 󰆓 " + std::to_string(auto_save_interval_ms) + "ms ",
         theme.fg_status_info, theme.bg_status_info, false, true, 40});
  }

  if (!current_theme_name.empty()) {
    right_segments.push_back({"  " +
                                  ui_truncate_cells(current_theme_name, 14) +
                                  " ",
                              theme.fg_status_muted, theme.bg_status_muted,
                              false, true, 30});
  }

  if (discord_rpc.is_connected()) {
    right_segments.push_back({" 󰙯 RPC ", theme.fg_status_muted,
                              theme.bg_status_muted, false, true, 20});
  }

  right_segments.push_back({" UTF-8 ", theme.fg_status_muted,
                            theme.bg_status_muted, false, true, 10});

  const int min_gap = content_w >= 40 ? 2 : 1;
  status_drop_optional_to_fit(right_segments, std::max(0, content_w / 2));
  int right_w = status_layout_width(right_segments);
  int left_budget =
      std::max(0, content_w - right_w - (right_w > 0 ? min_gap : 0));
  status_drop_optional_to_fit(left_segments, left_budget);

  if (status_layout_width(left_segments) > left_budget &&
      left_segments.size() > 2) {
    auto logo = std::find_if(left_segments.begin(), left_segments.end(),
                             [](const StatusSegment &segment) {
                               return segment.text.find("JOT") !=
                                      std::string::npos;
                             });
    if (logo != left_segments.end()) {
      left_segments.erase(logo);
    }
  }

  if (status_layout_width(left_segments) > left_budget &&
      left_segments.size() > 2) {
    int excess = status_layout_width(left_segments) - left_budget;
    size_t file_index = 0;
    for (size_t i = 0; i < left_segments.size(); i++) {
      if (left_segments[i].text.find(file_label) != std::string::npos) {
        file_index = i;
        break;
      }
    }
    StatusSegment &file_segment = left_segments[file_index];
    int target = std::max(4, ui_cell_count(file_segment.text) - excess);
    file_segment.text = ui_truncate_cells(file_segment.text, target);
  }

  while (status_layout_width(left_segments) > left_budget &&
         !left_segments.empty()) {
    StatusSegment &last = left_segments.back();
    int target = ui_cell_count(last.text) -
                 (status_layout_width(left_segments) - left_budget);
    if (target <= 0) {
      left_segments.pop_back();
    } else {
      last.text = ui_take_cells(last.text, target);
      break;
    }
  }

  right_w = status_layout_width(right_segments);
  int right_x = content_x + std::max(0, content_w - right_w);
  int left_w = std::max(0, right_x - (right_w > 0 ? min_gap : 0));
  status_draw_segmented_at(ui, content_x, y, left_w - content_x,
                           left_segments);
  if (right_w > 0) {
    status_draw_segmented_at(ui, right_x, y, right_w, right_segments);
  }

  // Message / context row.
  if (!message.empty()) {
    status_draw_clipped(ui, content_x, y + 1, content_w, "  › " + message,
                        theme.fg_status_message, theme.bg_status, true);
  } else {
    std::string context = "  " + status_workspace_label(root_dir);
    if (active_buf && !active_buf->filepath.empty()) {
      context += "  ·  " + active_buf->filepath;
    } else if (show_home_menu) {
      context += "  ·  Home";
    }
    status_draw_clipped(ui, content_x, y + 1, content_w,
                        ui_truncate_cells(context, std::max(0, content_w)),
                        theme.fg_status_muted, theme.bg_status);
  }
}

void Editor::render_command_palette() {
  if (!show_command_palette)
    return;

  int y = ui->get_height() - 1;
  int w = ui->get_render_width();
  UIRect rect = {0, y, w, 1};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);

  std::string text = ":" + command_palette_query;
  if ((int)text.length() > w - 1) {
    text = text.substr(text.length() - (w - 1));
  }
  ui->draw_text(0, y, text, theme.fg_command, theme.bg_command, true);

  if (!command_palette_results.empty()) {
    int max_items = std::min(8, (int)command_palette_results.size());
    int selected = std::clamp(command_palette_selected, 0,
                              (int)command_palette_results.size() - 1);
    int start_idx = std::max(0, selected - max_items + 1);
    if (start_idx + max_items > (int)command_palette_results.size()) {
      start_idx = std::max(0, (int)command_palette_results.size() - max_items);
    }

    int top_y = std::max(0, y - max_items);
    for (int row = 0; row < max_items; row++) {
      int idx = start_idx + row;
      if (idx < 0 || idx >= (int)command_palette_results.size()) {
        break;
      }
      int row_y = top_y + row;
      bool is_selected = (idx == selected);
      int fg = is_selected ? theme.fg_selection : theme.fg_command;
      int bg = is_selected ? theme.bg_selection : theme.bg_command;

      UIRect row_rect = {0, row_y, w, 1};
      ui->fill_rect(row_rect, " ", fg, bg);

      const auto &suggestion = command_palette_results[idx];
      std::string category = suggestion.category;
      if ((int)category.size() > 12) {
        category = category.substr(0, 9) + "...";
      }

      std::string prefix = is_selected ? "> " : "  ";
      int cat_w = std::min(14, std::max(8, w / 6));
      int detail_w = std::max(0, w / 3);
      int label_w = std::max(8, w - cat_w - detail_w - 4);

      std::string label = suggestion.label;
      label = ui_truncate_cells(label, label_w);
      std::string detail = suggestion.detail;
      detail = ui_truncate_cells(detail, detail_w);

      ui->draw_text(0, row_y, prefix + label, fg, bg, is_selected);
      if (w > 32) {
        ui->draw_text(std::max(0, w - cat_w - detail_w), row_y, category,
                      theme.fg_comment, bg);
        ui->draw_text(std::max(0, w - detail_w), row_y, detail,
                      theme.fg_comment, bg);
      }
    }
  } else if (!command_palette_query.empty()) {
    int row_y = std::max(0, y - 1);
    UIRect row_rect = {0, row_y, w, 1};
    ui->fill_rect(row_rect, " ", theme.fg_comment, theme.bg_command);
    ui->draw_text(0, row_y, "  No matches", theme.fg_comment,
                  theme.bg_command);
  }
}

void Editor::render_quick_pick() {
  if (!show_quick_pick) {
    return;
  }

  int screen_w = ui->get_render_width();
  int screen_h = ui->get_height();
  int w = std::min(std::max(56, screen_w - 10), 112);
  int h = std::min(std::max(12, screen_h - 8), 26);
  if (screen_w < 62) {
    w = std::max(22, screen_w - 2);
  }
  if (screen_h < 16) {
    h = std::max(8, screen_h - 2);
  }
  int x = std::max(0, (screen_w - w) / 2);
  int y = std::max(1, (screen_h - h) / 3);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                            theme.fg_panel_border, theme.bg_command});
  std::string title = quick_pick_title.empty() ? " Quick Pick"
                                               : " " + quick_pick_title;
  ui_draw_panel_title(*ui, rect, ui_truncate_cells(title, w - 2),
                      theme.fg_command, theme.bg_command);

  std::string count = std::to_string(quick_pick_items.size()) + "/" +
                      std::to_string(quick_pick_all_items.size());
  ui->draw_text(std::max(x + 1, x + w - (int)count.size() - 1), y, count,
                theme.fg_comment, theme.bg_command);

  int input_y = y + 1;
  std::string query = "> " + quick_pick_query;
  ui->draw_text(x + 1, input_y, ui_truncate_cells(query, w - 2),
                theme.fg_selection, theme.bg_selection, true);

  int list_y = y + 3;
  int list_h = std::max(0, h - 5);
  int selected =
      std::clamp(quick_pick_selected, 0,
                 std::max(0, (int)quick_pick_items.size() - 1));
  int start_idx = std::max(0, selected - list_h + 1);
  if (start_idx + list_h > (int)quick_pick_items.size()) {
    start_idx = std::max(0, (int)quick_pick_items.size() - list_h);
  }

  if (quick_pick_items.empty()) {
    std::string empty = quick_pick_query.empty()
                            ? "Type to filter or search"
                            : "No matches";
    ui->draw_text(x + 2, list_y, ui_truncate_cells(empty, w - 4),
                  theme.fg_comment, theme.bg_command);
  }

  for (int row = 0; row < list_h; row++) {
    int idx = start_idx + row;
    if (idx < 0 || idx >= (int)quick_pick_items.size()) {
      break;
    }
    const auto &item = quick_pick_items[(size_t)idx];
    bool is_selected = idx == selected;
    int fg = is_selected ? theme.fg_selection : theme.fg_command;
    int bg = is_selected ? theme.bg_selection : theme.bg_command;
    int row_y = list_y + row;
    ui->fill_rect({x + 1, row_y, std::max(1, w - 2), 1}, " ", fg, bg);

    int detail_w = w >= 72 ? std::max(16, w / 3) : 0;
    int label_w = std::max(8, w - detail_w - 5);
    std::string prefix = is_selected ? "> " : "  ";
    ui->draw_text(x + 1, row_y,
                  prefix + ui_truncate_cells(item.label, label_w), fg, bg,
                  is_selected);
    if (detail_w > 0 && !item.detail.empty()) {
      ui->draw_text(x + w - detail_w - 1, row_y,
                    ui_truncate_cells(item.detail, detail_w),
                    theme.fg_comment, bg);
    }
  }

  std::string footer = "Enter open  Esc close  Up/Down move";
  if (selected >= 0 && selected < (int)quick_pick_items.size() &&
      !quick_pick_items[(size_t)selected].preview.empty()) {
    footer = quick_pick_items[(size_t)selected].preview;
  }
  ui_draw_footer(*ui, rect, ui_truncate_cells(footer, w - 2),
                 theme.fg_comment, theme.bg_command);
}

void Editor::render_search_panel() {
  if (!show_search)
    return;

  int w = std::min(72, std::max(42, ui->get_render_width() / 2));
  int h = search_replace_visible ? 5 : 4;
  int x = ui->get_width() - w - 2;
  int y = 1 + tab_height;

  if (x < 0)
    x = 0;
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  std::string count = "0/0";
  if (search_result_index >= 0 && !search_results.empty()) {
    count = std::to_string(search_result_index + 1) + "/" +
            std::to_string(search_results.size());
  } else if (!search_query.empty()) {
    count = "0/0";
  }

  std::string chips;
  chips += search_case_sensitive ? " Aa " : " aa ";
  chips += search_whole_word ? " W " : " w ";
  if (search_regex) {
    chips += " .* ";
  }
  chips += " " + count + " ";
  ui_draw_panel_title(*ui, rect, " Find", theme.fg_command, theme.bg_command);
  ui->draw_text(std::max(x + 1, x + w - (int)chips.size() - 1), y, chips,
                theme.fg_comment, theme.bg_command);

  int label_w = 9;
  int input_w = std::max(1, w - label_w - 3);
  int find_fg = search_focus_replace ? theme.fg_command : theme.fg_selection;
  int find_bg = search_focus_replace ? theme.bg_command : theme.bg_selection;
  ui->draw_text(x + 1, y + 1, "Find", theme.fg_comment, theme.bg_command);
  ui->draw_text(x + label_w, y + 1, ui_truncate_cells(search_query, input_w), find_fg,
                find_bg, !search_focus_replace);

  if (search_replace_visible) {
    int replace_fg = search_focus_replace ? theme.fg_selection : theme.fg_command;
    int replace_bg = search_focus_replace ? theme.bg_selection : theme.bg_command;
    ui->draw_text(x + 1, y + 2, "Replace", theme.fg_comment,
                  theme.bg_command);
    ui->draw_text(x + label_w, y + 2,
                  ui_truncate_cells(search_replace_text, input_w),
                  replace_fg, replace_bg, search_focus_replace);
  }

  std::string footer = search_replace_visible
                           ? "Enter next  Up prev  Tab field  ^R one  ^R+Shift all"
                           : "Enter next  Up prev  Tab case  ^H replace  ^E regex";
  ui->draw_text(x + 1, y + h - 2, ui_truncate_cells(footer, w - 3),
                theme.fg_comment, theme.bg_command);
}

void Editor::render_context_menu() {
  if (!show_context_menu || context_menu_items.empty())
    return;

  int w = std::max(1, context_menu_w);
  int h = std::max(1, context_menu_h);
  int x = context_menu_x;
  int y = context_menu_y;

  if (x + w > ui->get_render_width())
    x = std::max(0, ui->get_render_width() - w);
  if (y + h > ui->get_height())
    y = std::max(0, ui->get_height() - h);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(context_menu_items.size());
  for (size_t i = 0; i < context_menu_items.size(); i++) {
    rows.push_back({context_menu_items[i].label,
                    (int)i == context_menu_selected,
                    context_menu_items[i].enabled});
  }
  ui_draw_selectable_rows(*ui, x + 1, y + 1, std::max(1, w - 2),
                          std::max(0, h - 2), rows,
                          {theme.fg_command, theme.bg_command,
                           theme.fg_selection, theme.bg_selection,
                           theme.fg_comment, theme.bg_command});
}

std::vector<Editor::MenuBarMenu> Editor::build_menu_bar_model() const {
  return {
      {"File",
       {{"New File", MENU_ACTION_NEW_FILE},
        {"Open File...", MENU_ACTION_OPEN_FINDER},
        {"Save", MENU_ACTION_SAVE},
        {"Save As...", MENU_ACTION_SAVE_AS},
        {"Close File", MENU_ACTION_CLOSE_FILE},
        {"Quit", MENU_ACTION_QUIT}}},
      {"Edit",
       {{"Undo", MENU_ACTION_UNDO},
        {"Redo", MENU_ACTION_REDO},
        {"Cut", MENU_ACTION_CUT},
        {"Copy", MENU_ACTION_COPY},
        {"Paste", MENU_ACTION_PASTE},
        {"Find", MENU_ACTION_COMMAND, "Toggle Search"},
        {"Format Document", MENU_ACTION_COMMAND, "Format Document"},
        {"Trim Trailing Whitespace", MENU_ACTION_COMMAND,
         "Trim Trailing Whitespace"}}},
      {"Selection",
       {{"Select All", MENU_ACTION_SELECT_ALL},
        {"Select Line", MENU_ACTION_SELECT_LINE},
        {"Duplicate Line", MENU_ACTION_DUPLICATE_LINE},
        {"Move Line Up", MENU_ACTION_MOVE_LINE_UP},
        {"Move Line Down", MENU_ACTION_MOVE_LINE_DOWN},
        {"Toggle Comment", MENU_ACTION_TOGGLE_COMMENT}}},
      {"View",
       {{"Command Palette", MENU_ACTION_COMMAND_PALETTE},
        {"Explorer", MENU_ACTION_TOGGLE_SIDEBAR},
        {"Toggle Minimap", MENU_ACTION_TOGGLE_MINIMAP},
        {"Color Theme", MENU_ACTION_THEME},
        {"Home", MENU_ACTION_HOME}}},
      {"Go",
       {{"Go to Line...", MENU_ACTION_COMMAND, ":line "},
        {"Go to Definition", MENU_ACTION_LSP_DEFINITION},
        {"Back", MENU_ACTION_LSP_BACK}}},
      {"Debug",
       {{"Start Debugging", MENU_ACTION_COMMAND, ":debug "},
        {"Continue", MENU_ACTION_DEBUG_CONTINUE},
        {"Pause", MENU_ACTION_DEBUG_PAUSE},
        {"Step Over", MENU_ACTION_DEBUG_STEP_OVER},
        {"Step Into", MENU_ACTION_DEBUG_STEP_IN},
        {"Step Out", MENU_ACTION_DEBUG_STEP_OUT},
        {"Stop", MENU_ACTION_DEBUG_STOP},
        {"Debug Panel", MENU_ACTION_TOGGLE_DEBUG_PANEL}}},
      {"Terminal",
       {{"Toggle Terminal", MENU_ACTION_TOGGLE_TERMINAL},
        {"New Terminal", MENU_ACTION_NEW_TERMINAL},
        {"Run Task...", MENU_ACTION_TASKS},
        {"Rerun Last Task", MENU_ACTION_RERUN_TASK}}},
      {"Help",
       {{"Help", MENU_ACTION_HELP},
        {"LSP Status", MENU_ACTION_COMMAND, ":lspstatus"},
        {"Tree-sitter Status", MENU_ACTION_COMMAND, ":tsstatus"},
        {"Git Status", MENU_ACTION_COMMAND, ":gitstatus"}}},
  };
}

void Editor::render_menu_bar() {
  int w = ui ? ui->get_render_width() : 0;
  if (w <= 0) {
    return;
  }

  UIRect row = {0, 0, w, 1};
  ui->fill_rect(row, " ", theme.fg_status, theme.bg_status);
  menu_bar_segments.clear();

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  int x = 0;
  for (int i = 0; i < (int)menus.size(); i++) {
    std::string label = " " + menus[i].label + " ";
    int label_w = (int)label.size();
    if (x + label_w > w) {
      break;
    }
    bool active = show_menu_bar_dropdown && i == menu_bar_active;
    int fg = active ? theme.fg_selection : theme.fg_status;
    int bg = active ? theme.bg_selection : theme.bg_status;
    ui->draw_text(x, 0, label, fg, bg, active);
    menu_bar_segments.push_back({i, x, x + label_w});
    x += label_w;
  }
}

void Editor::render_menu_dropdown() {
  if (!show_menu_bar_dropdown || !ui) {
    return;
  }

  std::vector<MenuBarMenu> menus = build_menu_bar_model();
  if (menu_bar_active < 0 || menu_bar_active >= (int)menus.size()) {
    return;
  }

  const auto &menu = menus[menu_bar_active];
  if (menu.items.empty()) {
    return;
  }

  int label_x = 0;
  for (const auto &segment : menu_bar_segments) {
    if (segment.menu_index == menu_bar_active) {
      label_x = segment.x;
      break;
    }
  }

  int max_label = 0;
  for (const auto &item : menu.items) {
    max_label = std::max(max_label, (int)item.label.size());
  }

  int w = std::max(18, max_label + 4);
  int h = (int)menu.items.size() + 2;
  int x = std::clamp(label_x, 0, std::max(0, ui->get_render_width() - w));
  int y = 1;
  int max_h = std::max(1, ui->get_height() - y - status_height);
  h = std::min(h, max_h);

  UIRect rect = {x, y, w, h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  std::vector<UISelectableRow> rows;
  rows.reserve(menu.items.size());
  for (int i = 0; i < (int)menu.items.size(); i++) {
    rows.push_back({menu.items[(size_t)i].label, i == menu_bar_selected,
                    menu.items[(size_t)i].enabled});
  }
  ui_draw_selectable_rows(*ui, x + 1, y + 1, std::max(1, w - 2),
                          std::max(0, h - 2), rows,
                          {theme.fg_command, theme.bg_command,
                           theme.fg_selection, theme.bg_selection,
                           theme.fg_comment, theme.bg_command});
}

void Editor::render_save_prompt() {
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  int x = w / 2 - prompt.length() / 2;
  int y = h / 2;

  UIRect rect = {x - 2, y - 1, (int)prompt.length() + 4, 3};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, theme.bg_command);

  // Input area for filename if needed?
  if (save_prompt_input.length() > 0 || get_buffer().filepath.empty()) {
    std::string disp = "Filename: " + save_prompt_input;
    ui->draw_text(x, y + 1, disp, theme.fg_command, theme.bg_command);
  }
}

void Editor::render_quit_prompt() {
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Unsaved changes! Quit anyway? (y/n)";
  int x = w / 2 - prompt.length() / 2;
  int y = h / 2;

  UIRect rect = {x - 2, y - 1, (int)prompt.length() + 4, 3};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  ui->draw_text(x, y, prompt, theme.fg_command, theme.bg_command);
}

void Editor::render_popup() {
  if (!popup.visible)
    return;

  UIRect rect = {popup.x, popup.y, popup.w, popup.h};
  ui_draw_panel(*ui, rect, {theme.fg_command, theme.bg_command,
                           theme.fg_panel_border, theme.bg_command});

  // Split text by newlines
  std::vector<std::string> lines;
  std::istringstream stream(popup.text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }

  for (int i = 0; i < (int)lines.size(); i++) {
    if (i >= popup.h - 2)
      break;
    ui->draw_text(popup.x + 1, popup.y + 1 + i,
                  ui_truncate_cells(lines[i], popup.w - 2), theme.fg_command,
                  theme.bg_command);
  }
}

void Editor::render_tabs() {
  // Global shared tabs are intentionally disabled.
  // Each pane renders its own local tab header.
}

// ---------------------------------------------------------------------------
// Easter egg: Konami code (↑↑↓↓←→←→) rainbow popup
// ---------------------------------------------------------------------------
