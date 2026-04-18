#include "editor.h"
#include <algorithm>

IntegratedTerminal *Editor::get_integrated_terminal(int index) {
  if (integrated_terminals.empty()) {
    return nullptr;
  }

  int resolved = index;
  if (resolved < 0) {
    resolved = current_integrated_terminal;
  }
  if (resolved < 0 || resolved >= (int)integrated_terminals.size()) {
    return nullptr;
  }
  return integrated_terminals[resolved].get();
}

void Editor::activate_integrated_terminal(int index, bool focus) {
  if (index < 0 || index >= (int)integrated_terminals.size()) {
    return;
  }

  current_integrated_terminal = index;
  for (int i = 0; i < (int)integrated_terminals.size(); i++) {
    integrated_terminals[i]->set_focused(focus && i == index);
  }
}

void Editor::create_integrated_terminal() {
  auto term = std::make_unique<IntegratedTerminal>();
  if (!term->open_shell()) {
    set_message("Failed to open integrated terminal");
    return;
  }

  for (auto &existing : integrated_terminals) {
    existing->set_focused(false);
  }

  integrated_terminals.push_back(std::move(term));
  current_integrated_terminal = (int)integrated_terminals.size() - 1;
  show_integrated_terminal = true;
  activate_integrated_terminal(current_integrated_terminal, true);
  set_message("Opened terminal " +
              std::to_string(current_integrated_terminal + 1));
  needs_redraw = true;
}

void Editor::close_integrated_terminal(int index) {
  if (index < 0 || index >= (int)integrated_terminals.size()) {
    return;
  }

  integrated_terminals[index]->close_shell();
  integrated_terminals.erase(integrated_terminals.begin() + index);

  if (integrated_terminals.empty()) {
    current_integrated_terminal = -1;
    show_integrated_terminal = false;
    set_message("Closed terminal");
    needs_redraw = true;
    return;
  }

  if (current_integrated_terminal == index) {
    current_integrated_terminal =
        std::min(index, (int)integrated_terminals.size() - 1);
  } else if (index < current_integrated_terminal) {
    current_integrated_terminal--;
  }

  activate_integrated_terminal(current_integrated_terminal,
                               show_integrated_terminal);
  set_message("Closed terminal");
  needs_redraw = true;
}

void Editor::toggle_integrated_terminal() {
  if (integrated_terminals.empty()) {
    create_integrated_terminal();
    return;
  }

  IntegratedTerminal *term = get_integrated_terminal();
  if (!term) {
    current_integrated_terminal = 0;
    term = get_integrated_terminal();
  }
  if (!term) {
    create_integrated_terminal();
    return;
  }

  if (!show_integrated_terminal) {
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    set_message("Integrated terminal opened");
    needs_redraw = true;
    return;
  }

  if (term->is_focused()) {
    activate_integrated_terminal(current_integrated_terminal, false);
    show_integrated_terminal = false;
    set_message("Integrated terminal hidden");
  } else {
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    set_message("Integrated terminal focused");
  }
  needs_redraw = true;
}

void Editor::handle_integrated_terminal_input(int ch, bool is_ctrl,
                                              bool is_shift, bool is_alt) {
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term || !term->is_active()) {
    return;
  }

  if (is_ctrl && is_shift && (ch == 't' || ch == 'T')) {
    create_integrated_terminal();
    return;
  }

  if (ch == 27) {
    activate_integrated_terminal(current_integrated_terminal, false);
    set_message("Terminal focus off (Ctrl+` to reopen)");
    needs_redraw = true;
    return;
  }

  if (term->send_key(ch, is_ctrl, is_shift, is_alt)) {
    needs_redraw = true;
  }
}

