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
  create_integrated_terminal("");
}

void Editor::create_integrated_terminal(const std::string &label,
                                        const std::string &cwd) {
  show_home_menu = false;
  auto term = std::make_unique<IntegratedTerminal>();
  term->set_label(label);
  if (!term->open_shell(cwd)) {
#ifdef _WIN32
    set_message("Integrated terminal: ConPTY backend not implemented yet");
#else
    set_message("Failed to open integrated terminal: check $SHELL or PTY support");
#endif
    return;
  }

  for (auto &existing : integrated_terminals) {
    existing->set_focused(false);
  }

  integrated_terminals.push_back(std::move(term));
  current_integrated_terminal = (int)integrated_terminals.size() - 1;
  watch_integrated_terminal_fd(get_integrated_terminal(current_integrated_terminal));
  show_integrated_terminal = true;
  activate_integrated_terminal(current_integrated_terminal, true);
  if (auto *active = get_integrated_terminal(current_integrated_terminal)) {
    active->poll_output();
  }
  if (label.empty()) {
    set_message("Opened terminal " +
                std::to_string(current_integrated_terminal + 1));
  } else {
    set_message("Opened " + label);
  }
  needs_redraw = true;
}

void Editor::close_integrated_terminal(int index) {
  if (index < 0 || index >= (int)integrated_terminals.size()) {
    return;
  }

  unwatch_integrated_terminal_fd(integrated_terminals[index].get());
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
  show_home_menu = false;
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

  if (!term->is_active()) {
    if (!term->open_shell()) {
#ifdef _WIN32
      set_message("Integrated terminal: ConPTY backend not implemented yet");
#else
      set_message("Failed to restart terminal: check $SHELL or PTY support");
#endif
      needs_redraw = true;
      return;
    }
    watch_integrated_terminal_fd(term);
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    term->poll_output();
    set_message("Integrated terminal restarted");
    needs_redraw = true;
    return;
  }

  if (!show_integrated_terminal) {
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    term->poll_output();
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
    term->poll_output();
    set_message("Integrated terminal focused");
  }
  needs_redraw = true;
}

