#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cstdio>
#include <sstream>

void Editor::render_status_line() {
  int y = ui->get_height() - status_height;
  int w = ui->get_width();

  UIRect rect = {0, y, w, status_height};
  ui->fill_rect(rect, " ", theme.fg_status, theme.bg_status);

  std::string l_text = "  ";
  if (show_home_menu) {
    l_text += "󰚩 Home  •  Enter: Open  •  1-9: Quick Recent  •  Esc: Hide";
  } else if (!buffers.empty() && current_buffer < (int)buffers.size()) {
    std::string name = buffers[current_buffer].filepath;
    if (name.empty())
      name = "[No Name]";

    // Add modified marker
    if (buffers[current_buffer].modified) {
      name += " [+]";
    }

    l_text += name;

    // Add cursor pos
    auto &buf = buffers[current_buffer];
    l_text += "  Ln " + std::to_string(buf.cursor.y + 1) + ", Col " +
              std::to_string(buf.cursor.x + 1);
  }

  ui->draw_text(0, y, l_text, theme.fg_status, theme.bg_status, true);

  // Right side — encoding + quick status flags
  std::string enc_str = "  UTF-8";
  if (has_git_repo()) {
    enc_str += "  git:" + git_branch;
    if (git_dirty_count > 0) {
      enc_str += " *" + std::to_string(git_dirty_count);
    }
  }
  if (auto_save_enabled) {
    enc_str += "  AS";
  }
  enc_str += "  ";
  int r_len = (int)enc_str.length();

  if (w > (int)l_text.length() + r_len + 2) {
    ui->draw_text(w - (int)enc_str.length() - 2, y, enc_str,
                  theme.fg_status, theme.bg_status);
  }

  // Message area (status line 2)
  if (!message.empty()) {
    ui->draw_text(0, y + 1, message, theme.fg_status_message, theme.bg_status);
  }
}

void Editor::render_command_palette() {
  if (!show_command_palette)
    return;

  int y = ui->get_height() - 1;
  int w = ui->get_width();
  UIRect rect = {0, y, w, 1};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);

  std::string text = ":" + command_palette_query;
  if ((int)text.length() > w - 1) {
    text = text.substr(text.length() - (w - 1));
  }
  ui->draw_text(0, y, text, theme.fg_command, theme.bg_command, true);

  if (!command_palette_results.empty()) {
    int max_items = std::min(6, (int)command_palette_results.size());
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

      std::string item = command_palette_results[idx];
      if ((int)item.size() > w - 3) {
        item = item.substr(0, std::max(0, w - 6)) + "...";
      }

      std::string prefix = is_selected ? "> " : "  ";
      ui->draw_text(0, row_y, prefix + item, fg, bg, is_selected);
    }
  }
}

void Editor::render_input_prompt() {
  if (!input_prompt_visible)
    return;

  int w = 40;
  int h = 3;
  int x = ui->get_width() / 2 - w / 2;
  int y = ui->get_height() / 4;

  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  ui->draw_text(x + 1, y + 1, input_prompt_message + input_prompt_buffer,
                theme.fg_command, theme.bg_command);
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
  if (!show_context_menu)
    return;

  std::vector<std::string> options = {"Copy", "Paste", "Cut", "Close Buffer"};

  int w = 20;
  int h = options.size() + 2;
  int x = context_menu_x;
  int y = context_menu_y;

  if (x + w > ui->get_width())
    x = ui->get_width() - w;
  if (y + h > ui->get_height())
    y = ui->get_height() - h;

  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  for (size_t i = 0; i < options.size(); i++) {
    int fg = theme.fg_command;
    int bg = theme.bg_command;
    if ((int)i == context_menu_selected) {
      bg = theme.bg_selection;
    }
    ui->draw_text(x + 1, y + 1 + i, options[i], fg, bg);
  }
}

void Editor::render_save_prompt() {
  int h = ui->get_height();
  int w = ui->get_width();

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
  int w = ui->get_width();

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
