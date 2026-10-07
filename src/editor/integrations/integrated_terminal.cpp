#include "base/external_open.h"
#include "base/url_span.h"
#include "editor.h"
#include "render/pane_edges.h"
#include "ui/text.h"
#include <algorithm>

namespace
{
  // A session row leads with nf-dev-terminal, the glyph the file tree already
  // uses for shell scripts: it says "this row is a shell session" without
  // repeating the view label on the bar above it.
  constexpr const char *kShellSessionIcon = "\uE795";

  // Cells a session row spends around the name itself: the leading pad, the
  // glyph, the space after it and the trailing pad.
  constexpr int kShellSessionFrameCells = 4;

  // The session list's bounds: wide enough for a readable name, never more than a
  // third of the panel, and gone entirely when the shell would be squeezed below
  // a usable width (the shell takes the full row then).
  constexpr int kShellListMinCells = 12;
  constexpr int kShellListMinShellCells = 24;

  // The gap between the two view labels, and between the action cluster and the
  // labels it must clear.
  constexpr int kViewTabGap = 3;

  // The panel's own actions, in the order the cluster draws them left to right.
  // One cell each, with a two-cell gap, anchored to the panel's right edge.
  enum BottomPanelAction
  {
    BOTTOM_PANEL_ACTION_NEW = 0,
    BOTTOM_PANEL_ACTION_ZOOM,
    BOTTOM_PANEL_ACTION_CLOSE,
    BOTTOM_PANEL_ACTION_COUNT,
  };

  // The cell an action's glyph sits on, or -1 when it would run into the view
  // labels: the cluster sheds actions from the inside out as the panel narrows.
  int bottom_panel_action_x(int action, int panel_w, int cluster_left)
  {
    const int x = panel_w - 2 - 3 * (BOTTOM_PANEL_ACTION_CLOSE - action);
    return x >= cluster_left ? x : -1;
  }

  // The action owning a cell of the tab bar, or -1 for none.
  int bottom_panel_action_at(int x, int panel_w, int cluster_left)
  {
    for (int action = 0; action < BOTTOM_PANEL_ACTION_COUNT; action++)
    {
      if (bottom_panel_action_x(action, panel_w, cluster_left) == x)
      {
        return action;
      }
    }
    return -1;
  }

  // The first session the list shows, so the focused shell stays visible when the
  // sessions outnumber the rows.
  int shell_list_first_row(int count, int rows, int current)
  {
    if (count <= rows)
    {
      return 0;
    }
    return std::clamp(current - rows / 2, 0, count - rows);
  }

  // The selected run in reading order. Each end carries its own row's column, so
  // a drag kept inside a single row still has two distinct ends.
  struct TerminalSelectionRange
  {
    int start_row = 0;
    int start_col = 0;
    int end_row = 0;
    int end_col = 0;
  };

  TerminalSelectionRange
  terminal_selection_range(int anchor_row, int anchor_col, int cur_row, int cur_col)
  {
    const bool forward = anchor_row < cur_row || (anchor_row == cur_row && anchor_col <= cur_col);
    TerminalSelectionRange range;
    range.start_row = forward ? anchor_row : cur_row;
    range.start_col = forward ? anchor_col : cur_col;
    range.end_row = forward ? cur_row : anchor_row;
    range.end_col = forward ? cur_col : anchor_col;
    return range;
  }
} // namespace

std::string Editor::integrated_terminal_display_name(int index) const
{
  if (index < 0 || index >= (int)integrated_terminals.size() || !integrated_terminals[index])
  {
    return {};
  }
  const std::string &custom = integrated_terminals[index]->get_label();
  // A generated name is bounded by construction; draw it exactly.
  return custom.empty() ? "term " + std::to_string(index + 1) : custom;
}

