// Top-level mouse event dispatch: event classification, hover
// cancellation, modal / menu / panel routing, and the buffer
// interaction path (click, drag-select, double/triple click, ctrl
// hover, breakpoint and fold toggles, minimap and tab clicks).
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "input/mouse/mouse_internal.h"

using namespace mouse_internal;
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unordered_map>

namespace
{
  bool mouse_debug_enabled()
  {
    static int cached = -1;
    if (cached < 0)
    {
      cached = std::getenv("JOT_MOUSE_DEBUG") != nullptr ? 1 : 0;
    }
    return cached == 1;
  }

  void mouse_debug_log(const char *fmt, ...)
  {
    if (!mouse_debug_enabled())
      return;
    const char *path = std::getenv("JOT_MOUSE_DEBUG");
    if (!path || !*path)
      path = "/tmp/jot-mouse.log";
    FILE *f = std::fopen(path, "a");
    if (!f)
      return;
    std::va_list ap;
    va_start(ap, fmt);
    std::fprintf(f, "[mouse] ");
    std::vfprintf(f, fmt, ap);
    std::fputc('\n', f);
    std::fclose(f);
    va_end(ap);
  }

  constexpr int kMouseDragThreshold = 1;

  int buffer_line_for_visible_index(const FileBuffer &buf, int visible_index)
  {
    int target = std::max(0, visible_index);
    for (int line = 0; line < (int)buf.line_count(); line++)
    {
      if (Folding::is_line_hidden(buf.fold_ranges, line))
      {
        continue;
      }
      if (target == 0)
      {
        return line;
      }
      target--;
    }
    return std::max(0, (int)buf.line_count() - 1);
  }
} // namespace

