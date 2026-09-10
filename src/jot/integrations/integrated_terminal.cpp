#include "editor.h"
#include <algorithm>

IntegratedTerminal *Editor::get_integrated_terminal(int index)
{
  if (integrated_terminals.empty())
  {
    return nullptr;
  }

  int resolved = index;
  if (resolved < 0)
  {
    resolved = current_integrated_terminal;
  }
  if (resolved < 0 || resolved >= (int)integrated_terminals.size())
  {
    return nullptr;
  }
  return integrated_terminals[resolved].get();
}

void Editor::activate_integrated_terminal(int index, bool focus)
{
  if (index < 0 || index >= (int)integrated_terminals.size())
  {
    return;
  }

  current_integrated_terminal = index;
  for (int i = 0; i < (int)integrated_terminals.size(); i++)
  {
    integrated_terminals[i]->set_focused(focus && i == index);
  }
}

void Editor::create_integrated_terminal()
{
  create_integrated_terminal("");
}

void Editor::create_integrated_terminal(const std::string &label, const std::string &cwd)
{
  show_home_menu = false;
  
  auto term = std::make_unique<IntegratedTerminal>();
  term->set_label(label);
  if (!term->open_shell(cwd))
  {
#ifdef _WIN32
    set_message("Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)", false);
#else
    set_message("Failed to open integrated terminal: check $SHELL or PTY support", false);
#endif
    return;
  }

  for (auto &existing : integrated_terminals)
  {
    existing->set_focused(false);
  }
  integrated_terminals.push_back(std::move(term));
  current_integrated_terminal = (int)integrated_terminals.size() - 1;
  watch_integrated_terminal_fd(get_integrated_terminal(current_integrated_terminal));
  show_integrated_terminal = true;
  activate_integrated_terminal(current_integrated_terminal, true);
  if (auto *active = get_integrated_terminal(current_integrated_terminal))
  {
    active->poll_output();
  }
  if (label.empty())
  {
    set_message("Opened terminal " + std::to_string(current_integrated_terminal + 1), false);
  }
  else
  {
    set_message("Opened " + label, false);
  }
  needs_redraw = true;
}

void Editor::close_integrated_terminal(int index)
{
  if (index < 0 || index >= (int)integrated_terminals.size())
  {
    return;
  }

  unwatch_integrated_terminal_fd(integrated_terminals[index].get());
  integrated_terminals[index]->close_shell();
  integrated_terminals.erase(integrated_terminals.begin() + index);

  for (auto &job : lsp_install_jobs)
  {
    if (job.terminal_index == index)
    {
      job.terminal_index = -1;
      if (job.running)
      {
        job.running = false;
        job.failed = true;
        job.progress = "terminal closed";
      }
    }
    else if (job.terminal_index > index)
    {
      job.terminal_index--;
    }
  }

  if (integrated_terminals.empty())
  {
    current_integrated_terminal = -1;
    show_integrated_terminal = false;
    terminal_zoom_active = false;
    clear_terminal_selection();
    update_pane_layout();
    set_message("Closed terminal", false);
    needs_redraw = true;
    return;
  }

  if (current_integrated_terminal == index)
  {
    current_integrated_terminal = std::min(index, (int)integrated_terminals.size() - 1);
  }
  else if (index < current_integrated_terminal)
  {
    current_integrated_terminal--;
  }

  activate_integrated_terminal(current_integrated_terminal, show_integrated_terminal);
  clear_terminal_selection();
  set_message("Closed terminal", false);
  needs_redraw = true;
}

int Editor::integrated_terminal_reserved_h() const
{
  if (!show_integrated_terminal || integrated_terminals.empty() || terminal_zoom_active)
  {
    return 0;
  }
  return integrated_terminal_panel_h();
}

int Editor::integrated_terminal_panel_h() const
{
  if (!ui)
  {
    return 5;
  }
  if (terminal_zoom_active && show_integrated_terminal)
  {
    // Zoomed: the terminal spans everything below the menu bar (which is
    // 0 rows in the compact layout), covering the pane headers too.
    return std::max(5, ui->get_height() - status_height - topbar_height());
  }
  // Bottom panel: the stored height, capped so the panes always keep a
  // few rows (the same bound the drag clamps to).
  int max_h = std::max(5, ui->get_height() - status_height - tab_height - 5);
  return std::clamp(integrated_terminal_height, 5, max_h);
}