void Editor::handle_integrated_terminal_input(int ch, bool is_ctrl,
                                              bool is_shift, bool is_alt) {
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term) {
    return;
  }

  if (!term->is_active()) {
    if (!term->open_shell()) {
#ifdef _WIN32
      set_message("Integrated terminal: ConPTY backend not implemented yet");
#else
      set_message("Failed to restart terminal: check $SHELL or PTY support");
#endif
      needs_redraw = true;
      return;
    }
    watch_integrated_terminal_fd(term);
    set_message("Integrated terminal restarted");
    needs_redraw = true;
  }

  if (is_ctrl && is_shift && (ch == 't' || ch == 'T')) {
    create_integrated_terminal();
    return;
  }

  if (ch == 27) {
    activate_integrated_terminal(current_integrated_terminal, false);
    set_message("Terminal focus off");
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
  int panel_w = ui->get_render_width();
  int tab_y = panel_y + 1;

  if (x < 0 || x >= panel_w || y < panel_y || y >= panel_y + panel_h) {
    return false;
  }

  if (y == tab_y || y == panel_y) {
    int tab_x = 1;
    for (int i = 0; i < (int)integrated_terminals.size(); i++) {
      std::string base_label = integrated_terminals[i]->get_label().empty()
                                   ? "term " + std::to_string(i + 1)
                                   : integrated_terminals[i]->get_label();
      std::string label = " " + base_label + " ";
      int close_x = tab_x + (int)label.size();
      int tab_w = (int)label.size() + 2;

      if (x >= tab_x && x < tab_x + tab_w) {
        if ((int)integrated_terminals.size() > 1 && x == close_x) {
          close_integrated_terminal(i);
        } else {
          show_integrated_terminal = true;
          activate_integrated_terminal(i, true);
          integrated_terminals[i]->poll_output();
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
  IntegratedTerminal *term = get_integrated_terminal();
  if (term && !term->is_active()) {
    if (term->open_shell()) {
      watch_integrated_terminal_fd(term);
      set_message("Integrated terminal restarted");
    } else {
#ifdef _WIN32
      set_message("Integrated terminal: ConPTY backend not implemented yet");
#else
      set_message("Failed to restart terminal: check $SHELL or PTY support");
#endif
    }
  }
  if (term) {
    term->poll_output();
  }
  needs_redraw = true;
  return true;
}

void Editor::watch_integrated_terminal_fd(IntegratedTerminal *term) {
#ifdef _WIN32
  (void)term;
  return;
#else
  if (!term || term->get_master_fd() < 0 ||
      event_loop_.is_watching_fd(term->get_master_fd())) {
    return;
  }

  int fd = term->get_master_fd();
  event_loop_.watch_fd(fd, true, false, [this, fd] {
    IntegratedTerminal *matched = nullptr;
    for (auto &term : integrated_terminals) {
      if (term && term->get_master_fd() == fd) {
        matched = term.get();
        break;
      }
    }
    if (!matched) {
      event_loop_.unwatch_fd(fd);
      return;
    }
    if (matched->poll_output() && show_integrated_terminal) {
      needs_redraw = true;
    }
    if (matched->get_master_fd() != fd) {
      event_loop_.unwatch_fd(fd);
    }
  });
#endif
}

void Editor::unwatch_integrated_terminal_fd(IntegratedTerminal *term) {
#ifdef _WIN32
  (void)term;
  return;
#else
  if (!term || term->get_master_fd() < 0) {
    return;
  }
  event_loop_.unwatch_fd(term->get_master_fd());
#endif
}

bool Editor::handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up,
                                               bool is_scroll_down) {
  if (!show_integrated_terminal || integrated_terminals.empty()) {
    return false;
  }

  IntegratedTerminal *term = get_integrated_terminal();
  if (!term) {
    return false;
  }

  int panel_h = std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
  int panel_y = std::max(tab_height, ui->get_height() - status_height - panel_h);
  int panel_w = ui->get_render_width();

  if (x < 0 || x >= panel_w || y < panel_y || y >= panel_y + panel_h) {
    return false;
  }

  int content_h = std::max(1, panel_h - 3);
  bool changed = false;
  if (is_scroll_up) {
    changed = term->scroll_lines(3, content_h);
  } else if (is_scroll_down) {
    changed = term->scroll_lines(-3, content_h);
  }

  if (changed) {
    needs_redraw = true;
  }
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
  int panel_w = ui->get_render_width();
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(1, panel_h - 3);
  term->resize(content_h, content_w);

  int cursor_y =
      panel_y + 2 + std::clamp(term->get_cursor_row(), 0, content_h - 1);
  int cursor_x =
      1 + (int)std::min((size_t)(content_w - 1), term->get_cursor_column());

  ui->set_cursor(cursor_x, cursor_y);
}

void Editor::render_integrated_terminal() {
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term) {
    return;
  }

  int panel_h = std::clamp(integrated_terminal_height, 5,
                           std::max(5, ui->get_height() / 2));
  int panel_y = std::max(tab_height, ui->get_height() - status_height - panel_h);
  int panel_w = ui->get_render_width();
  UIRect panel = {0, panel_y, panel_w, panel_h};

  int term_fg = theme.fg_terminal;
  int term_bg = theme.bg_terminal;
  if (term_fg == term_bg) {
    term_fg = (theme.fg_default == term_bg) ? 15 : theme.fg_default;
  }

  ui->fill_rect(panel, " ", term_fg, term_bg);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  int tab_y = panel_y + 1;
  int tab_x = 1;
  for (int i = 0; i < (int)integrated_terminals.size(); i++) {
    std::string base_label = integrated_terminals[i]->get_label().empty()
                                 ? "term " + std::to_string(i + 1)
                                 : integrated_terminals[i]->get_label();
    std::string label = " " + base_label + " ";
    bool active = (i == current_integrated_terminal);
    bool focused = active && integrated_terminals[i]->is_focused();
    int fg = focused
                 ? theme.fg_terminal_tab_focused
                 : (active ? theme.fg_terminal_tab_active
                           : theme.fg_terminal_tab_inactive);
    int bg = focused
                 ? theme.bg_terminal_tab_focused
                 : (active ? theme.bg_terminal_tab_active
                           : theme.bg_terminal_tab_inactive);

    if (tab_x + (int)label.size() + 2 >= panel_w) {
      break;
    }

    ui->draw_text(tab_x, tab_y, label, fg, bg, active);
    int close_x = tab_x + (int)label.size();
    int close_fg =
        ((int)integrated_terminals.size() > 1) ? theme.fg_terminal_tab_close : fg;
    ui->draw_text(close_x, tab_y, "x", close_fg, bg);
    ui->draw_text(close_x + 1, tab_y, "|", theme.fg_terminal_tab_separator,
                  theme.bg_terminal);
    tab_x += (int)label.size() + 2;
  }

  if (tab_x + 3 < panel_w) {
    ui->draw_text(tab_x, tab_y, " + ", theme.fg_terminal_tab_plus,
                  theme.bg_terminal_tab_plus, true);
  }

  int content_h = std::max(1, panel_h - 3);
  int content_w = std::max(1, panel_w - 2);
  term->resize(content_h, content_w);
  auto rows = term->get_recent_output_rows(content_h);
  auto all_blank = [](const std::vector<IntegratedTerminal::OutputRow> &v) {
    for (const auto &row : v) {
      if (!row.text.empty() || !row.cells.empty()) {
        return false;
      }
    }
    return true;
  };
  if (!term->is_active() && (rows.empty() || all_blank(rows))) {
    rows.clear();
    rows.push_back({"[terminal inactive: shell failed or exited]", {}});
    rows.push_back({"[try :terminalnew or check $SHELL]", {}});
  }
  int start_y = panel_y + 2;
  int start = std::max(0, (int)rows.size() - content_h);
  for (int i = 0; i < content_h; i++) {
    int idx = start + i;
    if (idx >= (int)rows.size()) {
      break;
    }
    std::string line = rows[idx].text;
    int max_cols = std::max(1, panel_w - 2);
    int trim_from = std::max(0, (int)line.size() - max_cols);
    if ((int)line.size() > max_cols) {
      line = line.substr(trim_from);
    }

    bool drew_styled = false;
    if (idx >= 0 && idx < (int)rows.size()) {
      auto &styled = rows[idx].cells;
      if (!styled.empty()) {
        int sx = 1;
        int start_cell = std::max(0, (int)styled.size() - max_cols);
        for (int j = start_cell; j < (int)styled.size() && sx < 1 + max_cols; j++) {
          auto colors =
              IntegratedTerminal::resolve_cell_colors(styled[j], term_fg, term_bg);
          int fg = std::clamp(colors.fg, 0, 255);
          int bg = std::clamp(colors.bg, 0, 255);
          ui->draw_text(sx, start_y + i, styled[j].ch, fg, bg);
          sx++;
        }
        drew_styled = true;
      }
    }

    if (!drew_styled) {
      ui->draw_text(1, start_y + i, line, term_fg, term_bg);
    }
  }
}
