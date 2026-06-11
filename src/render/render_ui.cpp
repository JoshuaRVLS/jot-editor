#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
int status_utf8_cell_len(const std::string &text, size_t i) {
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

int status_cell_count(const std::string &text) {
  int cells = 0;
  size_t i = 0;
  while (i < text.size()) {
    int len = status_utf8_cell_len(text, i);
    if (len <= 0 || i + (size_t)len > text.size()) {
      i++;
    } else {
      i += (size_t)len;
    }
    cells++;
  }
  return cells;
}

std::string status_take_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";

  std::string out;
  int cells = 0;
  size_t i = 0;
  while (i < text.size() && cells < max_cells) {
    int len = status_utf8_cell_len(text, i);
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

std::string status_truncate_cells(const std::string &text, int max_cells) {
  if (max_cells <= 0)
    return "";
  if (status_cell_count(text) <= max_cells)
    return text;
  if (max_cells <= 2)
    return status_take_cells(text, max_cells);
  return status_take_cells(text, max_cells - 2) + "..";
}

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

int status_layout_width(const std::vector<StatusSegment> &segments) {
  if (segments.empty())
    return 0;
  int width = 0;
  for (const auto &segment : segments) {
    width += status_cell_count(segment.text);
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
    std::string text = status_take_cells(segment.text, remaining);
    ui->draw_text(pos, y, text, segment.fg, segment.bg, segment.bold);
    pos += status_cell_count(text);

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
  ui->draw_text(x, y, status_take_cells(text, w), fg, bg, bold);
}
} // namespace

void Editor::render_status_line() {
  int y = ui->get_height() - status_height;
  int w = ui->get_render_width();

  UIRect rect = {0, y, w, status_height};
  ui->fill_rect(rect, " ", theme.fg_status, theme.bg_status);

  if (w <= 0) {
    return;
  }

  std::vector<StatusSegment> left_segments;
  std::vector<StatusSegment> right_segments;
  if (w >= 28) {
    left_segments.push_back({" 󰚩 JOT ", theme.fg_status_logo,
                             theme.bg_status_logo, true, true, 10});
  }

  FileBuffer *active_buf = nullptr;
  if (!buffers.empty() && current_buffer >= 0 &&
      current_buffer < (int)buffers.size()) {
    active_buf = &buffers[current_buffer];
  }

  std::string mode_label = "NORMAL";
  if (mode == MODE_INSERT) {
    mode_label = "INSERT";
  } else if (mode == MODE_VISUAL) {
    mode_label = visual_line_mode ? "V-LINE" : "VISUAL";
  }
  left_segments.push_back({" " + mode_label + " ", theme.fg_status_info,
                           theme.bg_status_info, true, true, 20});

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
    std::string git = "  " + status_truncate_cells(git_branch, 18);
    if (git_dirty_count > 0) {
      git += " +" + std::to_string(git_dirty_count);
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
                 status_truncate_cells(first_running.empty() ? "LSP"
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

  if (!integrated_terminals.empty()) {
    std::string term_label = "terminal";
    IntegratedTerminal *term = get_integrated_terminal();
    if (term && !term->get_label().empty()) {
      term_label = term->get_label();
    } else if ((int)integrated_terminals.size() > 1) {
      term_label = std::to_string(integrated_terminals.size()) + " terms";
    }
    std::string term_text = "  " + status_truncate_cells(term_label, 16);
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
                                  status_truncate_cells(current_theme_name, 14) +
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

  const int min_gap = w >= 40 ? 2 : 1;
  status_drop_optional_to_fit(right_segments, std::max(0, w / 2));
  int right_w = status_layout_width(right_segments);
  int left_budget = std::max(0, w - right_w - (right_w > 0 ? min_gap : 0));
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
    int target = std::max(4, status_cell_count(file_segment.text) - excess);
    file_segment.text = status_truncate_cells(file_segment.text, target);
  }

  while (status_layout_width(left_segments) > left_budget &&
         !left_segments.empty()) {
    StatusSegment &last = left_segments.back();
    int target = status_cell_count(last.text) -
                 (status_layout_width(left_segments) - left_budget);
    if (target <= 0) {
      left_segments.pop_back();
    } else {
      last.text = status_take_cells(last.text, target);
      break;
    }
  }

  right_w = status_layout_width(right_segments);
  int right_x = std::max(0, w - right_w);
  int left_w = std::max(0, right_x - (right_w > 0 ? min_gap : 0));
  status_draw_segmented_at(ui, 0, y, left_w, left_segments);
  if (right_w > 0) {
    status_draw_segmented_at(ui, right_x, y, right_w, right_segments);
  }

  // Message / context row.
  if (!message.empty()) {
    status_draw_clipped(ui, 0, y + 1, w, "  › " + message,
                        theme.fg_status_message, theme.bg_status, true);
  } else {
    std::string context = "  " + status_workspace_label(root_dir);
    if (active_buf && !active_buf->filepath.empty()) {
      context += "  ·  " + active_buf->filepath;
    } else if (show_home_menu) {
      context += "  ·  Home";
    }
    status_draw_clipped(ui, 0, y + 1, w,
                        status_truncate_cells(context, std::max(0, w)),
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
      if ((int)label.size() > label_w) {
        label = label.substr(0, std::max(0, label_w - 3)) + "...";
      }
      std::string detail = suggestion.detail;
      if ((int)detail.size() > detail_w) {
        detail = detail.substr(0, std::max(0, detail_w - 3)) + "...";
      }

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

void Editor::render_search_panel() {
  if (!show_search)
    return;

  int w = 52;
  int h = 4;
  int x = ui->get_width() - w - 2;
  int y = 1 + tab_height;

  if (x < 0)
    x = 0;
  if (x + w > ui->get_width())
    w = std::max(20, ui->get_width() - x);

  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  std::string mode = std::string(search_case_sensitive ? "Aa" : "aa") +
                     (search_whole_word ? ",W" : ",w");
  std::string q = "Find[" + mode + "]: " + search_query;
  if ((int)q.length() > w - 4) {
    q = q.substr(0, std::max(0, w - 7)) + "...";
  }
  ui->draw_text(x + 1, y + 1, q, theme.fg_command, theme.bg_command);

  if (search_result_index >= 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%lu", search_result_index + 1,
             search_results.size());
    ui->draw_text(x + w - 10, y + 1, buf, theme.fg_comment, theme.bg_command);
  } else if (!search_query.empty() && search_results.empty()) {
    ui->draw_text(x + w - 12, y + 1, "0/0", theme.fg_comment,
                  theme.bg_command);
  }

  std::string hint = "Enter/Down:Next  Up:Prev  Tab:Case  Ctrl+W:Word  Esc:Close";
  if ((int)hint.size() > w - 3) {
    hint = hint.substr(0, std::max(0, w - 6)) + "...";
  }
  ui->draw_text(x + 1, y + 2, hint, theme.fg_comment, theme.bg_command);
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
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  for (size_t i = 0; i < context_menu_items.size(); i++) {
    const auto &item = context_menu_items[i];
    int fg = item.enabled ? theme.fg_command : theme.fg_comment;
    int bg = theme.bg_command;
    if ((int)i == context_menu_selected) {
      bg = item.enabled ? theme.bg_selection : theme.bg_command;
    }
    UIRect row_rect = {x + 1, y + 1 + (int)i, std::max(1, w - 2), 1};
    ui->fill_rect(row_rect, " ", fg, bg);
    std::string label = item.label;
    if ((int)label.size() > w - 3) {
      label = label.substr(0, std::max(0, w - 5)) + "..";
    }
    ui->draw_text(x + 2, y + 1 + (int)i, label, fg, bg,
                  item.enabled && (int)i == context_menu_selected);
  }
}

void Editor::render_save_prompt() {
  int h = ui->get_height();
  int w = ui->get_render_width();

  std::string prompt = "Save As: type filename, Enter=save, Esc=cancel";
  int x = w / 2 - prompt.length() / 2;
  int y = h / 2;

  UIRect rect = {x - 2, y - 1, (int)prompt.length() + 4, 3};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

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
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  ui->draw_text(x, y, prompt, theme.fg_command, theme.bg_command);
}

void Editor::render_popup() {
  if (!popup.visible)
    return;

  UIRect rect = {popup.x, popup.y, popup.w, popup.h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

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
    ui->draw_text(popup.x + 1, popup.y + 1 + i, lines[i], theme.fg_command,
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