int Editor::integrated_terminal_panel_y() const
{
  if (!ui)
  {
    return 0;
  }
  if (terminal_zoom_active && show_integrated_terminal)
  {
    return topbar_height();
  }
  return std::max(tab_height, ui->get_height() - status_height - integrated_terminal_panel_h());
}

int Editor::integrated_terminal_panel_w() const
{
  if (!ui)
  {
    return 1;
  }
  if (terminal_zoom_active && show_integrated_terminal)
  {
    // Zoomed: the terminal also covers the sidebar / right-panel docks.
    return std::max(1, ui->get_render_width());
  }
  return std::max(1, ui->get_render_width() - effective_right_panel_width());
}

void Editor::toggle_terminal_zoom()
{
  if (integrated_terminals.empty())
  {
    create_integrated_terminal();
    return;
  }
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term)
  {
    current_integrated_terminal = 0;
    term = get_integrated_terminal();
  }
  if (!term)
  {
    create_integrated_terminal();
    return;
  }
  if (!term->is_active())
  {
    if (!term->open_shell())
    {
      set_message("Failed to open integrated terminal: check $SHELL or PTY support", false);
      needs_redraw = true;
      return;
    }
    watch_integrated_terminal_fd(term);
    term->poll_output();
  }
  show_integrated_terminal = true;
  activate_integrated_terminal(current_integrated_terminal, true);
  terminal_zoom_active = !terminal_zoom_active;
  update_pane_layout();
  set_message(terminal_zoom_active ? "Terminal fullscreen (Alt+Shift+Z to exit)"
                                   : "Terminal un-zoomed",
              false);
  needs_redraw = true;
}

void Editor::toggle_integrated_terminal()
{
  show_home_menu = false;
  if (integrated_terminals.empty())
  {
    create_integrated_terminal();
    return;
  }

  IntegratedTerminal *term = get_integrated_terminal();
  if (!term)
  {
    current_integrated_terminal = 0;
    term = get_integrated_terminal();
  }
  if (!term)
  {
    create_integrated_terminal();
    return;
  }

  if (!term->is_active())
  {
    if (!term->open_shell())
    {
#ifdef _WIN32
      set_message("Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)", false);
#else
      set_message("Failed to restart terminal: check $SHELL or PTY support", false);
#endif
      needs_redraw = true;
      return;
    }
    watch_integrated_terminal_fd(term);
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    term->poll_output();
    set_message("Integrated terminal restarted", false);
    needs_redraw = true;
    return;
  }

  if (!show_integrated_terminal)
  {
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    term->poll_output();
    set_message("Integrated terminal opened", false);
    needs_redraw = true;
    return;
  }

  if (term->is_focused())
  {
    activate_integrated_terminal(current_integrated_terminal, false);
    show_integrated_terminal = false;
    terminal_zoom_active = false;
    update_pane_layout();
    set_message("Integrated terminal hidden", false);
  }
  else
  {
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    term->poll_output();
    set_message("Integrated terminal focused", false);
  }
  needs_redraw = true;
}

void Editor::handle_integrated_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term)
  {
    return;
  }

  if (!term->is_active())
  {
    if (!term->open_shell())
    {
#ifdef _WIN32
      set_message("Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)");
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

  if (is_ctrl && is_shift && (ch == 't' || ch == 'T'))
  {
    create_integrated_terminal();
    return;
  }

  // Terminal zoom uses the same modifier family as pane zoom (Alt+Shift+Z);
  // when the terminal is focused the key reaches the shell otherwise, so it
  // is intercepted here.
  if (is_alt && is_shift && (ch == 'Z' || ch == 'z'))
  {
    toggle_terminal_zoom();
    return;
  }

  if (ch == 27)
  {
    activate_integrated_terminal(current_integrated_terminal, false);
    if (terminal_zoom_active)
    {
      terminal_zoom_active = false;
      update_pane_layout();
    }
    set_message("Terminal focus off");
    needs_redraw = true;
    return;
  }

  if (term->send_key(ch, is_ctrl, is_shift, is_alt))
  {
    needs_redraw = true;
  }
}

void Editor::begin_terminal_selection(int x, int y)
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term)
  {
    return;
  }
  const int panel_y = integrated_terminal_panel_y();
  const int panel_h = integrated_terminal_panel_h();
  int content_h = std::max(1, panel_h - 3);
  int row = term->get_top_visible_row(content_h)
            + std::clamp(y - (panel_y + 2), 0, content_h - 1);
  int col = std::max(0, x - 1);
  terminal_sel_anchor_row = row;
  terminal_sel_anchor_col = col;
  terminal_sel_cur_row = row;
  terminal_sel_cur_col = col;
  terminal_sel_active = true;
  terminal_sel_dragging = true;
}