std::string Editor::integrated_terminal_session_label(int index) const
{
  const std::string name = integrated_terminal_display_name(index);
  if (name.empty())
  {
    return {};
  }
  // The column is sized from the longest name, so this elides only when the
  // panel-wide cap bit: the drawn name gives way before the column does. Only
  // the drawn label shrinks: get_label() keeps the full name, which is what the
  // task runner, lazygit reuse and the Lua API match on.
  const int budget = std::max(1, bottom_panel_shell_list_w() - kShellSessionFrameCells);
  return " " + std::string(kShellSessionIcon) + " " + ui_truncate_cells_ellipsis(name, budget);
}

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
  // A terminal belongs to the workspace, so with no directory named it starts
  // at the workspace root. It used to inherit jot's process cwd, which is that
  // same directory only when the workspace came in as an argument (main chdirs
  // into it): a workspace resumed with no arguments, or opened later from the
  // home menu, left the shell in whatever directory jot was launched from.
  if (!term->open_shell(cwd.empty() ? root_dir : cwd))
  {
#ifdef _WIN32
    set_message(
        "Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)",
        false);
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
  if (!show_integrated_terminal || terminal_zoom_active)
  {
    return 0;
  }
  // The panel reserves its height for either view: the problems list is just
  // as real as the shell and must not be laid over the panes.
  if (bottom_panel_view == BOTTOM_PANEL_TERMINAL && integrated_terminals.empty())
  {
    return 0;
  }
  // One row more than the panel's own rows: the rule along its top is the
  // panel's first row now, not the pane area's last one. Keeping it inside this
  // reservation is what lets a pane's last row be code (see pane_edges.h): the
  // pane area ends above the rule instead of spending a row on it.
  return integrated_terminal_panel_h() + 1;
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
  int max_h = std::max(5, ui->get_height() - status_height - pane_area_top() - 5);
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
  return std::max(pane_area_top(),
                  ui->get_height() - status_height - integrated_terminal_panel_h());
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
      set_message(
          "Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)",
          false);
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
      set_message(
          "Failed to open integrated terminal: ConPTY unavailable (Windows 10 1809+ required)");
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

TerminalView Editor::docked_terminal_view()
{
  TerminalView view;
  view.term = get_integrated_terminal();
  // The dock inks no left border, so its content starts one cell in; the last
  // cell is left to the panel's own edge. The session list takes its column (and
  // the divider gutter in front of it) off the shell's width when it is up.
  const int list_w = bottom_panel_shell_list_w();
  view.x = 1;
  view.y = bottom_panel_content_y();
  view.w = std::max(1, integrated_terminal_panel_w() - 2 - (list_w > 0 ? list_w + 1 : 0));
  view.h = std::max(1, bottom_panel_content_h());
  view.floating = false;
  return view;
}

std::string
Editor::terminal_link_at(const TerminalView &view, int x, int y, int &start_col, int &end_col)
{
  start_col = 0;
  end_col = 0;
  if (!view.term || x < view.x || y < view.y || y >= view.y + view.h)
  {
    return "";
  }
  const int cell_col = x - view.x;
  const int full_row = view.term->get_top_visible_row(view.h) + (y - view.y);
  const std::vector<IntegratedTerminal::StyledCell> cells = view.term->get_row_cells_at(full_row);

  // The row's text with the byte offset each cell starts at: the pointer names
  // a cell, but link detection works on the text.
  std::vector<int> offsets;
  offsets.reserve(cells.size() + 1);
  std::string text;
  for (const auto &cell : cells)
  {
    offsets.push_back((int)text.size());
    text += cell.ch;
  }
  offsets.push_back((int)text.size());
  if (cell_col < 0 || cell_col + 1 >= (int)offsets.size())
  {
    return "";
  }

  int start = 0;
  int end = 0;
  if (!url_span::find(text, offsets[(size_t)cell_col], start, end))
  {
    return "";
  }

  // Back to cell columns for the underline: the span starts at the cell
  // holding its first byte and ends past the one holding its last.
  auto cell_of_byte = [&offsets](int byte)
  {
    for (int i = 0; i + 1 < (int)offsets.size(); i++)
    {
      if (byte < offsets[(size_t)i + 1])
      {
        return i;
      }
    }
    return (int)offsets.size() - 2;
  };
  start_col = cell_of_byte(start);
  end_col = cell_of_byte(end - 1) + 1;
  return text.substr((size_t)start, (size_t)(end - start));
}

bool Editor::open_terminal_link_at(const TerminalView &view, int x, int y)
{
  int start_col = 0;
  int end_col = 0;
  const std::string url = terminal_link_at(view, x, y, start_col, end_col);
  if (url.empty())
  {
    return false;
  }
  if (external_open::open_url(url))
  {
    set_message("Opening " + url);
  }
  else
  {
    set_message("No browser opener found for " + url);
  }
  needs_redraw = true;
  return true;
}

void Editor::update_terminal_link_hover(int x, int y)
{
  // The floating box is painted over the dock and takes the pointer first, the
  // same order the click routing uses; the view under the pointer is the one
  // that answers, so a dock link still underlines with the box open.
  auto holds = [](const TerminalView &view, int px, int py)
  {
    return view.term && px >= view.x && px < view.x + view.w && py >= view.y
           && py < view.y + view.h;
  };

  TerminalView view;
  if (show_floating_terminal && floating_terminal)
  {
    const TerminalView box = floating_terminal_view();
    if (holds(box, x, y))
    {
      view = box;
    }
  }
  if (!view.term && show_integrated_terminal && get_integrated_terminal())
  {
    const TerminalView dock = docked_terminal_view();
    if (holds(dock, x, y))
    {
      view = dock;
    }
  }

  int start_col = 0;
  int end_col = 0;
  const std::string url =
      view.term ? terminal_link_at(view, x, y, start_col, end_col) : std::string();
  const bool on_link = !url.empty();
  const int row = on_link ? view.term->get_top_visible_row(view.h) + (y - view.y) : -1;

  // Only a real change repaints: a run of Ctrl+motions over one link is one
  // underline, not a frame each.
  if (on_link == terminal_link_hover_active && view.floating == terminal_link_hover_in_float
      && row == terminal_link_hover_row && start_col == terminal_link_hover_start
      && end_col == terminal_link_hover_end)
  {
    return;
  }
  terminal_link_hover_active = on_link;
  terminal_link_hover_in_float = view.floating;
  terminal_link_hover_row = row;
  terminal_link_hover_start = start_col;
  terminal_link_hover_end = end_col;
  needs_redraw = true;
}

TerminalView Editor::terminal_selection_view()
{
  return terminal_sel_in_float ? floating_terminal_view() : docked_terminal_view();
}

void Editor::begin_terminal_selection(int x, int y)
{
  begin_terminal_selection_in(docked_terminal_view(), x, y);
}

void Editor::begin_terminal_selection_in(const TerminalView &view, int x, int y)
{
  if (!view.term)
  {
    return;
  }
  const int row = view.term->get_top_visible_row(view.h) + std::clamp(y - view.y, 0, view.h - 1);
  const int col = std::max(0, x - view.x);
  terminal_sel_anchor_row = row;
  terminal_sel_anchor_col = col;
  terminal_sel_cur_row = row;
  terminal_sel_cur_col = col;
  terminal_sel_active = true;
  terminal_sel_dragging = true;
  // Only the view that began the drag paints the band (see the row renderer).
  terminal_sel_in_float = view.floating;
}

void Editor::update_terminal_selection_pos(int x, int y)
{
  const TerminalView view = terminal_selection_view();
  if (!view.term || !terminal_sel_dragging)
  {
    return;
  }
  terminal_sel_cur_row =
      view.term->get_top_visible_row(view.h) + std::clamp(y - view.y, 0, view.h - 1);
  terminal_sel_cur_col = std::max(0, x - view.x);
}

std::string Editor::terminal_selection_text()
{
  IntegratedTerminal *term = terminal_selection_view().term;
  if (!term || !terminal_sel_active)
  {
    return "";
  }
  const TerminalSelectionRange range = terminal_selection_range(
      terminal_sel_anchor_row, terminal_sel_anchor_col, terminal_sel_cur_row, terminal_sel_cur_col);

  std::string out;
  for (int r = range.start_row; r <= range.end_row; r++)
  {
    std::string line = term->get_row_text_at(r);
    int line_len = (int)line.size();
    int from = std::clamp(range.start_col, 0, line_len);
    int to = (r == range.end_row) ? std::clamp(range.end_col, 0, line_len) : line_len;
    if (to < from)
    {
      to = from;
    }
    out += line.substr(from, (size_t)(to - from));
    if (r != range.end_row)
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

bool Editor::handle_integrated_terminal_mouse(
    int x, int y, bool is_click, bool is_motion, bool is_click_release, bool ctrl)
{
  if (!show_integrated_terminal || integrated_terminals.empty())
  {
    return false;
  }

  const int panel_h = integrated_terminal_panel_h();
  const int panel_y = integrated_terminal_panel_y();
  const int panel_w = integrated_terminal_panel_w();
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

  // The tab row is the panel's first row. Its labels and actions are hit-tested
  // by handle_bottom_panel_mouse, which runs first; whatever it declines up
  // there is consumed inertly so a stray click can never fall through and focus
  // a shell.
  if (y == bottom_panel_view_tab_y())
  {
    return true;
  }

  // The session list owns its column: a press focuses that shell. It is drawn
  // over the content rows, so it is hit-tested before the terminal below it.
  if (bottom_panel_shell_list_at(x, y) >= 0)
  {
    if (!is_click)
    {
      return true;
    }
    const int index = bottom_panel_shell_list_at(x, y);
    show_integrated_terminal = true;
    // A live shell is polled so its latest output is on screen the moment it
    // is selected; a session the poll finds exited closes here, and its exit
    // path clears the focus flag, so the press has to land after the poll.
    if (integrated_terminals[(size_t)index]->is_active())
    {
      integrated_terminals[(size_t)index]->poll_output();
    }
    activate_integrated_terminal(index, true);
    set_message("Focused terminal " + std::to_string(index + 1));
    needs_redraw = true;
    return true;
  }

  // The panel's own cells around the shell -- the gutter in front of the list
  // and the border columns -- are chrome: a press there focuses nothing and
  // opens no selection, while a motion still falls through for link hovers.
  const TerminalView view = docked_terminal_view();
  if (is_click && (x < view.x || x >= view.x + view.w || y < view.y || y >= view.y + view.h))
  {
    return true;
  }

  // Ctrl+click on a link in the shell's own output opens it, the way it does
  // in the editor (and in VSCode's terminal). The click still lands the focus
  // on the shell; it only skips the selection a plain click begins.
  if (is_click && ctrl && open_terminal_link_at(docked_terminal_view(), x, y))
  {
    activate_integrated_terminal(current_integrated_terminal, true);
    return true;
  }

  if (is_click)
  {
    // Content area: a primary click refocuses / restarts the terminal, then
    // starts a mouse selection (drag to extend, release copies it).
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
    // Anchored after the restart and the poll, which move the rows the panel
    // shows: the anchor must name the row under the pointer in the next frame.
    begin_terminal_selection(x, y);
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
  event_loop_.watch_fd(
      fd,
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
        // The floating terminal is its own object, not a dock
        // tab, but its shell is polled by the same watcher.
        if (!matched && floating_terminal && floating_terminal->get_master_fd() == fd)
        {
          matched = floating_terminal.get();
        }
        if (!matched)
        {
          event_loop_.unwatch_fd(fd);
          return;
        }
        const bool on_screen =
            matched == floating_terminal.get() ? show_floating_terminal : show_integrated_terminal;
        if (matched->poll_output() && on_screen)
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

  int content_h = std::max(1, bottom_panel_content_h());
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

  const TerminalView view = docked_terminal_view();
  int content_w = std::max(1, view.w);
  int content_h = std::max(1, view.h);
  term->resize(content_h, content_w);

  int cursor_y = view.y + std::clamp(term->get_cursor_row(), 0, content_h - 1);
  int cursor_x = view.x + (int)std::min((size_t)(content_w - 1), term->get_cursor_column());

  ui->set_cursor(cursor_x, cursor_y);
}

const char *Editor::bottom_panel_view_label(int view)
{
  // Flat uppercase labels, the way the panel's own tabs read in VS Code: no
  // glyphs, no chips. The active one carries the brighter colour and an
  // underline; the inactive one stays muted.
  return view == BOTTOM_PANEL_PROBLEMS ? "PROBLEMS" : "TERMINAL";
}

int Editor::bottom_panel_view_tab_x(int view) const
{
  // The labels run Problems then Terminal, the way the panel's tabs read in VS
  // Code. That is not the enum's own order, so the offsets are keyed off the
  // label rather than the index.
  if (view == BOTTOM_PANEL_PROBLEMS)
  {
    return 1;
  }
  return 1 + ui_cell_count(bottom_panel_view_label(BOTTOM_PANEL_PROBLEMS)) + kViewTabGap;
}

int Editor::bottom_panel_view_tabs_width() const
{
  // The two labels plus the gap between them: cell count, not byte count, so
  // the renderer and the click hit-test walk the same offsets.
  return ui_cell_count(bottom_panel_view_label(BOTTOM_PANEL_TERMINAL))
         + ui_cell_count(bottom_panel_view_label(BOTTOM_PANEL_PROBLEMS)) + kViewTabGap;
}

// The panel has no border row of its own: the pane area above owns the
// separator along its top (see pane_edges.h), so the panel's first row carries
// the tab bar -- which is also what stops the two rules from stacking.
int Editor::bottom_panel_view_tab_y() const
{
  return integrated_terminal_panel_y();
}

int Editor::bottom_panel_content_y() const
{
  // Both views start right under the tab bar: the shell's sessions moved out of
  // the row and into the list down the panel's right side.
  return bottom_panel_view_tab_y() + 1;
}

int Editor::bottom_panel_content_h() const
{
  // Everything between the content row and the panel's bottom border.
  const int bottom = integrated_terminal_panel_y() + integrated_terminal_panel_h() - 1;
  return std::max(0, bottom - bottom_panel_content_y());
}

int Editor::bottom_panel_shell_list_w() const
{
  if (!ui || !show_integrated_terminal || bottom_panel_view != BOTTOM_PANEL_TERMINAL
      || integrated_terminals.empty())
  {
    return 0;
  }
  int longest = 0;
  for (int i = 0; i < (int)integrated_terminals.size(); i++)
  {
    longest = std::max(longest, ui_cell_count(integrated_terminal_display_name(i)));
  }
  const int panel_w = integrated_terminal_panel_w();
  const int cap = std::max(kShellListMinCells, panel_w / 3);
  const int want = std::clamp(longest + kShellSessionFrameCells, kShellListMinCells, cap);
  // The divider column is the list's own, so the shell pays for it too.
  if (panel_w - want - 3 < kShellListMinShellCells)
  {
    return 0;
  }
  return want;
}

int Editor::bottom_panel_shell_list_x() const
{
  // The list hugs the panel's right edge, leaving the last cell to the panel
  // frame (which inks the shared edge when the right dock is up).
  const int list_w = bottom_panel_shell_list_w();
  return list_w > 0 ? integrated_terminal_panel_w() - 1 - list_w : -1;
}

int Editor::bottom_panel_shell_list_at(int x, int y) const
{
  const int list_w = bottom_panel_shell_list_w();
  if (list_w <= 0)
  {
    return -1;
  }
  const int list_x = bottom_panel_shell_list_x();
  const int content_y = bottom_panel_content_y();
  const int rows = bottom_panel_content_h();
  if (x < list_x || x >= list_x + list_w || y < content_y || y >= content_y + rows)
  {
    return -1;
  }
  const int count = (int)integrated_terminals.size();
  const int first = shell_list_first_row(count, rows, current_integrated_terminal);
  const int index = first + (y - content_y);
  return index < count ? index : -1;
}

void Editor::render_bottom_panel_session_list()
{
  const int list_w = bottom_panel_shell_list_w();
  if (list_w <= 0)
  {
    return;
  }
  const int list_x = bottom_panel_shell_list_x();
  const int content_y = bottom_panel_content_y();
  const int rows = bottom_panel_content_h();
  const int count = (int)integrated_terminals.size();
  const int first = shell_list_first_row(count, rows, current_integrated_terminal);
  for (int row = 0; row < rows; row++)
  {
    const int y = content_y + row;
    // A one-cell divider in the gutter turns the column into its own region the
    // way a dock's border does, without spending a border row.
    ui->draw_text(list_x - 1, y, "\u2502", theme.fg_terminal_tab_separator, theme.bg_terminal);
    const int index = first + row;
    if (index >= count)
    {
      continue;
    }
    const bool active = index == current_integrated_terminal;
    const bool focused = active && integrated_terminals[(size_t)index]->is_focused();
    // Focused (the shell that owns the keyboard) takes the theme's chip; the
    // merely-selected one keeps the panel background with the brighter text.
    const int fg = focused
                       ? theme.fg_terminal_tab_focused
                       : (active ? theme.fg_terminal_tab_active : theme.fg_terminal_tab_inactive);
    const int bg = focused ? theme.bg_terminal_tab_focused : theme.bg_terminal;
    ui->fill_rect(UIRect{list_x, y, list_w, 1}, " ", fg, bg);
    ui->draw_text(list_x, y, integrated_terminal_session_label(index), fg, bg, active);
  }
}

void Editor::render_integrated_terminal()
{
  if (!show_integrated_terminal)
  {
    return;
  }

  IntegratedTerminal *term = get_integrated_terminal();
  // The shell view needs a terminal; the problems list does not, which is what
  // lets the panel stay useful before any shell exists.
  if (bottom_panel_view == BOTTOM_PANEL_TERMINAL && !term)
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
  // The rule along the panel's top is the panel's own first row -- the
  // reservation above includes it -- and doubles as the resize handle. It used
  // to be the pane area's last row, which spent a code row on chrome; drawing
  // it here keeps that row code and marks the panel off from the panes by its
  // own background. Wrapped in the reservation, so it is skipped while zoomed
  // (where the panel starts right under the menu bar).
  const int rule_y = panel_y - 1;
  const bool has_rule = integrated_terminal_reserved_h() > 0 && rule_y >= topbar_height();
  // The panel is a drawer under the pane area, so it inks only the sides that
  // face a region: the right dock when it shares the panel's rows. Its left and
  // the row over the status line are left unpainted: the dock beside it draws
  // its own line, and the background change marks the row against the status
  // line (see pane_edges.h).
  UIBorderEdges edges = pane_layout::kNoEdges;
  const int dock_w = effective_right_panel_width();
  if (dock_w > 0 && panel.x + panel.w <= ui->get_render_width() - dock_w)
  {
    // The dock spans these rows, so the shared edge is the panel's right side.
    edges.right = true;
  }
  if (has_rule)
  {
    UIBorderEdges rule_edges = pane_layout::kNoEdges;
    rule_edges.bottom = true;
    ui->draw_border(
        UIRect{0, rule_y, panel_w, 1}, theme.fg_panel_border, theme.bg_terminal, rule_edges);
    if (edges.right)
    {
      // The separator against the right dock runs up through the rule row, so
      // the two meet in a corner instead of the rule running into the dock.
      ui->draw_text(panel_w - 1, rule_y, "┐", theme.fg_panel_border, theme.bg_terminal);
    }
  }
  ui->draw_border(panel, theme.fg_panel_border, theme.bg_terminal, edges);

  // The tab bar: the panel's views on the left, its actions on the right, all
  // on the panel's own background. Flat labels (no chips) are what let one row
  // serve both views; the active one reads by its brighter text and its
  // underline. Drawing walks the same geometry the click handler hit-tests, so
  // the offsets cannot drift.
  const int view_tab_y = bottom_panel_view_tab_y();
  const int cluster_left = bottom_panel_view_tabs_width() + 2;
  const BottomPanelView view_order[2] = {BOTTOM_PANEL_PROBLEMS, BOTTOM_PANEL_TERMINAL};
  for (int i = 0; i < 2; i++)
  {
    const BottomPanelView view = view_order[i];
    const int view_tab_x = bottom_panel_view_tab_x(view);
    const std::string label = bottom_panel_view_label(view);
    if (view_tab_x + ui_cell_count(label) > panel_w)
    {
      break;
    }
    const bool active = ((int)bottom_panel_view == view);
    const int fg = active ? theme.fg_terminal_tab_active : theme.fg_terminal_tab_inactive;
    // The underline in the accent colour marks the showing view without
    // spending a chip's background on it.
    ui->draw_text(view_tab_x,
                  view_tab_y,
                  label,
                  fg,
                  term_bg,
                  false,
                  false,
                  active ? 1 : 0,
                  active ? theme.fg_terminal_tab_focused : -1);
  }
  for (int action = 0; action < BOTTOM_PANEL_ACTION_COUNT; action++)
  {
    const int action_x = bottom_panel_action_x(action, panel_w, cluster_left);
    if (action_x < 0)
    {
      continue;
    }
    // The "+" and the red close keep the colours the shell's tab strip used
    // for them; the zoom toggle stays muted chrome.
    const int fg = action == BOTTOM_PANEL_ACTION_NEW     ? theme.fg_terminal_tab_plus
                   : action == BOTTOM_PANEL_ACTION_CLOSE ? theme.fg_terminal_tab_close
                                                         : theme.fg_terminal_tab_inactive;
    const char *glyph = action == BOTTOM_PANEL_ACTION_NEW ? "+"
                        : action == BOTTOM_PANEL_ACTION_ZOOM
                            ? (terminal_zoom_active ? "\uF066" : "\uF065")
                            : "\uF00D";
    ui->draw_text(action_x, view_tab_y, glyph, fg, term_bg);
  }

  if (bottom_panel_view == BOTTOM_PANEL_PROBLEMS)
  {
    render_problems_view(0, panel_w);
    return;
  }

  const TerminalView view = docked_terminal_view();
  if (view.term)
  {
    view.term->resize(view.h, view.w);
  }
  render_terminal_rows(view, term_fg, term_bg);
  render_bottom_panel_session_list();
}

void Editor::render_terminal_rows(const TerminalView &view, int term_fg, int term_bg)
{
  IntegratedTerminal *term = view.term;
  if (!term)
  {
    return;
  }

  const int content_x = view.x;
  const int content_y = view.y;
  const int max_cols = std::max(1, view.w);
  const int content_h = std::max(1, view.h);
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
  // Full-space row of the top displayed line; display row i is full-space
  // row full_base + i when the window is full (synthetic placeholder rows
  // for a dead terminal map back to 0-based instead).
  const int full_base = term->get_top_visible_row(content_h);
  const TerminalSelectionRange sel = terminal_selection_range(
      terminal_sel_anchor_row, terminal_sel_anchor_col, terminal_sel_cur_row, terminal_sel_cur_col);
  // Only the view that began the drag paints the band: the anchors are shared
  // state, so a selection made in the floating box must not tint the dock.
  const bool selected =
      terminal_sel_active && terminal_sel_in_float == view.floating && sel.end_row >= 0;
  const bool full_window = (int)rows.size() >= content_h;
  // The Ctrl+hover underline belongs to the view it was armed in.
  const bool link_hover =
      terminal_link_hover_active && terminal_link_hover_in_float == view.floating;
  for (int i = 0; i < content_h; i++)
  {
    int idx = i;
    if (idx >= (int)rows.size())
    {
      break;
    }
    std::string line = rows[idx].text;
    int trim_from = std::max(0, (int)line.size() - max_cols);
    if ((int)line.size() > max_cols)
    {
      line = line.substr(trim_from);
    }

    // Selection window on this row: sel_left inclusive, sel_right
    // exclusive; unbounded in between the boundary rows.
    int full_row = full_window ? full_base + i : i;
    bool row_sel = selected && full_row >= sel.start_row && full_row <= sel.end_row;
    int sel_left = 0;
    int sel_right = INT_MAX;
    if (row_sel)
    {
      if (full_row == sel.start_row)
      {
        sel_left = sel.start_col;
      }
      if (full_row == sel.end_row)
      {
        sel_right = sel.end_col;
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
        int sx = content_x;
        int start_cell = std::max(0, (int)styled.size() - max_cols);
        for (int j = start_cell; j < (int)styled.size() && sx < content_x + max_cols; j++)
        {
          auto colors = IntegratedTerminal::resolve_cell_colors(styled[j], term_fg, term_bg);
          // No clamp here: a cell colour is an index, but the shell's default
          // colours are theme values, which may be exact 24-bit colours.
          int fg = colors.fg;
          bool sel_cell = row_sel && j >= sel_left && j < sel_right;
          int bg = sel_cell ? theme.bg_selection : colors.bg;
          bool link_cell = link_hover && full_row == terminal_link_hover_row
                           && j >= terminal_link_hover_start && j < terminal_link_hover_end;
          ui->draw_text(sx,
                        content_y + i,
                        styled[j].ch,
                        fg,
                        bg,
                        false,
                        false,
                        link_cell ? 1 : 0,
                        link_cell ? theme.fg_function : -1);
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
        int sx = content_x;
        for (size_t k = 0; k < line.size() && sx < content_x + max_cols; k++, sx++)
        {
          int ccol = trim_from + (int)k;
          bool sel_cell = ccol >= sel_left && ccol < sel_right;
          ui->draw_text(sx,
                        content_y + i,
                        line.substr(k, 1),
                        term_fg,
                        sel_cell ? theme.bg_selection : term_bg);
        }
      }
      else
      {
        ui->draw_text(content_x, content_y + i, line, term_fg, term_bg);
      }
    }
  }
}

void Editor::toggle_bottom_panel()
{
  show_home_menu = false;
  if (show_integrated_terminal)
  {
    // Hide. The panel toggles even while the shell owns focus, which is what
    // VS Code's workbench.action.togglePanel does with its skip-shell list.
    show_integrated_terminal = false;
    terminal_zoom_active = false;
    if (bottom_panel_view == BOTTOM_PANEL_TERMINAL)
    {
      activate_integrated_terminal(current_integrated_terminal, false);
    }
    else if (focus_state == FOCUS_BOTTOM_PANEL)
    {
      focus_state = FOCUS_EDITOR;
    }
    update_pane_layout();
    set_message("Panel hidden", false);
    needs_redraw = true;
    return;
  }

  if (bottom_panel_view == BOTTOM_PANEL_TERMINAL)
  {
    if (integrated_terminals.empty())
    {
      // Nothing to reveal yet; creating a terminal shows the panel too.
      create_integrated_terminal();
      return;
    }
    show_integrated_terminal = true;
    activate_integrated_terminal(current_integrated_terminal, true);
  }
  else
  {
    show_integrated_terminal = true;
    focus_state = FOCUS_BOTTOM_PANEL;
  }
  update_pane_layout();
  set_message("Panel opened", false);
  needs_redraw = true;
}

void Editor::show_problems_panel()
{
  bottom_panel_view = BOTTOM_PANEL_PROBLEMS;
  show_integrated_terminal = true;
  // The list takes focus, so the shell must stop swallowing keys.
  activate_integrated_terminal(current_integrated_terminal, false);
  focus_state = FOCUS_BOTTOM_PANEL;
  problems_selected = 0;
  problems_scroll = 0;
  update_pane_layout();
  needs_redraw = true;
}

bool Editor::handle_bottom_panel_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  if (!show_integrated_terminal || bottom_panel_view != BOTTOM_PANEL_PROBLEMS)
  {
    return false;
  }

  const std::vector<QuickPickItem> items = workspace_diagnostic_quick_pick_items();
  const int total = (int)items.size();
  auto step = [&](int delta)
  {
    if (total <= 0)
    {
      return;
    }
    problems_selected = std::clamp(problems_selected + delta, 0, total - 1);
    needs_redraw = true;
  };

  if (ch == 27)
  {
    focus_state = FOCUS_EDITOR;
    set_message("Panel focus off", false);
    needs_redraw = true;
    return true;
  }
  // 1009 / 1008 are the decoded Down / Up arrow codes.
  if (ch == 'j' || ch == 1009)
  {
    step(1);
    return true;
  }
  if (ch == 'k' || ch == 1008)
  {
    step(-1);
    return true;
  }
  if (ch == 'g')
  {
    problems_selected = 0;
    problems_scroll = 0;
    needs_redraw = true;
    return true;
  }
  if (ch == 'G')
  {
    problems_selected = std::max(0, total - 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 13 || ch == 10)
  {
    jump_to_problem(problems_selected);
    return true;
  }
  if (!is_ctrl && !is_alt && ch == 'q')
  {
    show_integrated_terminal = false;
    focus_state = FOCUS_EDITOR;
    update_pane_layout();
    needs_redraw = true;
    return true;
  }
  // Anything else belongs to the list, not the buffer underneath it: focus is
  // in the panel, so unhandled keys are swallowed rather than forwarded.
  (void)is_shift;
  (void)is_alt;
  return true;
}

bool Editor::handle_bottom_panel_mouse(int x, int y, bool is_click)
{
  if (!show_integrated_terminal)
  {
    return false;
  }

  // The panel owns its own rows and nothing else. Claiming the whole screen here
  // (the Problems view used to answer for any y, on any click or motion) left the
  // list holding focus -- and the keys with it -- so a click in the buffer could
  // not get back to editing, and a motion in the explorer was swallowed too.
  const int panel_y = integrated_terminal_panel_y();
  const int panel_h = integrated_terminal_panel_h();
  const int panel_w = integrated_terminal_panel_w();
  if (y < panel_y || y >= panel_y + panel_h || x < 0 || x >= panel_w)
  {
    return false;
  }

  // The view tab strip belongs to the panel as a whole, not to the view that
  // happens to be showing -- it is the only way back once the shell is active.
  // Switching a view is an action, so it takes a press: hovering the labels
  // (or dragging a selection across the row) must leave the panel alone.
  if (y == bottom_panel_view_tab_y())
  {
    if (!is_click)
    {
      return true;
    }
    for (int view = 0; view < 2; view++)
    {
      const int tab_x = bottom_panel_view_tab_x(view);
      const int label_w = ui_cell_count(bottom_panel_view_label(view));
      if (x >= tab_x && x < tab_x + label_w)
      {
        if ((int)bottom_panel_view != view)
        {
          bottom_panel_view = (BottomPanelView)view;
          if (bottom_panel_view == BOTTOM_PANEL_PROBLEMS)
          {
            // The list takes focus; the shell must stop swallowing keys.
            activate_integrated_terminal(current_integrated_terminal, false);
            focus_state = FOCUS_BOTTOM_PANEL;
          }
          else
          {
            focus_state = FOCUS_EDITOR;
            activate_integrated_terminal(current_integrated_terminal, true);
          }
          needs_redraw = true;
        }
        return true;
      }
    }
    // The action cluster at the right end of the bar: the new shell, the zoom
    // toggle and the panel close, all of which used to be buried in the shell's
    // tab strip.
    const int action = bottom_panel_action_at(x, panel_w, bottom_panel_view_tabs_width() + 2);
    if (action == BOTTOM_PANEL_ACTION_NEW)
    {
      // A new shell is a terminal-view action: show it where it lands. The view
      // moves only once the shell is up, so a failed spawn cannot leave the
      // panel pointing at an empty terminal view (which reserves no rows).
      create_integrated_terminal();
      if (!integrated_terminals.empty())
      {
        bottom_panel_view = BOTTOM_PANEL_TERMINAL;
        focus_state = FOCUS_EDITOR;
      }
      return true;
    }
    if (action == BOTTOM_PANEL_ACTION_ZOOM)
    {
      if (integrated_terminals.empty())
      {
        bottom_panel_view = BOTTOM_PANEL_TERMINAL;
      }
      toggle_terminal_zoom();
      return true;
    }
    if (action == BOTTOM_PANEL_ACTION_CLOSE)
    {
      // The panel's own close: the same path Ctrl+J takes when the panel is up.
      toggle_bottom_panel();
      return true;
    }
    // Past the labels and the actions the rest of the row is inert (the shell's
    // sessions live in the list now), while the Problems view owns the row.
    return bottom_panel_view == BOTTOM_PANEL_PROBLEMS;
  }

  // Only the Problems view has rows to hit; the shell's content belongs to the
  // terminal handler, which runs after this one declines.
  if (bottom_panel_view != BOTTOM_PANEL_PROBLEMS)
  {
    return false;
  }

  if (!is_click)
  {
    return true;
  }

  const int content_y = bottom_panel_content_y();
  const int content_h = bottom_panel_content_h();
  if (y < content_y || y >= content_y + content_h)
  {
    // Inside the panel but not on a list row (the shell's tab strip, the space
    // under a short list): inert, but it still takes focus.
    return true;
  }
  // The list starts under the header row, which is text and not a finding: a
  // press up there computes a negative index and falls out below. Same rule the
  // renderer lays the rows out by, so a press and the row it opens cannot drift.
  const int content_w = std::max(0, integrated_terminal_panel_w() - 2);
  const int header_rows = problems_summary_header(content_w).empty() ? 0 : 1;
  const std::vector<QuickPickItem> items = workspace_diagnostic_quick_pick_items();
  const int index = problems_scroll + (y - content_y - header_rows);
  if (index < 0 || index >= (int)items.size())
  {
    return true;
  }
  jump_to_problem(index);
  return true;
}

bool Editor::handle_problems_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down)
{
  if (!show_integrated_terminal || bottom_panel_view != BOTTOM_PANEL_PROBLEMS)
  {
    return false;
  }
  // Only the wheel over the panel's own rows belongs to the list: anywhere else
  // it is the buffer, the explorer or a dock scrolling.
  const int panel_y = integrated_terminal_panel_y();
  const int panel_h = integrated_terminal_panel_h();
  const int panel_w = integrated_terminal_panel_w();
  if (y < panel_y || y >= panel_y + panel_h || x < 0 || x >= panel_w)
  {
    return false;
  }
  // The renderer clamps against the list length, so a free-running offset is
  // safe here and keeps the scroll from needing the row count.
  if (is_scroll_up)
  {
    problems_scroll = std::max(0, problems_scroll - 3);
    needs_redraw = true;
  }
  if (is_scroll_down)
  {
    problems_scroll += 3;
    needs_redraw = true;
  }
  return true;
}

void Editor::jump_to_problem(int index)
{
  std::vector<QuickPickItem> items = workspace_diagnostic_quick_pick_items();
  if (index < 0 || index >= (int)items.size())
  {
    return;
  }
  // Reuse the picker's accept path so opening the file and placing the cursor
  // stays in one place; the picker is opened only to be accepted immediately.
  open_quick_pick(QUICK_PICK_WORKSPACE_DIAGNOSTICS, "Workspace Diagnostics", std::move(items));
  quick_pick_selected = index;
  accept_quick_pick();
  focus_state = FOCUS_EDITOR;
  needs_redraw = true;
}

void Editor::render_problems_view(int x, int w)
{
  const int content_x = x + 1;
  const int content_w = std::max(0, w - 2);
  const int content_y = bottom_panel_content_y();
  const int content_h = bottom_panel_content_h();
  if (content_w < 8 || content_h <= 0)
  {
    return;
  }

  const std::vector<QuickPickItem> items = workspace_diagnostic_quick_pick_items();
  const int total = (int)items.size();
  if (total == 0)
  {
    ui->draw_text(content_x, content_y, "No problems", theme.fg_comment, theme.bg_terminal);
    return;
  }

  // The header row names the tally -- totals, severities, then how many findings
  // each file holds -- and the findings start under it, so everything below is
  // shifted by the one row it costs.
  const std::string header = problems_summary_header(content_w);
  const int header_rows = header.empty() ? 0 : 1;
  if (header_rows != 0)
  {
    ui->draw_text(content_x, content_y, header, theme.fg_comment, theme.bg_terminal);
  }
  const int list_y = content_y + header_rows;
  const int list_h = std::max(0, content_h - header_rows);

  // Keep the cursor on a row that exists, and the list scrolled to it.
  problems_selected = std::clamp(problems_selected, 0, total - 1);
  if (problems_selected < problems_scroll)
  {
    problems_scroll = problems_selected;
  }
  if (problems_selected >= problems_scroll + list_h)
  {
    problems_scroll = problems_selected - list_h + 1;
  }
  problems_scroll = std::clamp(problems_scroll, 0, std::max(0, total - list_h));

  for (int row = 0; row < list_h; row++)
  {
    const int index = problems_scroll + row;
    if (index >= total)
    {
      break;
    }
    const QuickPickItem &item = items[(size_t)index];
    const bool selected = index == problems_selected;
    // No selection fill. The row's text is the point of the list, and its two
    // halves are already colored by severity and by "secondary" -- painting the
    // theme's selection color behind both (a loud mid-tone in the default
    // palette) is what made them hard to read. The selected row is marked the
    // way the pane tabs mark the active one: an accent sliver in a column of its
    // own, leaving every glyph on the panel's own background.
    const int bg = theme.bg_terminal;
    int fg = theme.fg_default;
    switch (item.severity)
    {
    case 1:
      fg = theme.fg_diagnostic_error;
      break;
    case 2:
      fg = theme.fg_diagnostic_warning;
      break;
    case 3:
      fg = theme.fg_diagnostic_info;
      break;
    case 4:
      fg = theme.fg_diagnostic_hint;
      break;
    default:
      break;
    }

    // The picker's label carries a written severity ("Error: ...") that the
    // colored dot already says, so the row leads with the message.
    std::string message = item.label;
    const size_t sep = message.find(": ");
    if (sep != std::string::npos)
    {
      message = message.substr(sep + 2);
    }
    std::string location = item.detail;
    // A marker column, then the severity dot, then the message. The location
    // only takes part of the row once a column of message is still left over.
    const bool show_location = (int)location.size() + 6 < content_w;
    const int text_w = std::max(0, content_w - 3 - (show_location ? (int)location.size() + 2 : 0));
    if ((int)message.size() > text_w)
    {
      message = message.substr(0, (size_t)text_w);
    }

    const int row_y = list_y + row;
    ui->draw_text(content_x,
                  row_y,
                  selected ? "▌" : " ",
                  selected ? theme.fg_active_border : fg,
                  bg,
                  selected);
    ui->draw_text(content_x + 1, row_y, "●", fg, bg, selected);
    ui->draw_text(content_x + 2, row_y, " ", fg, bg, selected);
    ui->draw_text(content_x + 3,
                  row_y,
                  message + std::string(std::max(0, text_w - (int)message.size()), ' '),
                  fg,
                  bg,
                  selected);
    if (show_location)
    {
      ui->draw_text(content_x + 3 + text_w + 2, row_y, location, theme.fg_comment, bg, selected);
    }
  }
}