bool Editor::handle_integrated_terminal_mouse(int x, int y) {
  if (!show_integrated_terminal || integrated_terminals.empty()) {
    return false;
  }

  int panel_h = std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
  int panel_y = std::max(tab_height, ui->get_height() - status_height - panel_h);
  int panel_w = ui->get_width();
  int tab_y = panel_y + 1;

  if (x < 0 || x >= panel_w || y < panel_y || y >= panel_y + panel_h) {
    return false;
  }

  if (y == tab_y) {
    int tab_x = 1;
    for (int i = 0; i < (int)integrated_terminals.size(); i++) {
      std::string label = " term " + std::to_string(i + 1) + " ";
      int close_x = tab_x + (int)label.size();
      int tab_w = (int)label.size() + 2;

      if (x >= tab_x && x < tab_x + tab_w) {
        if ((int)integrated_terminals.size() > 1 && x == close_x) {
          close_integrated_terminal(i);
        } else {
          show_integrated_terminal = true;
          activate_integrated_terminal(i, true);
          set_message("Focused terminal " + std::to_string(i + 1));
          needs_redraw = true;
        }
        return true;
      }

      tab_x += tab_w;
      if (tab_x >= panel_w - 4) {
        break;
      }
    }

    std::string plus_tab = " + ";
    if (x >= tab_x && x < tab_x + (int)plus_tab.size()) {
      create_integrated_terminal();
      return true;
    }

    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    needs_redraw = true;
    return true;
  }

  show_integrated_terminal = true;
  activate_integrated_terminal(current_integrated_terminal, true);
  needs_redraw = true;
  return true;
}

void Editor::place_integrated_terminal_cursor() {
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term || !term->is_active() ||
      !term->is_focused()) {
    return;
  }

  int panel_h = std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
  int panel_y = std::max(tab_height, ui->get_height() - status_height - panel_h);
  int panel_w = ui->get_width();
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(1, panel_h - 3);

  auto lines = term->get_recent_lines(content_h);
  int cursor_line_index = std::max(0, (int)lines.size() - 1);
  int cursor_y = panel_y + 2 + std::min(cursor_line_index, content_h - 1);

  const std::string &line = term->get_current_line();
  size_t display_start = 0;
  if ((int)line.size() > content_w) {
    display_start = line.size() - (size_t)content_w;
  }

  size_t cursor_column = term->get_cursor_column();
  if (cursor_column < display_start) {
    cursor_column = display_start;
  }
  int cursor_x = 1 + (int)std::min((size_t)(content_w - 1),
                                   cursor_column - display_start);

  ui->set_cursor(cursor_x, cursor_y);
}

void Editor::render_integrated_terminal() {
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term || !term->is_active()) {
    return;
  }

  int panel_h = std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
  int panel_y = std::max(tab_height, ui->get_height() - status_height - panel_h);
  int panel_w = ui->get_width();
  UIRect panel = {0, panel_y, panel_w, panel_h};

  ui->fill_rect(panel, " ", theme.fg_command, theme.bg_command);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_command);

  int tab_y = panel_y + 1;
  int tab_x = 1;
  for (int i = 0; i < (int)integrated_terminals.size(); i++) {
    std::string label = " term " + std::to_string(i + 1) + " ";
    bool active = (i == current_integrated_terminal);
    bool focused = active && integrated_terminals[i]->is_focused();
    int fg = active ? theme.fg_default : theme.fg_status;
    int bg = focused ? theme.bg_selection
                     : (active ? theme.bg_status : theme.bg_command);

    if (tab_x + (int)label.size() + 2 >= panel_w) {
      break;
    }

    ui->draw_text(tab_x, tab_y, label, fg, bg, active);
    int close_x = tab_x + (int)label.size();
    int close_fg = ((int)integrated_terminals.size() > 1) ? 1 : fg;
    ui->draw_text(close_x, tab_y, "x", close_fg, bg);
    ui->draw_text(close_x + 1, tab_y, "|", theme.fg_panel_border,
                  theme.bg_command);
    tab_x += (int)label.size() + 2;
  }

  if (tab_x + 3 < panel_w) {
    ui->draw_text(tab_x, tab_y, " + ", theme.fg_command, theme.bg_status,
                  true);
  }

  int content_h = std::max(1, panel_h - 3);
  auto lines = term->get_recent_lines(content_h);
  int start_y = panel_y + 2;
  int start = std::max(0, (int)lines.size() - content_h);
  for (int i = 0; i < content_h; i++) {
    int idx = start + i;
    if (idx >= (int)lines.size()) {
      break;
    }
    std::string line = lines[idx];
    if ((int)line.size() > panel_w - 2) {
      line = line.substr(std::max(0, (int)line.size() - (panel_w - 2)));
    }
    ui->draw_text(1, start_y + i, line, theme.fg_default, theme.bg_command);
  }
}