void Editor::update_terminal_selection_pos(int x, int y)
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !terminal_sel_dragging)
  {
    return;
  }
  const int panel_y = integrated_terminal_panel_y();
  const int panel_h = integrated_terminal_panel_h();
  int content_h = std::max(1, panel_h - 3);
  int row = term->get_top_visible_row(content_h)
            + std::clamp(y - (panel_y + 2), 0, content_h - 1);
  terminal_sel_cur_row = row;
  terminal_sel_cur_col = std::max(0, x - 1);
}

std::string Editor::terminal_selection_text()
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!term || !terminal_sel_active)
  {
    return "";
  }
  int start_row = std::min(terminal_sel_anchor_row, terminal_sel_cur_row);
  int end_row = std::max(terminal_sel_anchor_row, terminal_sel_cur_row);
  int start_col =
      (start_row == terminal_sel_anchor_row) ? terminal_sel_anchor_col : terminal_sel_cur_col;
  int end_col =
      (end_row == terminal_sel_anchor_row) ? terminal_sel_anchor_col : terminal_sel_cur_col;
  if (start_col > end_col)
  {
    std::swap(start_col, end_col);
  }

  std::string out;
  for (int r = start_row; r <= end_row; r++)
  {
    std::string line = term->get_row_text_at(r);
    int line_len = (int)line.size();
    int from = std::clamp(start_col, 0, line_len);
    int to = (r == end_row) ? std::clamp(end_col, 0, line_len) : line_len;
    if (to < from)
    {
      to = from;
    }
    out += line.substr(from, (size_t)(to - from));
    if (r != end_row)
    {
      out += "\n";
    }
  }
  return out;
}

void Editor::finish_terminal_selection()
{
  if (!terminal_sel_dragging)
  {
    return;
  }
  terminal_sel_dragging = false;
  std::string text = terminal_selection_text();
  if (!text.empty())
  {
    set_clipboard_text(text);
    set_message("Copied " + std::to_string(text.size()) + " chars from terminal");
  }
  needs_redraw = true;
}

void Editor::clear_terminal_selection()
{
  terminal_sel_active = false;
  terminal_sel_dragging = false;
  terminal_sel_anchor_row = -1;
  terminal_sel_anchor_col = -1;
  terminal_sel_cur_row = -1;
  terminal_sel_cur_col = -1;
}

