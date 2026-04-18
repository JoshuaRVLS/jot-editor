#include "editor.h"
#include "python_api.h"
#include <cstdio>
#include <sstream>

void Editor::render_status_line() {
  int y = ui->get_height() - status_height;
  int w = ui->get_width();

  UIRect rect = {0, y, w, status_height};
  ui->fill_rect(rect, " ", theme.fg_status, theme.bg_status);

  std::string l_text = "  ";
  if (!buffers.empty() && current_buffer < (int)buffers.size()) {
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

  // Right side — encoding only
  std::string enc_str = "  UTF-8  ";
  int r_len = (int)enc_str.length();

  if (w > (int)l_text.length() + r_len + 2) {
    ui->draw_text(w - (int)enc_str.length() - 2, y, enc_str,
                  theme.fg_status, theme.bg_status);
  }

  // Message area (status line 2)
  if (!message.empty()) {
    ui->draw_text(0, y + 1, message, theme.fg_default, theme.bg_status);
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
}

void Editor::render_input_prompt() {
  if (!input_prompt_visible)
    return;

  int w = 40;
  int h = 3;
  int x = ui->get_width() / 2 - w / 2;
  int y = ui->get_height() / 4;

  UIRect rect = {x, y, w, h};
  UIRect shadow = {x + 1, y + 1, w, h};
  ui->draw_rect(shadow, 8, 0);

  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  ui->draw_text(x + 1, y + 1, input_prompt_message + input_prompt_buffer,
                theme.fg_command, theme.bg_command);
}

void Editor::render_search_panel() {
  if (!show_search)
    return;

  int w = 40;
  int h = 3;
  int x = ui->get_width() - w - 2;
  int y = 1 + tab_height;

  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  std::string mode = search_case_sensitive ? "Aa" : "aa";
  std::string q = "Find[" + mode + "]: " + search_query;
  if ((int)q.length() > w - 12) {
    q = q.substr(0, w - 14) + "..";
  }
  ui->draw_text(x + 1, y + 1, q, theme.fg_command, theme.bg_command);

  if (search_result_index >= 0) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%lu", search_result_index + 1,
             search_results.size());
    ui->draw_text(x + w - 10, y + 1, buf, theme.fg_comment, theme.bg_command);
  }
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
  UIRect shadow = {x + 1, y + 1, w, h};
  ui->draw_rect(shadow, 8, 0);

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
  UIRect shadow = {rect.x + 1, rect.y + 1, rect.w, rect.h};
  ui->draw_rect(shadow, 8, 0);

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
  // Draw shadow
  UIRect shadow = {rect.x + 1, rect.y + 1, rect.w, rect.h};
  ui->draw_rect(shadow, 8, 0);

  ui->fill_rect(rect, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(rect, theme.fg_panel_border, theme.bg_command);

  ui->draw_text(x, y, prompt, theme.fg_command, theme.bg_command);
}

void Editor::render_popup() {
  if (!popup.visible)
    return;

  UIRect rect = {popup.x, popup.y, popup.w, popup.h};
  UIRect shadow = {rect.x + 1, rect.y + 1, rect.w, rect.h};
  ui->draw_rect(shadow, 8, 0);

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
  int x = 0;
  int y = 0;
  int w = ui->get_width();

  // Background bar
  if (show_sidebar)
    x += std::min(sidebar_width, std::max(0, ui->get_width() - 20));

  if (x >= w)
    return;

  UIRect bar = {x, y, w - x, tab_height};
  ui->fill_rect(bar, " ", theme.fg_status, theme.bg_status);

  // Render open buffers as tabs
  int tab_x = x;
  for (int i = 0; i < (int)buffers.size(); i++) {
    std::string name = get_filename(buffers[i].filepath);
    if (name.empty())
      name = "[No Name]";
    if (buffers[i].modified)
      name += "+";

    std::string disp = " " + name + " ";
    int bg = theme.bg_status; // Default background (Black/Dark)
    int fg = 8;               // Grey for inactive tabs

    if (i == current_buffer) {
      bg = 4; // Blue background for active tab
      fg = 7; // White text
    }

    ui->draw_text(tab_x, y, disp, fg, bg);
    int close_x = tab_x + (int)disp.length();
    int close_fg = buffers.size() > 1 ? 1 : fg;
    ui->draw_text(close_x, y, "x", close_fg, bg);
    // Vertical separator
    ui->draw_text(close_x + 1, y, "|", theme.fg_panel_border,
                  theme.bg_status);

    tab_x += (int)disp.length() + 2;
    if (tab_x >= w)
      break;
  }
}

// ---------------------------------------------------------------------------
// Easter egg: Konami code (↑↑↓↓←→←→) rainbow popup
// ---------------------------------------------------------------------------