// Local definition of MEVENT used by handle_mouse()
struct MEVENT
{
  int x, y;
  int bstate;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

static bool is_word_char(unsigned char c)
{
  return std::isalnum(c) || c == '_';
}

static bool is_operator_char(unsigned char c)
{
  switch (c)
  {
  case '=':
  case '!':
  case '<':
  case '>':
  case '+':
  case '-':
  case '*':
  case '/':
  case '%':
  case '&':
  case '|':
  case '^':
  case '~':
  case '?':
  case ':':
    return true;
  default:
    return false;
  }
}

static bool is_pathish_run_char(unsigned char c)
{
  return !std::isspace(c) && c != '"' && c != '\'' && c != '`' && c != '<' && c != '>' && c != '('
         && c != ')' && c != '[' && c != ']' && c != '{' && c != '}';
}

static bool is_identifier_chain_char(const std::string &line, int i)
{
  if (i < 0 || i >= (int)line.size())
    return false;
  unsigned char c = (unsigned char)line[i];
  if (is_word_char(c))
    return true;
  if (c == '.' || c == '-')
  {
    return i > 0 && i + 1 < (int)line.size() && is_word_char((unsigned char)line[i - 1])
           && is_word_char((unsigned char)line[i + 1]);
  }
  if (c == ':')
  {
    bool left_scope = i > 0 && line[i - 1] == ':' && i + 1 < (int)line.size()
                      && is_word_char((unsigned char)line[i + 1]);
    bool right_scope = i + 1 < (int)line.size() && line[i + 1] == ':' && i > 0
                       && is_word_char((unsigned char)line[i - 1]);
    return left_scope || right_scope;
  }
  return false;
}

static bool find_smart_token_span(const std::string &line, int x, int &start, int &end)
{
  if (line.empty())
    return false;

  int pivot = std::clamp(x, 0, (int)line.size() - 1);
  if (std::isspace((unsigned char)line[pivot]))
  {
    start = pivot;
    end = pivot + 1;
    while (start > 0 && std::isspace((unsigned char)line[start - 1]))
      start--;
    while (end < (int)line.size() && std::isspace((unsigned char)line[end]))
      end++;
    return true;
  }

  int run_start = pivot;
  int run_end = pivot + 1;
  while (run_start > 0 && is_pathish_run_char((unsigned char)line[run_start - 1]))
    run_start--;
  while (run_end < (int)line.size() && is_pathish_run_char((unsigned char)line[run_end]))
    run_end++;

  bool path_like = false;
  for (int i = run_start; i < run_end; i++)
  {
    if (line[i] == '/' || line[i] == '\\')
    {
      path_like = true;
      break;
    }
  }
  if (!path_like && run_start < run_end && line[run_start] == '~')
    path_like = true;
  if (!path_like)
  {
    for (int i = run_start; i + 2 < run_end; i++)
    {
      if (line[i] == ':' && line[i + 1] == '/' && line[i + 2] == '/')
      {
        path_like = true;
        break;
      }
    }
  }
  if (path_like)
  {
    start = run_start;
    end = run_end;
    while (end > start && (line[end - 1] == ',' || line[end - 1] == ';'))
      end--;
    return start < end;
  }

  if (is_identifier_chain_char(line, pivot)
      || (pivot > 0 && is_identifier_chain_char(line, pivot - 1)))
  {
    int p = is_identifier_chain_char(line, pivot) ? pivot : pivot - 1;
    start = p;
    end = p + 1;
    while (start > 0 && is_identifier_chain_char(line, start - 1))
      start--;
    while (end < (int)line.size() && is_identifier_chain_char(line, end))
      end++;
    while (start < end && !is_word_char((unsigned char)line[start]))
      start++;
    while (end > start && !is_word_char((unsigned char)line[end - 1]))
      end--;
    return start < end;
  }

  if (is_operator_char((unsigned char)line[pivot]))
  {
    start = pivot;
    end = pivot + 1;
    while (start > 0 && is_operator_char((unsigned char)line[start - 1]))
      start--;
    while (end < (int)line.size() && is_operator_char((unsigned char)line[end]))
      end++;
    return true;
  }

  start = pivot;
  end = pivot + 1;
  return true;
}

static bool find_plain_word_span(const std::string &line, int x, int &start, int &end)
{
  if (line.empty())
    return false;
  int pivot = std::clamp(x, 0, (int)line.size() - 1);
  if (std::isspace((unsigned char)line[pivot]))
  {
    start = pivot;
    end = pivot + 1;
    while (start > 0 && std::isspace((unsigned char)line[start - 1]))
      start--;
    while (end < (int)line.size() && std::isspace((unsigned char)line[end]))
      end++;
    return true;
  }
  if (!is_word_char((unsigned char)line[pivot]))
    return false;
  start = pivot;
  end = pivot + 1;
  while (start > 0 && is_word_char((unsigned char)line[start - 1]))
    start--;
  while (end < (int)line.size() && is_word_char((unsigned char)line[end]))
    end++;
  return start < end;
}

static bool bracket_pair(char c, char &open, char &close, bool &is_open)
{
  switch (c)
  {
  case '(':
    open = '(';
    close = ')';
    is_open = true;
    return true;
  case ')':
    open = '(';
    close = ')';
    is_open = false;
    return true;
  case '[':
    open = '[';
    close = ']';
    is_open = true;
    return true;
  case ']':
    open = '[';
    close = ']';
    is_open = false;
    return true;
  case '{':
    open = '{';
    close = '}';
    is_open = true;
    return true;
  case '}':
    open = '{';
    close = '}';
    is_open = false;
    return true;
  default:
    return false;
  }
}

void Editor::handle_mouse(void *event_ptr)
{
  MEVENT *event = (MEVENT *)event_ptr;
  if (panes.empty())
    return;

  int bstate = event->bstate;

  bool is_click = (bstate == 1);
  bool is_click_release = (bstate == 2);
  bool is_right_click = (bstate == 3);
  bool is_middle_click = (bstate == 4);
  bool is_motion = (bstate == 32);
  bool is_primary_click = is_click || is_click_release;

  if (is_click || is_click_release || is_right_click || is_middle_click || is_motion)
  {
    if (!is_motion || mouse_selecting || mouse_drag_started)
    {
      cancel_lsp_mouse_hover();
      if (ctrl_hover_active)
      {
        ctrl_hover_active = false;
        ctrl_hover_buffer = -1;
        ctrl_hover_line = -1;
        ctrl_hover_start = -1;
        ctrl_hover_end = -1;
        needs_redraw = true;
      }
    }
    if (!is_motion || mouse_selecting || mouse_drag_started)
    {
      clear_debugger_breakpoint_hover();
    }
  }

  if ((is_click || is_motion || is_right_click)
      && handle_menu_bar_mouse(event->x, event->y, is_click, is_motion))
  {
    if (is_motion)
    {
      clear_debugger_breakpoint_hover();
    }
    return;
  }

  if (show_context_menu && (is_click || is_motion))
  {
    if (handle_context_menu_mouse(event->x, event->y, is_click))
    {
      if (is_motion)
      {
        clear_debugger_breakpoint_hover();
      }
      return;
    }
    if (is_motion)
    {
      clear_debugger_breakpoint_hover();
      return;
    }
  }

  if (show_tree_sitter_status_modal
      && (is_click || is_click_release || is_right_click || is_middle_click || is_motion))
  {
    if (is_click || is_right_click || is_middle_click)
    {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54)
      {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16)
      {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = event->x >= modal_x && event->x < modal_x + modal_w && event->y >= modal_y
                    && event->y < modal_y + modal_h;
      if (!inside || is_right_click || is_middle_click)
      {
        show_tree_sitter_status_modal = false;
      }
      needs_redraw = true;
    }
    return;
  }

  if (show_lsp_status_modal
      && (is_click || is_click_release || is_right_click || is_middle_click || is_motion))
  {
    if (is_click || is_right_click || is_middle_click)
    {
      int screen_w = ui->get_render_width();
      int screen_h = ui->get_height();
      int modal_w = std::min(std::max(48, screen_w - 8), 92);
      int modal_h = std::min(std::max(12, screen_h - 6), 28);
      if (screen_w < 54)
      {
        modal_w = std::max(20, screen_w - 2);
      }
      if (screen_h < 16)
      {
        modal_h = std::max(8, screen_h - 2);
      }
      int modal_x = std::max(0, (screen_w - modal_w) / 2);
      int modal_y = std::max(1, (screen_h - modal_h) / 2);
      bool inside = event->x >= modal_x && event->x < modal_x + modal_w && event->y >= modal_y
                    && event->y < modal_y + modal_h;
      if (!inside || is_right_click || is_middle_click)
      {
        show_lsp_status_modal = false;
      }
      needs_redraw = true;
    }
    return;
  }

  if (is_right_click)
  {
    if (right_panel_resize_dragging)
    {
      end_right_panel_resize_drag();
      return;
    }
    if (sidebar_resize_dragging)
    {
      end_sidebar_resize_drag();
      return;
    }
    if (mouse_selecting)
    {
      mouse_selecting = false;
      mouse_drag_started = false;
    }
    if (!open_context_menu_for_mouse(event->x, event->y))
    {
      close_context_menu();
    }
    return;
  }

  if (pane_resize_dragging)
  {
    if (bstate == 32)
    {
      update_pane_resize_drag(event->x, event->y);
      return;
    }
    if (is_click_release)
    {
      end_pane_resize_drag();
      return;
    }
    return;
  }

  if (right_panel_resize_dragging)
  {
    if (is_motion || is_click)
    {
      update_right_panel_resize_drag(event->x);
      return;
    }
    if (is_click_release)
    {
      end_right_panel_resize_drag();
      return;
    }
    return;
  }

  if (sidebar_resize_dragging)
  {
    if (is_motion || is_click)
    {
      update_sidebar_resize_drag(event->x);
      return;
    }
    if (is_click_release)
    {
      end_sidebar_resize_drag();
      return;
    }
    return;
  }

  if (is_click && begin_sidebar_resize_drag(event->x, event->y))
  {
    return;
  }

  if (is_click && begin_right_panel_resize_drag(event->x, event->y))
  {
    return;
  }

  if ((is_click || is_click_release || is_motion)
      && handle_debugger_mouse(event->x, event->y, is_click))
  {
    return;
  }

  if (handle_git_panel_mouse(event->x, event->y, is_click, false))
  {
    return;
  }

  if (is_click && begin_pane_resize_drag(event->x, event->y))
  {
    return;
  }

  if (show_settings_menu)
  {
    if (handle_settings_mouse(event->x, event->y, is_click))
    {
      return;
    }
    if (show_settings_menu)
    {
      return;
    }
  }

  if (show_home_menu)
  {
    if (handle_home_menu_mouse(event->x, event->y, is_click))
    {
      return;
    }
    if (show_home_menu)
    {
      return;
    }
  }

  if (show_sidebar && is_click)
  {
    int reserved_terminal_h = 0;
    if (show_integrated_terminal && !integrated_terminals.empty())
    {
      reserved_terminal_h =
          std::clamp(integrated_terminal_height, 5, std::max(5, ui->get_height() / 2));
    }
    int content_bottom = terminal.get_height() - status_height - reserved_terminal_h;
    int sidebar_w = effective_sidebar_width();
    if (event->x < sidebar_w && event->y >= topbar_height() && event->y < content_bottom)
    {
      focus_state = FOCUS_SIDEBAR;
      long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
      int sidebar_scroll = !kExplorerOnly && active_sidebar_view == SIDEBAR_VIEW_GIT
                               ? git_sidebar_scroll
                               : file_tree_scroll;
      int sidebar_row = event->y - topbar_height() - 1 + sidebar_scroll;
      bool sidebar_double = (last_sidebar_click_ms > 0) && (now_ms - last_sidebar_click_ms <= 350)
                            && (last_sidebar_click_row == sidebar_row);
      last_sidebar_click_ms = now_ms;
      last_sidebar_click_row = sidebar_row;
      handle_sidebar_mouse(event->x, event->y, true, sidebar_double);
      needs_redraw = true;
      return;
    }
  }

  // Act on the press only: acting on the release too would re-dispatch the
  // same header click after a close rebases the surviving tab onto the same
  // cell, closing two terminals from one click.
  if (is_click && handle_integrated_terminal_mouse(event->x, event->y))
  {
    return;
  }

  IntegratedTerminal *active_terminal = get_integrated_terminal();
  if (is_click && show_integrated_terminal && active_terminal && active_terminal->is_focused())
  {
    activate_integrated_terminal(current_integrated_terminal, false);
    needs_redraw = true;
  }

  int target_pane = current_pane;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    const auto &candidate = panes[i];
    if (event->x >= candidate.x && event->x < candidate.x + candidate.w && event->y >= candidate.y
        && event->y < candidate.y + candidate.h)
    {
      target_pane = i;
      break;
    }
  }