bool Editor::handle_integrated_terminal_mouse(int x,
                                              int y,
                                              bool is_click,
                                              bool is_motion,
                                              bool is_click_release)
{
  if (!show_integrated_terminal || integrated_terminals.empty())
  {
    return false;
  }

  const int panel_h = integrated_terminal_panel_h();
  const int panel_y = integrated_terminal_panel_y();
  const int panel_w = integrated_terminal_panel_w();
  int tab_y = panel_y + 1;
  const bool inside = x >= 0 && x < panel_w && y >= panel_y && y < panel_y + panel_h;

  // While a selection drag is in flight every motion/release belongs to the
  // selection, even when the pointer leaves the panel (the selection is
  // clamped to the visible rows).
  if (terminal_sel_dragging && (is_motion || is_click_release || is_click))
  {
    if (inside)
    {
      update_terminal_selection_pos(x, y);
    }
    if (is_click_release || is_click)
    {
      finish_terminal_selection();
    }
    needs_redraw = true;
    return true;
  }

  if (!inside)
  {
    return false;
  }

  if (y == tab_y || y == panel_y)
  {
    // Only presses act on the tab strip; motions/releases over it (e.g.
    // dragging a selection up past the content) are consumed but inert.
    if (!is_click)
    {
      return true;
    }
    int tab_x = 1;
    for (int i = 0; i < (int)integrated_terminals.size(); i++)
    {
      std::string base_label = integrated_terminals[i]->get_label().empty()
                                   ? "term " + std::to_string(i + 1)
                                   : integrated_terminals[i]->get_label();
      std::string label = " " + base_label + " ";
      int close_x = tab_x + (int)label.size();
      int tab_w = (int)label.size() + 2;

      if (x >= tab_x && x < tab_x + tab_w)
      {
        // The close "x" always closes its terminal, even the last one:
        // closing the final terminal hides the whole panel (Esc-like).
        if (x == close_x)
        {
          close_integrated_terminal(i);
        }
        else
        {
          show_integrated_terminal = true;
          activate_integrated_terminal(i, true);
          integrated_terminals[i]->poll_output();
          set_message("Focused terminal " + std::to_string(i + 1));
          needs_redraw = true;
        }
        return true;
      }

      tab_x += tab_w;
      if (tab_x >= panel_w - 4)
      {
        break;
      }
    }

    std::string plus_tab = " + ";
    if (x >= tab_x && x < tab_x + (int)plus_tab.size())
    {
      create_integrated_terminal();
      return true;
    }
    tab_x += (int)plus_tab.size();

    // Fullscreen toggle button, drawn right after the + tab.
    const std::string zoom_tab = "□";
    if (x >= tab_x && x < tab_x + (int)zoom_tab.size())
    {
      toggle_terminal_zoom();
      return true;
    }

    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    needs_redraw = true;
    return true;
  }

  if (is_click)
  {
    // Content area: a primary click starts a mouse selection (drag to
    // extend, release copies it) and refocuses / restarts the terminal.
    begin_terminal_selection(x, y);

    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
    IntegratedTerminal *term = get_integrated_terminal();
    if (term && !term->is_active())
    {
      if (term->open_shell())
      {
        watch_integrated_terminal_fd(term);
        set_message("Integrated terminal restarted");
      }
      else
      {
#ifdef _WIN32
        set_message(
            "Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)");
#else
        set_message("Failed to restart terminal: check $SHELL or PTY support");
#endif
      }
    }
    if (term)
    {
      term->poll_output();
    }
    needs_redraw = true;
    return true;
  }
  // Motions/releases over the content (not part of a selection drag) are
  // not terminal business: they fall through so hovers don't steal focus.
  return false;
}

void Editor::watch_integrated_terminal_fd(IntegratedTerminal *term)
{
#ifdef _WIN32
  (void)term;
  return;
#else
  if (!term || term->get_master_fd() < 0 || event_loop_.is_watching_fd(term->get_master_fd()))
  {
    return;
  }

  int fd = term->get_master_fd();
  event_loop_.watch_fd(fd,
                       true,
                       false,
                       [this, fd]
                       {
                         IntegratedTerminal *matched = nullptr;
                         for (auto &term : integrated_terminals)
                         {
                           if (term && term->get_master_fd() == fd)
                           {
                             matched = term.get();
                             break;
                           }
                         }
                         if (!matched)
                         {
                           event_loop_.unwatch_fd(fd);
                           return;
                         }
                         if (matched->poll_output() && show_integrated_terminal)
                         {
                           needs_redraw = true;
                         }
                         if (matched->get_master_fd() != fd)
                         {
                           event_loop_.unwatch_fd(fd);
                         }
                       });
#endif
}

void Editor::unwatch_integrated_terminal_fd(IntegratedTerminal *term)
{
#ifdef _WIN32
  (void)term;
  return;
#else
  if (!term || term->get_master_fd() < 0)
  {
    return;
  }
  event_loop_.unwatch_fd(term->get_master_fd());
#endif
}

bool Editor::handle_integrated_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down)
{
  if (!show_integrated_terminal || integrated_terminals.empty())
  {
    return false;
  }

  IntegratedTerminal *term = get_integrated_terminal();
  if (!term)
  {
    return false;
  }

  const int panel_h = integrated_terminal_panel_h();
  const int panel_y = integrated_terminal_panel_y();
  const int panel_w = integrated_terminal_panel_w();

  if (x < 0 || x >= panel_w || y < panel_y || y >= panel_y + panel_h)
  {
    return false;
  }

  int content_h = std::max(1, panel_h - 3);
  bool changed = false;
  if (is_scroll_up)
  {
    changed = term->scroll_lines(3, content_h);
  }
  else if (is_scroll_down)
  {
    changed = term->scroll_lines(-3, content_h);
  }

  if (changed)
  {
    needs_redraw = true;
  }
  return true;
}

