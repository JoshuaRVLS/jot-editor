// The floating terminal (Alt+Shift+T, :termfloat): a box centered over the pane
// area hosting its own shell, alongside the docked panel rather than inside it.
//
// It is a second view of the same machinery: the rows come from
// IntegratedTerminal, the painting from Editor::render_terminal_rows and the
// selection from the shared anchors (see src/core/panels.h TerminalView),
// so the two views cannot drift apart. Hiding it leaves the shell and its
// scrollback running; a shell that has exited restarts on the next key or the
// next open, in the directory it was first started in.
#include "core/editor.h"
#include "core/panes.h" // pane_content_top, pane_viewport_h
#include "terminal/integrated.h"
#include "ui/components.h"

#include <algorithm>

namespace
{
  // A box narrower than this cannot show a command line, and one shorter cannot
  // show a prompt with its output. Both still give way to a small pane, which
  // is the real bound.
  constexpr int kMinFloatWidth = 20;
  constexpr int kMinFloatHeight = 5;
} // namespace

TerminalBox Editor::floating_terminal_rect() const
{
  // The room the box lives in: the active pane's own text rows. The pane chrome
  // (the breadcrumb winbar) is a float painted *after* this overlay, so a box
  // that reached up into it had its top border and both top corners erased by
  // the breadcrumb, which read as a frame with no top.
  const PaneArea area = compute_pane_area();
  TerminalBox room = {area.x, area.y, std::max(1, area.w), std::max(1, area.h)};
  if (!panes.empty())
  {
    const int index = std::clamp(current_pane, 0, (int)panes.size() - 1);
    const SplitPane &pane = panes[(size_t)index];
    const int top = std::max(area.y, pane_content_top(pane));
    const int bottom = std::min(area.y + area.h, top + std::max(1, pane_viewport_h(pane)));
    room = {pane.x, top, std::max(1, pane.w), std::max(1, bottom - top)};
  }

  // The settings are a share of that room, so the box is as big as the pane
  // allows; the floors keep a usable shell on a small one.
  int w = std::min(room.w, room.w * floating_terminal_width / 100);
  int h = std::min(room.h, room.h * floating_terminal_height / 100);
  w = std::max(w, std::min(kMinFloatWidth, room.w));
  h = std::max(h, std::min(kMinFloatHeight, room.h));
  const TerminalBox box = {room.x + std::max(0, (room.w - w) / 2),
                           room.y + std::max(0, (room.h - h) / 2),
                           std::max(1, w),
                           std::max(1, h)};
  return box;
}

TerminalView Editor::floating_terminal_view() const
{
  TerminalView view;
  view.term = floating_terminal.get();
  const TerminalBox box = floating_terminal_rect();
  view.x = box.x + 1;
  view.y = box.y + 1;
  view.w = std::max(1, box.w - 2);
  view.h = std::max(1, box.h - 2);
  view.floating = true;
  return view;
}

void Editor::render_floating_terminal()
{
  if (!show_floating_terminal || !floating_terminal)
  {
    return;
  }

  int term_fg = theme.fg_terminal;
  int term_bg = theme.bg_terminal;
  if (term_fg == term_bg)
  {
    term_fg = (theme.fg_default == term_bg) ? 15 : theme.fg_default;
  }

  const TerminalBox box = floating_terminal_rect();
  const UIRect frame = {box.x, box.y, box.w, box.h};
  // An opaque box with no scrim over the rest: the buffer stays readable around
  // it, and the shell's own cursor inside is the focus mark.
  ui_draw_panel(*ui, frame, UIPanelStyle{term_fg, term_bg, theme.fg_panel_border, term_bg});
  // The title says which shell this is, and the docked strip's focused colour
  // carries the one fact worth colouring: this shell has the keys.
  ui_draw_panel_title(*ui, frame, " Floating terminal ", theme.fg_terminal_tab_focused, term_bg);

  const TerminalView view = floating_terminal_view();
  if (view.term)
  {
    view.term->resize(view.h, view.w);
  }
  render_terminal_rows(view, term_fg, term_bg);
}

void Editor::place_floating_terminal_cursor()
{
  IntegratedTerminal *term = floating_terminal.get();
  if (!show_floating_terminal || !term || !term->is_active() || !term->is_focused())
  {
    return;
  }
  if (term->get_scroll_offset() != 0 || !term->is_cursor_position_valid())
  {
    ui->hide_cursor();
    return;
  }

  const TerminalView view = floating_terminal_view();
  const int cursor_x = view.x + (int)std::min((size_t)(view.w - 1), term->get_cursor_column());
  const int cursor_y = view.y + std::clamp(term->get_cursor_row(), 0, view.h - 1);
  ui->set_cursor(cursor_x, cursor_y);
}

void Editor::toggle_floating_terminal()
{
  if (show_floating_terminal)
  {
    hide_floating_terminal("Floating terminal hidden (Alt+Shift+T brings it back)");
    return;
  }
  open_floating_terminal();
}