  if ((is_click || is_middle_click) && target_pane != current_pane)
  {
    activate_pane(target_pane);
  }

  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);
  refresh_folds(buf);

  if ((is_click || is_middle_click) && event->y == pane.y && !buffers.empty())
  {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    int tabs_x = pane.x + 1;
    int tabs_w = std::max(1, draw_w - 2);
    if (event->x >= tabs_x && event->x < tabs_x + tabs_w)
    {
      FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
      int page = std::max(1, (int)tabs.segments.size());
      if (tabs.scroll_left_x >= 0 && event->x >= tabs.scroll_left_x
          && event->x < tabs.scroll_left_end_x)
      {
        if (scroll_local_tabs(pane, -page))
        {
          needs_redraw = true;
        }
        focus_state = FOCUS_EDITOR;
        restart_blink();
        return;
      }
      if (tabs.scroll_right_x >= 0 && event->x >= tabs.scroll_right_x
          && event->x < tabs.scroll_right_end_x)
      {
        if (scroll_local_tabs(pane, page))
        {
          needs_redraw = true;
        }
        focus_state = FOCUS_EDITOR;
        restart_blink();
        return;
      }
      for (const auto &tab : tabs.segments)
      {
        if (is_middle_click && event->x >= tab.x && event->x < tab.end_x)
        {
          close_buffer_at(tab.buffer_id);
          focus_state = FOCUS_EDITOR;
          restart_blink();
          needs_redraw = true;
          return;
        }

        if (is_click && event->x == tab.close_x)
        {
          close_buffer_at(tab.buffer_id);
          focus_state = FOCUS_EDITOR;
          restart_blink();
          needs_redraw = true;
          return;
        }

        if (is_click && event->x >= tab.x && event->x < tab.close_x)
        {
          switch_to_local_tab(tab.tab_index);
          focus_state = FOCUS_EDITOR;
          restart_blink();
          needs_redraw = true;
          return;
        }
      }
    }
  }

  if (show_minimap && is_click)
  {
    if (event->x >= pane.x + pane.w - minimap_width && event->x < pane.x + pane.w)
    {
      int h = std::max(1, pane.h - tab_height - 1);
      if (event->y >= pane.y + tab_height && event->y < pane.y + tab_height + h)
      {
        int rel_y = event->y - (pane.y + tab_height);
        refresh_folds(buf);
        int total_lines = Folding::visible_line_count(buf.fold_ranges, (int)buf.line_count());
        if (total_lines > 0)
        {
          float ratio = (float)h / total_lines;
          if (ratio > 1.0f)
            ratio = 1.0f;
          int target_visible = (int)(rel_y / ratio);
          int max_scroll_offset = std::max(0, total_lines - h);
          target_visible = std::clamp(target_visible, 0, max_scroll_offset);
          buf.scroll_offset = buffer_line_for_visible_index(buf, target_visible);
          cancel_lsp_mouse_hover();
          hide_lsp_signature();
          hide_lsp_completion();
          restart_blink();
          needs_redraw = true;
          return;
        }
      }
    }
  }

  // Global top tab bar is disabled; pane-local headers are rendered per pane.

  bool inside_pane = (event->x >= pane.x && event->x < pane.x + pane.w && event->y >= pane.y
                      && event->y < pane.y + pane.h);

  if (!inside_pane && !mouse_selecting)
  {
    if (is_motion)
    {
      clear_debugger_breakpoint_hover();
    }
    return;
  }

  if (inside_pane && is_click)
    focus_state = FOCUS_EDITOR;

  const int line_num_width = 8;
  const int code_start_x = pane.x + 1 + line_num_width;
  const int content_top = pane.y + tab_height;
  const int content_bottom = pane.y + pane.h - 1;

  int raw_rel_y = event->y - content_top;
  int rel_y = raw_rel_y;
  int rel_visual_x = event->x - code_start_x;
  if (event->y < content_top)
    raw_rel_y = -1;
  if (event->y >= content_bottom)
    raw_rel_y = content_bottom - content_top;
  rel_y = raw_rel_y;
  // NOTE: rel_visual_x is intentionally left signed here. The drag path
  // below decides per-mode whether the pointer may extend past the code
  // edges (edge-panning selection) or must clamp (gutter clicks); an
  // unconditional clamp would pin the cursor to column 0/the viewport edge
  // and defeat horizontal auto-scroll.
  int visible_rows = std::max(1, pane.h - tab_height - 1);
  int max_scroll_offset = std::max(
      0, Folding::visible_line_count(buf.fold_ranges, (int)buf.line_count()) - visible_rows);

  if (bstate == 32 && mouse_selecting)
  {
    if (raw_rel_y < 0)
    {
      int scroll_by = std::min(buf.scroll_offset, std::max(1, -raw_rel_y));
      buf.scroll_offset -= scroll_by;
    }
    else if (raw_rel_y >= visible_rows)
    {
      int current_visible = 0;
      for (int line = 0; line < (int)buf.line_count() && line < buf.scroll_offset; line++)
      {
        if (!Folding::is_line_hidden(buf.fold_ranges, line))
        {
          current_visible++;
        }
      }
      int scroll_by =
          std::min(max_scroll_offset - current_visible, std::max(1, raw_rel_y - visible_rows + 1));
      buf.scroll_offset = buffer_line_for_visible_index(buf, current_visible + scroll_by);
    }
  }

  rel_y = std::clamp(rel_y, 0, visible_rows - 1);

  // Edge auto-scroll while drag-selecting: mirrors the vertical logic
  // below, but pans scroll_x so the selection can extend past the left /
  // right edge of the viewport. Runs before click_x is computed so the
  // cursor lands in the newly revealed columns on the same event.
  if (bstate == 32 && mouse_selecting && mouse_drag_started)
  {
    // Visible code width mirrors render_buffer_content: pane width minus
    // the minimap (when shown), the right-edge safety cell, the gutter
    // band (fold column + line numbers), and the pane border.
    int code_w = std::max(1, pane.w);
    if (show_minimap && code_w > 20)
      code_w = std::max(1, code_w - minimap_width);
    if (code_w > 3)
      code_w = std::max(1, code_w - 1);
    const int code_end_cells = code_start_x + std::max(1, code_w - 2 - line_num_width);
    if (event->x < code_start_x)
    {
      int overshoot = code_start_x - event->x;
      buf.scroll_x = std::max(0, buf.scroll_x - std::max(1, overshoot));
    }
    else if (event->x >= code_end_cells)
    {
      int overshoot = event->x - code_end_cells + 1;
      buf.scroll_x = buf.scroll_x + std::max(1, overshoot);
    }
  }

  int click_y = buffer_line_for_visible_row(buf, buf.scroll_offset, rel_y);
  if (click_y < 0)
    click_y = 0;
  if (click_y >= (int)buf.line_count())
    click_y = buf.line_count() - 1;
  if (click_y < 0)
    return;
  if (is_motion && !mouse_selecting && !mouse_drag_started && inside_pane && event->y >= content_top
      && event->y < content_bottom && event->x == pane.x + 1 && !buf.filepath.empty())
  {
    update_debugger_breakpoint_hover(current_pane, pane.buffer_id, click_y);
    cancel_lsp_mouse_hover();
    return;
  }
  else if (is_motion)
  {
    clear_debugger_breakpoint_hover();
  }
  if (is_click && event->x == pane.x + 1 && !buf.filepath.empty()
      && toggle_debugger_breakpoint(buf.filepath, click_y))
  {
    clear_debugger_breakpoint_hover();
    focus_state = FOCUS_EDITOR;
    return;
  }
  if (is_click && event->x == pane.x + 2 && toggle_fold_at_line(buf, click_y))
  {
    focus_state = FOCUS_EDITOR;
    return;
  }
  const std::string &clicked_line = buf.line(click_y);
  int line_len = clicked_line.length();
  int start_visual = compute_visual_column(clicked_line, buf.scroll_x, tab_size);
  // During an edge-panning drag the pointer may sit outside the code area:
  // extend rel_visual_x past the viewport instead of clamping it, so the
  // selection keeps growing into the newly revealed columns. Note rel_x
  // is signed here — the clamp below only applied to gutter clicks.
  int rel_x = event->x - code_start_x;
  if (!(bstate == 32 && mouse_selecting && mouse_drag_started))
  {
    rel_x = std::max(0, rel_x);
  }
  int click_visual = start_visual + rel_x;
  // Inlay hints insert cells on screen; subtract them so the click lands on
  // the logical column the user actually sees. Compare against the ORIGINAL
  // click column: the hint positions live in rendered space, and mutating
  // click_visual mid-loop would skip hints whose shifted column now sits
  // above the shrunken value, shoving the cursor right by their widths.
  const int raw_click_visual = click_visual;
  for (const auto &hw : lsp_inlay_hints_visual(buf.filepath, click_y, clicked_line, tab_size))
  {
    if (hw.first <= raw_click_visual)
    {
      click_visual -= hw.second;
    }
  }
  int click_x = visual_to_logical_column(clicked_line, click_visual, tab_size);
  // Past end-of-line: pin to the line end on the left edge, but allow the
  // cursor to ride past it on the right edge while panning so the
  // selection visibly extends.
  if (rel_x < 0 || !(bstate == 32 && mouse_selecting && mouse_drag_started))
  {
    click_x = std::clamp(click_x, 0, line_len);
  }
  else
  {
    click_x = std::max(0, click_x);
  }

  // VSCode-style Ctrl+hover: holding Ctrl while the mouse rests on a word
  // underlines the token (goto-definition affordance). word_span_at_exact
  // is defined below, so this block only records the event position; the
  // actual tracking runs after click_x/click_y are computed.
  const bool ctrl_held_now = event->ctrl;
  const int ctrl_ev_x = event->x;
  const int ctrl_ev_y = event->y;

  if (is_click && event->alt && inside_pane && event->y >= content_top && event->y < content_bottom
      && event->x >= code_start_x)
  {
    focus_state = FOCUS_EDITOR;
    add_caret_at(click_y, click_x);
    mouse_selecting = false;
    mouse_drag_started = false;
    restart_blink();
    needs_redraw = true;
    return;
  }

  auto set_word_selection =
      [&](const Cursor &anchor_start, const Cursor &anchor_end, const Cursor &current_pos)
  {
    Cursor current_start = current_pos;
    Cursor current_end = current_pos;
    const std::string &line = buf.line(current_pos.y);
    if (!line.empty())
    {
      int start = 0;
      int end = 0;
      if (find_plain_word_span(line, current_pos.x, start, end)
          || find_smart_token_span(line, current_pos.x, start, end))
      {
        current_start = {start, current_pos.y};
        current_end = {end, current_pos.y};
      }
    }

    if (compare_cursor_pos(current_start, anchor_start) < 0)
    {
      buf.selection.start = current_start;
      buf.selection.end = anchor_end;
      buf.cursor = current_start;
    }
    else
    {
      buf.selection.start = anchor_start;
      buf.selection.end = current_end;
      buf.cursor = current_end;
    }
    buf.selection.active = true;
  };

  auto set_line_selection =
      [&](const Cursor &anchor_start, const Cursor &anchor_end, const Cursor &current_pos)
  {
    Cursor current_start = {0, current_pos.y};
    Cursor current_end = {(int)buf.line(current_pos.y).length(), current_pos.y};

    if (compare_cursor_pos(current_start, anchor_start) < 0)
    {
      buf.selection.start = current_start;
      buf.selection.end = anchor_end;
      buf.cursor = current_start;
    }
    else
    {
      buf.selection.start = anchor_start;
      buf.selection.end = current_end;
      buf.cursor = current_end;
    }
    buf.selection.active = !(buf.selection.start.x == buf.selection.end.x
                             && buf.selection.start.y == buf.selection.end.y);
  };

  auto find_word_span_at_or_near = [&](int line_y, int x, int &start, int &end) -> bool
  {
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
    if (line.empty())
      return false;

    int pivot = std::clamp(x, 0, (int)line.size() - 1);
    if (find_plain_word_span(line, pivot, start, end))
      return true;
    if (pivot + 1 < (int)line.size() && find_plain_word_span(line, pivot + 1, start, end))
      return true;
    if (pivot - 1 >= 0 && find_plain_word_span(line, pivot - 1, start, end))
      return true;
    if (find_smart_token_span(line, pivot, start, end))
      return true;

    for (int d = 2; d < (int)line.size(); d++)
    {
      int right = pivot + d;
      int left = pivot - d;
      if (right < (int)line.size()
          && (find_plain_word_span(line, right, start, end)
              || find_smart_token_span(line, right, start, end)))
        return true;
      if (left >= 0
          && (find_plain_word_span(line, left, start, end)
              || find_smart_token_span(line, left, start, end)))
        return true;
      if (right >= (int)line.size() && left < 0)
        break;
    }

    return false;
  };

  auto word_span_at_exact = [&](int line_y, int x, int &start, int &end) -> bool
  {
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
    if (line.empty())
      return false;
    int pivot = std::clamp(x, 0, std::max(0, (int)line.size() - 1));
    if (!find_smart_token_span(line, pivot, start, end))
      return false;
    if (x < start || x >= end)
      return false;
    for (int i = start; i < end && i < (int)line.size(); i++)
    {
      if (is_word_char((unsigned char)line[i]))
        return true;
    }
    return false;
  };

  auto find_matching_bracket = [&](int line_y, int x, Cursor &open_pos, Cursor &close_pos) -> bool
  {
    if (line_y < 0 || line_y >= (int)buf.line_count())
      return false;
    const std::string &line = buf.line(line_y);
    if (x < 0 || x >= (int)line.size())
      return false;

    char open = 0, close = 0;
    bool is_open = false;
    if (!bracket_pair(line[x], open, close, is_open))
      return false;

    if (is_open)
    {
      int depth = 1;
      for (int y = line_y; y < (int)buf.line_count(); y++)
      {
        int start_x = (y == line_y) ? x + 1 : 0;
        for (int cx = start_x; cx < (int)buf.line(y).size(); cx++)
        {
          char ch = buf.line(y)[cx];
          if (ch == open)
          {
            depth++;
          }
          else if (ch == close)
          {
            depth--;
            if (depth == 0)
            {
              open_pos = {x, line_y};
              close_pos = {cx, y};
              return true;
            }
          }
        }
      }
    }
    else
    {
      int depth = 1;
      for (int y = line_y; y >= 0; y--)
      {
        int start_x = (y == line_y) ? x - 1 : (int)buf.line(y).size() - 1;
        for (int cx = start_x; cx >= 0; cx--)
        {
          char ch = buf.line(y)[cx];
          if (ch == close)
          {
            depth++;
          }
          else if (ch == open)
          {
            depth--;
            if (depth == 0)
            {
              open_pos = {cx, y};
              close_pos = {x, line_y};
              return true;
            }
          }
        }
      }
    }
    return false;
  };

  int second_click_y = buffer_line_for_visible_row(buf, buf.scroll_offset, rel_y);
  if (second_click_y < 0)
    second_click_y = 0;
  if (second_click_y >= (int)buf.line_count())
    second_click_y = buf.line_count() - 1;
  if (second_click_y < 0)
    return;
  click_y = second_click_y;
  if (is_motion && !mouse_selecting && !mouse_drag_started)
  {
    if (inside_pane && event->y >= content_top && event->y < content_bottom && event->x == pane.x + 1
        && !buf.filepath.empty())
    {
      update_debugger_breakpoint_hover(current_pane, pane.buffer_id, click_y);
      cancel_lsp_mouse_hover();
      return;
    }
    // NOTE: no early return here — plain motion must fall through so the
    // Ctrl+hover tracking below runs on every motion event.
  }

  // VSCode-style Ctrl+hover goto-definition underline. Runs here (after
  // word_span_at_exact is defined and click_x/click_y are final) on every
  // motion/click event: holding Ctrl over a word underlines the token,
  // releasing Ctrl or moving off clears it.
  {
    bool found = false;
    int tok_start = -1, tok_end = -1;
    if ((is_motion || is_click) && ctrl_held_now && !mouse_selecting && !mouse_drag_started
        && inside_pane && ctrl_ev_y >= content_top && ctrl_ev_y < content_bottom
        && ctrl_ev_x >= code_start_x
        && word_span_at_exact(click_y, click_x, tok_start, tok_end))
    {
      found = true;
    }
    if (found)
    {
      if (!ctrl_hover_active || ctrl_hover_buffer != pane.buffer_id || ctrl_hover_line != click_y
          || ctrl_hover_start != tok_start || ctrl_hover_end != tok_end)
      {
        ctrl_hover_active = true;
        ctrl_hover_buffer = pane.buffer_id;
        ctrl_hover_line = click_y;
        ctrl_hover_start = tok_start;
        ctrl_hover_end = tok_end;
        needs_redraw = true;
      }
    }
    else if (ctrl_hover_active)
    {
      ctrl_hover_active = false;
      ctrl_hover_buffer = -1;
      ctrl_hover_line = -1;
      ctrl_hover_start = -1;
      ctrl_hover_end = -1;
      needs_redraw = true;
    }
  }

  // Plain dwell hover (no Ctrl): arm the debounced LSP hover request for
  // the token under the cursor. maybe_fire_lsp_mouse_hover() fires it
  // after the dwell delay. Skipped while Ctrl is held — Ctrl+motion drives
  // the underline above, and arming both would fight over the popup.
  {
    int hover_token_start = -1;
    int hover_token_end = -1;
    if (is_motion && !mouse_selecting && !mouse_drag_started && !ctrl_held_now && inside_pane
        && event->y >= content_top && event->y < content_bottom && event->x >= code_start_x
        && word_span_at_exact(click_y, click_x, hover_token_start, hover_token_end))
    {
      request_lsp_hover_at(current_pane,
                           pane.buffer_id,
                           {click_x, click_y},
                           hover_token_start,
                           hover_token_end,
                           event->x,
                           event->y);
      return;
    }
    else if (is_motion && !mouse_selecting && !mouse_drag_started && !ctrl_held_now)
    {
      cancel_lsp_mouse_hover();
      return;
    }
  }

  if (is_click && event->ctrl && inside_pane && event->y >= content_top && event->y < content_bottom
      && event->x >= code_start_x)
  {
    int token_start = -1;
    int token_end = -1;
    if (word_span_at_exact(click_y, click_x, token_start, token_end))
    {
      focus_state = FOCUS_EDITOR;
      buf.cursor.x = std::clamp(click_x, token_start, token_end);
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;
      buf.selection.start = buf.cursor;
      buf.selection.end = buf.cursor;
      buf.selection.active = false;
      mouse_selecting = false;
      mouse_drag_started = false;
      request_lsp_definition();
      restart_blink();
      needs_redraw = true;
      return;
    }
  }

  if (bstate == 1)
  {
    focus_state = FOCUS_EDITOR;
    restart_blink();
    hide_lsp_completion();
    auto now = std::chrono::steady_clock::now();
    long long now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    bool same_click_cluster = (last_left_click_ms > 0) && (now_ms - last_left_click_ms <= 350)
                              && (last_left_click_pos.y == click_y)
                              && (std::abs(last_left_click_pos.x - click_x) <= 1);
    if (same_click_cluster)
    {
      last_left_click_count = std::min(3, last_left_click_count + 1);
    }
    else
    {
      last_left_click_count = 1;
    }
    last_left_click_ms = now_ms;
    last_left_click_pos = {click_x, click_y};

    mouse_press_screen_x = event->x;
    mouse_press_screen_y = event->y;
    mouse_press_buf_x = click_x;
    mouse_press_buf_y = click_y;
    mouse_drag_started = false;

    if (last_left_click_count >= 3)
    {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_LINE;
      mouse_start = {0, click_y};
      mouse_anchor_end = {line_len, click_y};
      set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      needs_redraw = true;
      mouse_debug_log("press count=3 buf=(%d,%d) mode=LINE", click_x, click_y);
    }
    else if (last_left_click_count == 2)
    {
      const std::string &line = buf.line(click_y);
      if (line.empty())
      {
        mouse_selecting = true;
        mouse_selection_mode = MOUSE_SELECT_LINE;
        mouse_start = {0, click_y};
        mouse_anchor_end = {0, click_y};
        buf.selection.start = mouse_start;
        buf.selection.end = mouse_anchor_end;
        buf.selection.active = true;
        buf.cursor.x = 0;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;
      }
      else
      {
        int pivot = std::min(click_x, (int)line.length() - 1);
        Cursor bracket_open{0, 0};
        Cursor bracket_close{0, 0};
        if (find_matching_bracket(click_y, pivot, bracket_open, bracket_close))
        {
          mouse_selecting = false;
          mouse_selection_mode = MOUSE_SELECT_CHAR;
          mouse_start = bracket_open;
          mouse_anchor_end = {bracket_close.x + 1, bracket_close.y};
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_anchor_end;
          buf.selection.active = true;
          buf.cursor = mouse_anchor_end;
          buf.preferred_x = buf.cursor.x;
          ensure_cursor_visible();
          needs_redraw = true;
          mouse_debug_log("press count=2 bracket buf=(%d,%d)", click_x, click_y);
          return;
        }

        int start = 0;
        int end = 0;
        if (!find_word_span_at_or_near(click_y, click_x, start, end))
        {
          start = 0;
          end = (int)line.length();
        }
        mouse_selecting = true;
        mouse_selection_mode = MOUSE_SELECT_WORD;
        mouse_start = {start, click_y};
        mouse_anchor_end = {end, click_y};
        buf.selection.start = mouse_start;
        buf.selection.end = mouse_anchor_end;
        buf.selection.active = true;
        buf.cursor = mouse_anchor_end;
        buf.preferred_x = buf.cursor.x;
      }
      needs_redraw = true;
      mouse_debug_log("press count=2 word buf=(%d,%d) mode=WORD", click_x, click_y);
    }
    else
    {
      mouse_selecting = true;
      mouse_selection_mode = MOUSE_SELECT_CHAR;
      mouse_start = {click_x, click_y};
      mouse_anchor_end = mouse_start;
      buf.cursor.x = click_x;
      buf.cursor.y = click_y;
      buf.preferred_x = buf.cursor.x;
      buf.selection.start = mouse_start;
      buf.selection.end = {click_x, click_y};
      buf.selection.active = false;
      needs_redraw = true;
      mouse_debug_log("press count=1 buf=(%d,%d) mode=CHAR", click_x, click_y);
    }
  }
  else if (bstate == 2)
  {
    restart_blink();
    hide_lsp_completion();

    if (mouse_selecting)
    {
      if (mouse_drag_started)
      {
        buf.cursor.x = click_x;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;

        if (mouse_selection_mode == MOUSE_SELECT_WORD)
        {
          set_word_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
        }
        else if (mouse_selection_mode == MOUSE_SELECT_LINE)
        {
          set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
        }
        else
        {
          buf.selection.end = {click_x, click_y};
          buf.selection.active = !(buf.selection.start.x == buf.selection.end.x
                                   && buf.selection.start.y == buf.selection.end.y);
        }
        mouse_debug_log(
            "release with-drag buf=(%d,%d) mode=%d", click_x, click_y, (int)mouse_selection_mode);
      }
      else
      {
        if (mouse_selection_mode == MOUSE_SELECT_CHAR)
        {
          buf.cursor.x = mouse_press_buf_x;
          buf.cursor.y = mouse_press_buf_y;
          buf.preferred_x = buf.cursor.x;
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_start;
          buf.selection.active = false;
        }
        else if (mouse_selection_mode == MOUSE_SELECT_WORD
                 || mouse_selection_mode == MOUSE_SELECT_LINE)
        {
          buf.selection.start = mouse_start;
          buf.selection.end = mouse_anchor_end;
          buf.selection.active = mouse_selection_mode == MOUSE_SELECT_LINE
                                 || !(buf.selection.start.x == buf.selection.end.x
                                      && buf.selection.start.y == buf.selection.end.y);
          buf.cursor = mouse_anchor_end;
          buf.preferred_x = buf.cursor.x;
        }
        mouse_debug_log("release no-drag restoring press buf=(%d,%d)", buf.cursor.x, buf.cursor.y);
      }
      mouse_selecting = false;
      mouse_drag_started = false;
      needs_redraw = true;
    }
    else
    {
      mouse_debug_log("release stray (mouse_selecting=false) buf=(%d,%d)", click_x, click_y);
    }
  }
  else if (bstate == 32)
  {
    restart_blink();
    hide_lsp_completion();
    if (mouse_selecting)
    {
      int dx = event->x - mouse_press_screen_x;
      int dy = event->y - mouse_press_screen_y;
      if (!mouse_drag_started)
      {
        if (std::abs(dx) > kMouseDragThreshold || std::abs(dy) > kMouseDragThreshold)
        {
          mouse_drag_started = true;
        }
        else
        {
          mouse_debug_log("motion under-threshold (dx=%d dy=%d) ignored", dx, dy);
          return;
        }
      }

      if (mouse_selection_mode == MOUSE_SELECT_WORD)
      {
        set_word_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      }
      else if (mouse_selection_mode == MOUSE_SELECT_LINE)
      {
        set_line_selection(mouse_start, mouse_anchor_end, {click_x, click_y});
      }
      else
      {
        buf.cursor.x = click_x;
        buf.cursor.y = click_y;
        buf.preferred_x = buf.cursor.x;
        buf.selection.end = {click_x, click_y};
        buf.selection.active = !(buf.selection.start.x == buf.selection.end.x
                                 && buf.selection.start.y == buf.selection.end.y);
      }
      needs_redraw = true;
      mouse_debug_log("motion drag buf=(%d,%d) drag_started=1", click_x, click_y);
    }
  }

  clamp_cursor(pane.buffer_id);
  buf.preferred_x = buf.cursor.x;
  ensure_cursor_visible(bstate != 32);
}