void Editor::place_integrated_terminal_cursor()
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term || !term->is_active() || !term->is_focused())
  {
    return;
  }
  if (term->get_scroll_offset() != 0)
  {
    ui->hide_cursor();
    return;
  }
  if (!term->is_cursor_position_valid())
  {
    ui->hide_cursor();
    return;
  }

  const int panel_h = integrated_terminal_panel_h();
  const int panel_y = integrated_terminal_panel_y();
  const int panel_w = integrated_terminal_panel_w();
  int content_w = std::max(1, panel_w - 2);
  int content_h = std::max(1, panel_h - 3);
  term->resize(content_h, content_w);

  int cursor_y = panel_y + 2 + std::clamp(term->get_cursor_row(), 0, content_h - 1);
  int cursor_x = 1 + (int)std::min((size_t)(content_w - 1), term->get_cursor_column());

  ui->set_cursor(cursor_x, cursor_y);
}

void Editor::render_integrated_terminal()
{
  IntegratedTerminal *term = get_integrated_terminal();
  if (!show_integrated_terminal || !term)
  {
    return;
  }

  const int panel_h = integrated_terminal_panel_h();
  const int panel_y = integrated_terminal_panel_y();
  const int panel_w = integrated_terminal_panel_w();
  UIRect panel = {0, panel_y, panel_w, panel_h};

  int term_fg = theme.fg_terminal;
  int term_bg = theme.bg_terminal;
  if (term_fg == term_bg)
  {
    term_fg = (theme.fg_default == term_bg) ? 15 : theme.fg_default;
  }

  ui->fill_rect(panel, " ", term_fg, term_bg);
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal);

  int tab_y = panel_y + 1;
  int tab_x = 1;
  for (int i = 0; i < (int)integrated_terminals.size(); i++)
  {
    std::string base_label = integrated_terminals[i]->get_label().empty()
                                 ? "term " + std::to_string(i + 1)
                                 : integrated_terminals[i]->get_label();
    std::string label = " " + base_label + " ";
    bool active = (i == current_integrated_terminal);
    bool focused = active && integrated_terminals[i]->is_focused();
    int fg = focused ? theme.fg_terminal_tab_focused
                     : (active ? theme.fg_terminal_tab_active : theme.fg_terminal_tab_inactive);
    int bg = focused ? theme.bg_terminal_tab_focused
                     : (active ? theme.bg_terminal_tab_active : theme.bg_terminal_tab_inactive);

    if (tab_x + (int)label.size() + 2 >= panel_w)
    {
      break;
    }

    ui->draw_text(tab_x, tab_y, label, fg, bg, active);
    int close_x = tab_x + (int)label.size();
    // Always render the close marker in the close color so it reads as a
    // button even when it is the only tab (clicking it closes the panel).
    ui->draw_text(close_x, tab_y, "x", theme.fg_terminal_tab_close, bg);
    ui->draw_text(close_x + 1, tab_y, "|", theme.fg_terminal_tab_separator, theme.bg_terminal);
    tab_x += (int)label.size() + 2;
  }

  if (tab_x + 3 < panel_w)
  {
    ui->draw_text(
        tab_x, tab_y, " + ", theme.fg_terminal_tab_plus, theme.bg_terminal_tab_plus, true);
    tab_x += 3;
    // Fullscreen toggle button; highlighted while zoomed so the state is
    // visible even when the terminal fills the whole area.
    if (tab_x + 1 < panel_w)
    {
      int zoom_fg = terminal_zoom_active ? theme.fg_terminal_tab_focused
                                         : theme.fg_terminal_tab_inactive;
      int zoom_bg = terminal_zoom_active ? theme.bg_terminal_tab_focused
                                         : theme.bg_terminal_tab_inactive;
      ui->draw_text(tab_x, tab_y, "□", zoom_fg, zoom_bg, true);
    }
  }

  int content_h = std::max(1, panel_h - 3);
  int content_w = std::max(1, panel_w - 2);
  term->resize(content_h, content_w);
  auto rows = term->get_recent_output_rows(content_h);
  auto all_blank = [](const std::vector<IntegratedTerminal::OutputRow> &v)
  {
    for (const auto &row : v)
    {
      if (!row.text.empty() || !row.cells.empty())
      {
        return false;
      }
    }
    return true;
  };
  if (!term->is_active() && (rows.empty() || all_blank(rows)))
  {
    rows.clear();
    rows.push_back({"[terminal inactive: shell failed or exited]", {}});
    rows.push_back({"[try :terminalnew or check $SHELL]", {}});
  }
  int start_y = panel_y + 2;
  // Full-space row of the top displayed line; display row i is full-space
  // row full_base + i when the window is full (synthetic placeholder rows
  // for a dead terminal map back to 0-based instead).
  int full_base = term->get_top_visible_row(content_h);
  int sel_start_row = std::min(terminal_sel_anchor_row, terminal_sel_cur_row);
  int sel_end_row = std::max(terminal_sel_anchor_row, terminal_sel_cur_row);
  int sel_start_col =
      (sel_start_row == terminal_sel_anchor_row) ? terminal_sel_anchor_col : terminal_sel_cur_col;
  int sel_end_col =
      (sel_end_row == terminal_sel_anchor_row) ? terminal_sel_anchor_col : terminal_sel_cur_col;
  if (sel_start_col > sel_end_col)
  {
    std::swap(sel_start_col, sel_end_col);
  }
  const bool sel = terminal_sel_active && sel_end_row >= 0;
  const bool full_window = (int)rows.size() >= content_h;
  for (int i = 0; i < content_h; i++)
  {
    int idx = i;
    if (idx >= (int)rows.size())
    {
      break;
    }
    std::string line = rows[idx].text;
    int max_cols = std::max(1, panel_w - 2);
    int trim_from = std::max(0, (int)line.size() - max_cols);
    if ((int)line.size() > max_cols)
    {
      line = line.substr(trim_from);
    }

    // Selection window on this row: sel_left inclusive, sel_right
    // exclusive; unbounded in between the boundary rows.
    int full_row = full_window ? full_base + i : i;
    bool row_sel = sel && full_row >= sel_start_row && full_row <= sel_end_row;
    int sel_left = 0;
    int sel_right = INT_MAX;
    if (row_sel)
    {
      if (full_row == sel_start_row)
      {
        sel_left = sel_start_col;
      }
      if (full_row == sel_end_row)
      {
        sel_right = sel_end_col;
      }
      if (sel_right < sel_left)
      {
        sel_right = sel_left;
      }
    }

    bool drew_styled = false;
    if (idx >= 0 && idx < (int)rows.size())
    {
      auto &styled = rows[idx].cells;
      if (!styled.empty())
      {
        int sx = 1;
        int start_cell = std::max(0, (int)styled.size() - max_cols);
        for (int j = start_cell; j < (int)styled.size() && sx < 1 + max_cols; j++)
        {
          auto colors = IntegratedTerminal::resolve_cell_colors(styled[j], term_fg, term_bg);
          int fg = std::clamp(colors.fg, 0, 255);
          bool sel_cell = row_sel && j >= sel_left && j < sel_right;
          int bg = sel_cell ? theme.bg_selection : std::clamp(colors.bg, 0, 255);
          ui->draw_text(sx, start_y + i, styled[j].ch, fg, bg);
          sx++;
        }
        drew_styled = true;
      }
    }

    if (!drew_styled)
    {
      if (row_sel)
      {
        // Byte-granular highlight for plain rows (no vterm cells): each
        // byte occupies one column, so the anchor's column maps directly.
        int sx = 1;
        for (size_t k = 0; k < line.size() && sx < 1 + max_cols; k++, sx++)
        {
          int ccol = trim_from + (int)k;
          bool sel_cell = ccol >= sel_left && ccol < sel_right;
          ui->draw_text(sx, start_y + i, line.substr(k, 1), term_fg,
                        sel_cell ? theme.bg_selection : term_bg);
        }
      }
      else
      {
        ui->draw_text(1, start_y + i, line, term_fg, term_bg);
      }
    }
  }
}