void Editor::open_floating_terminal()
{
  show_home_menu = false;
  if (!floating_terminal)
  {
    floating_terminal = std::make_unique<IntegratedTerminal>();
    floating_terminal->set_label("Floating terminal");
  }
  IntegratedTerminal *term = floating_terminal.get();
  // No directory named: the shell starts at the workspace root, the same rule
  // the docked terminal follows.
  if (!term->is_active() && !term->open_shell(root_dir))
  {
    floating_terminal.reset();
#ifdef _WIN32
    set_message("Failed to open floating terminal: ConPTY unavailable (Windows 10 1809+ required)");
#else
    set_message("Failed to open floating terminal: check $SHELL or PTY support");
#endif
    needs_redraw = true;
    return;
  }
  watch_integrated_terminal_fd(term);
  term->reset_scroll();
  term->poll_output();
  // Only one shell paints a cursor: the docked one gives up the keys while the
  // box is up. Esc returns focus to the editor, not to the dock.
  activate_integrated_terminal(current_integrated_terminal, false);
  clear_terminal_selection();
  show_floating_terminal = true;
  term->set_focused(true);
  set_message("Floating terminal (Esc hides it; the shell keeps running)", false);
  needs_redraw = true;
}

void Editor::hide_floating_terminal(const std::string &message)
{
  if (!show_floating_terminal)
  {
    return;
  }
  show_floating_terminal = false;
  if (floating_terminal)
  {
    floating_terminal->set_focused(false);
    floating_terminal->reset_scroll();
  }
  clear_terminal_selection();
  if (!message.empty())
  {
    set_message(message, false);
  }
  needs_redraw = true;
}

void Editor::handle_floating_terminal_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  if (!show_floating_terminal)
  {
    return;
  }
  IntegratedTerminal *term = floating_terminal.get();
  if (!term)
  {
    return;
  }

  // Esc is the box's way out: the overlay goes, the shell stays.
  if (ch == 27)
  {
    hide_floating_terminal("Floating terminal hidden (Alt+Shift+T brings it back)");
    return;
  }

  if (!term->is_active())
  {
    // The shell exited while the box was up: any key brings it back where it
    // was, the same rule the docked terminal uses.
    if (!term->open_shell())
    {
#ifdef _WIN32
      set_message("Failed to restart terminal: ConPTY unavailable (Windows 10 1809+ required)");
#else
      set_message("Failed to restart terminal: check $SHELL or PTY support");
#endif
      needs_redraw = true;
      return;
    }
    watch_integrated_terminal_fd(term);
    set_message("Floating terminal restarted", false);
  }

  if (term->send_key(ch, is_ctrl, is_shift, is_alt))
  {
    needs_redraw = true;
  }
}

void Editor::begin_floating_terminal_selection(int x, int y)
{
  begin_terminal_selection_in(floating_terminal_view(), x, y);
}

bool Editor::handle_floating_terminal_mouse(
    int x, int y, bool is_click, bool is_motion, bool is_click_release, bool ctrl)
{
  if (!show_floating_terminal || !floating_terminal)
  {
    return false;
  }
  const TerminalBox box = floating_terminal_rect();
  const bool inside = x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h;

  // A selection drag in flight keeps the pointer until the button comes up,
  // inside the box or not: the anchors clamp to the visible rows.
  if (terminal_sel_dragging && terminal_sel_in_float && (is_motion || is_click_release || is_click))
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
    // A click away dismisses the box, the way any overlay closes; the shell
    // stays running. Motion outside is left alone, so passing the pointer over
    // the buffer does not hide it.
    if (is_click)
    {
      hide_floating_terminal("Floating terminal hidden (Alt+Shift+T brings it back)");
      return true;
    }
    return false;
  }

  // Ctrl+click on a link in the box's shell opens it, like everywhere else;
  // the box stays up and the click is the link's, not a selection's.
  if (is_click && ctrl && open_terminal_link_at(floating_terminal_view(), x, y))
  {
    floating_terminal->set_focused(true);
    return true;
  }

  if (is_click)
  {
    if (!floating_terminal->is_active() && floating_terminal->open_shell())
    {
      watch_integrated_terminal_fd(floating_terminal.get());
    }
    floating_terminal->poll_output();
    floating_terminal->set_focused(true);
    // Anchored after the restart and the poll, which move the rows the box
    // shows: the anchor must name the row under the pointer in the next frame.
    begin_floating_terminal_selection(x, y);
    needs_redraw = true;
    return true;
  }
  // Motions and releases over the box, with no drag running, are not its
  // business (nothing to hover); they fall through.
  return false;
}

bool Editor::handle_floating_terminal_scroll(int x, int y, bool is_scroll_up, bool is_scroll_down)
{
  if (!show_floating_terminal || !floating_terminal)
  {
    return false;
  }
  const TerminalBox box = floating_terminal_rect();
  if (x < box.x || x >= box.x + box.w || y < box.y || y >= box.y + box.h)
  {
    return false;
  }

  const int content_h = std::max(1, box.h - 2);
  bool changed = false;
  if (is_scroll_up)
  {
    changed = floating_terminal->scroll_lines(3, content_h);
  }
  else if (is_scroll_down)
  {
    changed = floating_terminal->scroll_lines(-3, content_h);
  }
  if (changed)
  {
    needs_redraw = true;
  }
  return true;
}
