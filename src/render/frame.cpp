#include "bracket.h"
#include "column_utils.h"
#include "core/editor.h"
#include "core/file_icons.h"
#include "folding.h"
#include "lua/api/api.h"
#include "render/gutter.h"
#include "render/pane_edges.h"
#include "ui/text.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <unordered_map>

namespace
{
  std::string ellipsize_right(const std::string &s, int max_len)
  {
    if (max_len <= 0)
    {
      return "";
    }
    if (ui_cell_count(s) <= max_len)
    {
      return s;
    }
    if (max_len <= 3)
    {
      return ui_take_cells(s, max_len);
    }
    return ui_take_cells(s, max_len - 3) + "...";
  }

  std::string file_tab_base_name(const FileBuffer &buffer)
  {
    std::string base;
    if (!buffer.filepath.empty())
    {
      base = std::filesystem::path(buffer.filepath).filename().string();
    }
    return base.empty() ? "[No Name]" : base;
  }

  bool compute_code_cursor_screen_pos(const SplitPane &pane,
                                      const FileBuffer &buf,
                                      bool show_minimap,
                                      int minimap_width,
                                      int tab_size,
                                      int hint_cells,
                                      int &display_x,
                                      int &display_y)
  {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }

    const int code_start_x = pane.x + 1 + gutter::width(buf.line_count());
    const int code_end_x = pane.x + draw_w - 2;
    const int min_y = pane_content_top(pane);
    int max_y = pane.y + pane.h - 1;

    const int viewport_h = std::max(1, pane_viewport_h(pane));
    // One prepared view for the whole question: the loop it replaces asked
    // "which line is on row N" once per row, re-scanning every fold range each
    // time. This runs twice per frame (the idle and the paint path).
    const auto fold_view = Folding::view_of(buf.fold_ranges);
    const int found = fold_view->visible_row_for_line(
      buf.scroll_offset, buf.cursor.y, viewport_h, (int)buf.line_count());
    const int visible_row = found >= 0 ? found : 0;
    const bool found_row = found >= 0;
    // Reports false when the caret has no cell on screen (its line scrolled out
    // of view, or the pane is too narrow for the code area); the caller then
    // hides the caret. display_y is still filled in for the found_row case.
    display_y = (found_row ? visible_row : viewport_h - 1) + pane_content_top(pane);

    int logical_cursor_x = buf.cursor.x;
    int logical_scroll_x = buf.scroll_x;
    if (buf.cursor.y >= 0 && buf.cursor.y < (int)buf.line_count())
    {
      const std::string &line = buf.line(buf.cursor.y);
      int cursor_visual = compute_visual_column(line, logical_cursor_x, tab_size);
      int scroll_visual = compute_visual_column(line, logical_scroll_x, tab_size);
      // Inlay hints before the caret shift the caret right with the text.
      display_x = code_start_x + (cursor_visual - scroll_visual) + hint_cells;
    }
    else
    {
      display_x = code_start_x + (logical_cursor_x - logical_scroll_x);
    }

    if (max_y < min_y)
      max_y = min_y;
    if (display_y < min_y)
      display_y = min_y;
    if (display_y > max_y)
      display_y = max_y;

    if (code_end_x < code_start_x)
    {
      display_x = code_start_x;
      return false;
    }
    if (display_x < code_start_x)
      display_x = code_start_x;
    if (display_x > code_end_x)
      display_x = code_end_x;
    return found_row;
  }

  UICursorShape editor_cursor_shape(const std::string &style_raw)
  {
    std::string style = style_raw;
    std::transform(style.begin(), style.end(), style.begin(), ::tolower);
    if (style == "block" || style == "steady_block" || style == "steadyblock")
      return UICursorShape::Block;
    return UICursorShape::Bar;
  }
} // namespace

void Editor::render()
{
  IntegratedTerminal *active_terminal = get_integrated_terminal();
  // Too-narrow windows lose the sidebar so the code keeps a usable width. That
  // is a reaction to the size, not a user decision: remember it and bring the
  // sidebar back once the window fits again, otherwise shrinking once would
  // hide the explorer for the rest of the session.
  if (ui && !zen_mode)
  {
    // Reserve a readable code area, not merely enough cells to draw a pane.
    const bool too_narrow = ui->get_render_width() < effective_sidebar_width() + 40;
    if (show_sidebar && too_narrow)
    {
      show_sidebar = false;
      sidebar_hidden_for_width_ = true;
      if (focus_state == FOCUS_SIDEBAR)
      {
        focus_state = FOCUS_EDITOR;
      }
    }
    else if (!show_sidebar && sidebar_hidden_for_width_ && !too_narrow)
    {
      show_sidebar = true;
      sidebar_hidden_for_width_ = false;
    }
  }
  ui->reset_cursor_state();
  sync_lua_ui_surfaces();

  if (!needs_redraw)
  {
    if (show_home_menu)
    {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }

    // The minibuffer owns the caret while it is up, and nothing else may place
    // one: the row it lives on is the status line, not the buffer's text.
    if (show_minibuffer)
    {
      place_minibuffer_cursor();
      ui->flush_cursor();
      return;
    }

    // Keep cursor visibility in sync even when no redraw is needed.
    if (show_menu_bar_dropdown || show_context_menu || show_quick_pick
        || show_tree_sitter_status_modal || show_lsp_status_modal || show_settings_menu)
    {
      if (show_settings_menu)
      {
        place_settings_cursor();
      }
      else
      {
        ui->hide_cursor();
      }
      ui->flush_cursor();
      return;
    }

    // show_rename_prompt belongs in the guard as much as its two siblings:
    // without it a focused rename prompt fell through to the editor's own caret
    // placement below and drew a second cursor in the buffer.
    if (show_command_palette || search.visible() || show_save_prompt || show_rename_prompt
        || show_quit_prompt)
    {
      if (show_command_palette)
      {
        place_command_palette_cursor();
      }
      else if (search.visible())
      {
        search.place_cursor();
      }
      else if (show_save_prompt)
      {
        place_save_prompt_cursor();
      }
      else if (show_rename_prompt)
      {
        place_rename_prompt_cursor();
      }
      else
      {
        ui->hide_cursor();
      }
      ui->flush_cursor();
      return;
    }
    if (show_floating_terminal)
    {
      place_floating_terminal_cursor();
      ui->flush_cursor();
      return;
    }
    if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
    {
      place_integrated_terminal_cursor();
      ui->flush_cursor();
      return;
    }
    if (show_right_panel)
    {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }
    if (show_sidebar && focus_state == FOCUS_SIDEBAR)
    {
      ui->hide_cursor();
      ui->flush_cursor();
      return;
    }
    if (!telescope.is_active() && !panes.empty())
    {
      auto &pane = get_pane();
      auto &buf = get_buffer(pane.buffer_id);
      int display_x = 0;
      int display_y = 0;
      // An image tab shows a picture and no text: there is no caret to place,
      // and the cell the text cursor would occupy is on the picture.
      if (image_viewer.is_image_file(buf.filepath))
      {
        ui->hide_cursor();
      }
      // Otherwise the caret is only drawn where it belongs: when its line is
      // scrolled out of view (or the pane is too narrow to hold it) it is
      // hidden. Parking it on the last row instead made it travel to and ride
      // the bottom edge, which read as the caret being dragged along by the
      // scroll.
      else if (compute_code_cursor_screen_pos(pane,
                                         buf,
                                         show_minimap,
                                         minimap_width,
                                         tab_size,
                                         lsp_inlay_hint_cells_before(buf.filepath,
                                                                     buf.cursor.y,
                                                                     buf.cursor.x,
                                                                     buf.line(buf.cursor.y)),
                                         display_x,
                                         display_y))
      {
        ui->set_cursor(display_x,
                       display_y,
                       editor_cursor_shape(config.get("cursor_style", "block")));
      }
      else
      {
        ui->hide_cursor();
      }
      ui->set_cursor_blink_visible(blink_visible);
      ui->flush_cursor();
    }
    return;
  }

  ui->clear();

  if (show_home_menu)
  {
    render_home_menu();
    if (kTopBarVisible)
    {
      render_menu_bar();
      render_menu_dropdown();
    }
    // No workspace strip here: the home screen owns the frame, and
    // tabline_shown() answers false while it is up.
    render_status_line();
    // The home screen returns before the shared tail, so its prompts take the
    // scrim here (quitting from the home menu raises the same panel).
    render_prompt_modal();
    if (lua_api)
    {
      // A registered home_screen handler paints its float on this early path.
      lua_api->render_floats();
    }
    bool raw_wants_next_frame = false;
    if (lua_api)
    {
      raw_wants_next_frame = lua_api->run_paint_hook();
    }
    ui->hide_cursor();
    ui->render();
    needs_redraw = frame_needs_repaint() || raw_wants_next_frame;
    return;
  }

  if (kTopBarVisible)
  {
    render_menu_bar();
  }
  // The workspace strip owns row 0; the panes lay out below it.
  render_tabline();
  update_pane_layout();

  if (telescope.is_active())
  {
    if (show_sidebar && !terminal_zoom_active)
    {
      render_sidebar();
    }
    render_panes();
    if (!terminal_zoom_active)
    {
      render_collapsed_sidebar_handle();
    }
    render_lsp_completion();
    render_lsp_signature();
    render_integrated_terminal();
    if (!terminal_zoom_active)
    {
      render_debugger_panel();
      render_git_panel();
      render_git_diff_panel();
      render_outline_panel();
      render_plugin_panel();
      render_tree_sitter_playground_panel();
    }
    render_status_line();
    ui->dim_rect({0, 0, ui->get_render_width(), ui->get_height()});
    // The picker is a modal: the chrome floats (sidebar, side panel, status
    // line) are painted first and stay under the scrim, the picker paints over
    // them -- natively below, or through its own float when a registered Lua
    // handler renders it -- and only the floats above the modal layer (toasts)
    // paint after. Two layers rather than one pass because the chrome floats are
    // recreated every frame: painted in a single pass after the picker's float
    // they would out-rank it by creation order and repaint their rectangles over
    // the panel (the picker's left box used to vanish behind the explorer).
    if (lua_api)
    {
      lua_api->begin_float_pass();
      lua_api->render_float_layer(std::numeric_limits<int>::min(),
                                  LuaAPI::kModalFloatZindex - 1);
    }
    render_telescope();
    // A Lua handler that consumed the native render opened the picker's float
    // during render_telescope(); this pass paints it, above the chrome.
    if (lua_api)
    {
      lua_api->render_float_layer(LuaAPI::kModalFloatZindex,
                                  std::numeric_limits<int>::max());
    }
    if (telescope.focus() != TelescopeFocus::Query)
    {
      ui->hide_cursor();
    }
    // jot.screen.on_paint paints over the picker too: a raw painter is the
    // outermost layer on purpose, so a script can annotate or replace any
    // surface without the editor having to know about it.
    bool raw_wants_next_frame = false;
    if (lua_api)
    {
      raw_wants_next_frame = lua_api->run_paint_hook();
    }
    ui->render();
    needs_redraw = frame_needs_repaint() || raw_wants_next_frame;
    return;
  }
  else
  {
    // The chrome and the panes paint under the save / rename / quit prompt too
    // -- those are modal *panels*, and the screen behind one stays on it, dimmed
    // (render_prompt_modal, further down, once the status line is on the grid).
    // This used to be one branch per prompt that painted nothing else, so the
    // buffer the prompt was asking about vanished behind it.
    if (show_sidebar && !terminal_zoom_active)
    {
      render_sidebar();
    }
    render_panes();
    if (!terminal_zoom_active)
    {
      render_collapsed_sidebar_handle();
    }
    render_lsp_completion();
    render_lsp_signature();
    render_integrated_terminal();
    if (!terminal_zoom_active)
    {
      render_debugger_panel();
      render_git_panel();
      render_git_diff_panel();
      render_outline_panel();
      render_plugin_panel();
      render_tree_sitter_playground_panel();
    }

    // The picture is drawn inside its pane, so the viewer is only "open" while
    // a pane actually shows it. Closing it here is what emits the delete for a
    // terminal-side placement (kitty / sixel): without it the image stayed
    // painted over whatever replaced it.
    bool image_on_screen = false;
    for (size_t i = 0; i < panes.size(); i++)
    {
      if (pane_zoom_active && (int)i != current_pane)
      {
        continue;
      }
      const int id = panes[i].buffer_id;
      if (id >= 0 && id < (int)buffers.size()
          && image_viewer.is_image_file(buffers[(size_t)id].filepath))
      {
        image_on_screen = true;
        break;
      }
    }
    if (!image_on_screen && image_viewer.is_active())
    {
      image_viewer.close();
    }
    // Graphics ride with the cells: taken once per frame, after the panes have
    // set the viewer's geometry for this draw.
    ui->set_frame_graphics(image_viewer.take_graphics_output());

    render_status_line();
    render_command_palette();
    render_quick_pick();
    render_settings_menu();
    search.render_panel();
    render_tree_sitter_status_modal();
    render_lsp_status_modal();
    render_context_menu();
    // The winbar's own drop-down rides above the panes with the other menus.
    render_winbar_menu();
    if (kTopBarVisible)
    {
      render_menu_dropdown();
    }

    if (easter_egg_timer > 0)
    {
      render_easter_egg();
      easter_egg_timer--;
      needs_redraw = true;
    }

    if (popup.visible)
    {
      if (popup.presentation == POPUP_MODAL)
      {
        ui->dim_rect({0, 0, ui->get_render_width(), ui->get_height()});
      }
      render_popup();
    }

    // Last before the floats: the save / rename / quit panel. The scrim lands
    // on a frame that is fully painted (status line included) and the panel's
    // own float, painted by the pass below, is the one rect the float pass does
    // not re-dim.
    render_prompt_modal();

    if (lua_api)
    {
      lua_api->render_floats();
    }
    // Paint the floating terminal after chrome floats. It occupies only the
    // active pane's text rows, so pane-owned tab/window chrome cannot erase its
    // frame or corners after it is drawn.
    render_floating_terminal();

    // The status-line prompt is painted last of all: it is the only thing on
    // screen that has to be readable whatever else is there, the Lua kit's own
    // status row included.
    render_minibuffer();

    // Set cursor state BEFORE ui->render() so the full-row paint emits the
    // correct cursor at the end of the frame.
    if (show_minibuffer)
    {
      place_minibuffer_cursor();
    }
    else if ((popup.visible && popup.presentation == POPUP_MODAL) || show_menu_bar_dropdown
             || show_context_menu || show_quick_pick || show_tree_sitter_status_modal
             || show_lsp_status_modal)
    {
      ui->hide_cursor();
    }
    else if (show_settings_menu)
    {
      place_settings_cursor();
    }
    else if (show_command_palette || search.visible() || show_save_prompt || show_quit_prompt)
    {
      if (show_command_palette)
      {
        place_command_palette_cursor();
      }
      else if (search.visible())
      {
        search.place_cursor();
      }
      else if (show_save_prompt)
      {
        place_save_prompt_cursor();
      }
      else if (show_rename_prompt)
      {
        place_rename_prompt_cursor();
      }
      else
      {
        ui->hide_cursor();
      }
    }
    else if (show_floating_terminal)
    {
      place_floating_terminal_cursor();
    }
    else if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
    {
      place_integrated_terminal_cursor();
    }
    else if (show_sidebar && focus_state == FOCUS_SIDEBAR)
    {
      ui->hide_cursor();
    }
    else if (!telescope.is_active())
    {
      if (!panes.empty())
      {
        auto &pane = get_pane();
        auto &buf = get_buffer(pane.buffer_id);
        int display_x = 0;
        int display_y = 0;
        // Same policy as the idle path above: an image tab has no caret to
        // place, and a caret whose line is not on screen is hidden rather than
        // parked on the last code row.
        if (image_viewer.is_image_file(buf.filepath))
        {
          ui->hide_cursor();
        }
        else if (compute_code_cursor_screen_pos(pane,
                                           buf,
                                           show_minimap,
                                           minimap_width,
                                           tab_size,
                                           lsp_inlay_hint_cells_before(buf.filepath,
                                                                       buf.cursor.y,
                                                                       buf.cursor.x,
                                                                       buf.line(buf.cursor.y)),
                                           display_x,
                                           display_y))
        {
          ui->set_cursor(display_x,
                         display_y,
                         editor_cursor_shape(config.get("cursor_style", "block")));
        }
        else
        {
          ui->hide_cursor();
        }
      }
    }

    ui->set_cursor_blink_visible(blink_visible);
    // The panes have stamped everything they drew, so this is where a tree a
    // file no longer on screen is holding gets released (see
    // evict_stale_syntax_trees). Before ui->render() so the sweep is part of
    // the same frame that fed it, never racing the next repaint.
#ifdef JOT_TREESITTER
    // An edit batch that announced itself before mutating (see ts_begin_edit)
    // is finished by now, so a snapshot that still matches the buffer means the
    // tree is in sync again; asked before the panes highlight so the per-line
    // highlight path never sees a pending snapshot.
    confirm_pending_syntax_edits();
    evict_stale_syntax_trees();
#endif
    // Last: the jot.screen painter runs once the editor has drawn everything it
    // knows about, and every cell it writes goes into the grid ui->render() is
    // about to diff out. A painter that returned true asked for another frame
    // (the animation loop); the editor idles again when it stops asking.
    bool raw_wants_next_frame = false;
    if (lua_api)
    {
      raw_wants_next_frame = lua_api->run_paint_hook();
    }
    ui->render();
    needs_redraw = frame_needs_repaint() || raw_wants_next_frame;
  }
}

void Editor::render_panes()
{
  // One surface for every pane's breadcrumb row, emitted before the first pane
  // paints: the painter has to see the whole split to know which rows this
  // frame has and which floats to close.
  emit_winbar_rows();
  for (size_t i = 0; i < panes.size(); i++)
  {
    // While a pane is zoomed only it is drawn; the hidden panes stay parked
    // off-screen until the zoom is toggled off.
    if (pane_zoom_active && (int)i != current_pane)
    {
      continue;
    }
    render_pane(panes[i], (int)i);
  }
  if (!pane_zoom_active)
  {
    render_pane_resize_guides();
  }
  // Last, so the drop hint reads over every pane's paint.
  render_pane_tab_drag_hint();
}

void Editor::render_pane_resize_guides()
{
  if (!pane_resize_dragging || pane_resize_node < 0 || pane_resize_node >= (int)pane_tree.size())
  {
    return;
  }

  const PaneArea area = compute_pane_area();

  std::function<void(int, int, int, int, int)> draw_node =
      [&](int node_index, int x, int y, int w, int h)
  {
    if (node_index < 0 || node_index >= (int)pane_tree.size() || w <= 1 || h <= 1)
    {
      return;
    }
    const PaneTreeNode &node = pane_tree[node_index];
    if (node.leaf)
    {
      return;
    }

    float ratio = std::clamp(node.ratio, 0.1f, 0.9f);
    if (node.vertical)
    {
      int first_w = std::max(1, (int)(w * ratio));
      if (w >= 2)
      {
        first_w = std::min(first_w, w - 1);
      }
      if (node_index == pane_resize_node)
      {
        int bx = x + first_w - 1;
        for (int row = y; row < y + h; row++)
        {
          ui->draw_text(bx, row, "│", theme.fg_active_border, theme.bg_active_border, true);
        }
      }
      int second_w = std::max(1, w - first_w);
      draw_node(node.first, x, y, first_w, h);
      draw_node(node.second, x + first_w, y, second_w, h);
    }
    else
    {
      int first_h = std::max(1, (int)(h * ratio));
      if (h >= 2)
      {
        first_h = std::min(first_h, h - 1);
      }
      if (node_index == pane_resize_node)
      {
        int by = y + first_h - 1;
        for (int col = x; col < x + w; col++)
        {
          ui->draw_text(col, by, "─", theme.fg_active_border, theme.bg_active_border, true);
        }
      }
      int second_h = std::max(1, h - first_h);
      draw_node(node.first, x, y, w, first_h);
      draw_node(node.second, x, y + first_h, w, second_h);
    }
  };

  draw_node(pane_root, area.x, area.y, area.w, area.h);
}

int Editor::find_local_tab_index(const SplitPane &pane, int buffer_id) const
{
  for (int i = 0; i < (int)pane.tab_buffer_ids.size(); i++)
  {
    if (pane.tab_buffer_ids[i] == buffer_id)
    {
      return i;
    }
  }
  return -1;
}

bool Editor::switch_to_local_tab(int target_index)
{
  auto &pane = get_pane();
  if (pane.tab_buffer_ids.empty())
  {
    return false;
  }
  target_index = std::clamp(target_index, 0, (int)pane.tab_buffer_ids.size() - 1);
  int buffer_id = pane.tab_buffer_ids[target_index];
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    return false;
  }
  // A tab no strip draws (the compilation log) is not one the keyboard lands
  // on either: Alt+<digit> and Ctrl+Tab walk the tabs that are on screen.
  if (!pane_tab_shown(buffer_id))
  {
    return false;
  }
  capture_pane_view(current_pane);
  pane.buffer_id = buffer_id;
  current_buffer = buffer_id;
  restore_pane_view(current_pane);
  focus_state = FOCUS_EDITOR;
  clamp_cursor(buffer_id);
  ensure_cursor_visible();

  // The workspace strip scrolls to keep the pane's new buffer visible; the
  // pane itself has no strip left to reveal into.
  reveal_tabline_position(tab_order.position_of(buffers, buffer_id));
  needs_redraw = true;
  return true;
}

bool Editor::cycle_local_tab(int delta)
{
  auto &pane = get_pane();
  if (pane.tab_buffer_ids.size() <= 1)
  {
    return false;
  }
  int current_idx = find_local_tab_index(pane, pane.buffer_id);
  if (current_idx < 0)
  {
    current_idx = 0;
  }
  const int n = (int)pane.tab_buffer_ids.size();
  int next_idx = current_idx;
  // Step until a drawn tab: a pane that also holds the compilation log must not
  // cycle into a tab its own strip leaves out.
  for (int step = 0; step < n; step++)
  {
    next_idx = ((next_idx + delta) % n + n) % n;
    if (pane_tab_shown(pane.tab_buffer_ids[(size_t)next_idx]))
    {
      break;
    }
  }
  if (next_idx == current_idx)
  {
    return false;
  }
  return switch_to_local_tab(next_idx);
}

std::vector<UIRect> Editor::pane_neighbours(const SplitPane &pane, int draw_w) const
{
  std::vector<UIRect> out;
  if (!ui)
  {
    return out;
  }

  const int total_w = std::max(1, ui->get_render_width());
  const int total_h = std::max(1, ui->get_height());

  // The pane area stops above the status line and any bottom panel; the sidebar
  // and dock rects pushed below are their full-height columns instead -- they
  // start on the row the strip leaves them (side_column_top), one row above the
  // panes while the strip is up.
  const int bottom_h = status_height + integrated_terminal_reserved_h();
  const int area_y = side_column_top();
  const int area_h = std::max(1, total_h - bottom_h - area_y);

  for (size_t i = 0; i < panes.size(); i++)
  {
    if (&panes[i] == &pane)
    {
      continue;
    }
    // A zoomed pane covers the area alone; the others are parked off-screen.
    if (pane_zoom_active && (int)i != current_pane)
    {
      continue;
    }
    const SplitPane &other = panes[i];
    if (other.h <= 0 || other.w <= 0)
    {
      continue;
    }
    out.push_back(UIRect{other.x, other.y, other.w, other.h});
  }

  // The sidebar occupies the columns immediately left of the pane area, so a
  // separator is shared; the sidebar itself draws that line (its right edge).
  const int sidebar_w = show_sidebar ? effective_sidebar_width() : 0;
  if (sidebar_w > 0 && pane.x >= sidebar_w)
  {
    out.push_back(UIRect{0, area_y, sidebar_w, area_h});
  }

  // The right dock starts where the pane area ends.
  const int dock_w = effective_right_panel_width();
  if (dock_w > 0 && pane.x + draw_w <= total_w - dock_w)
  {
    out.push_back(UIRect{total_w - dock_w, area_y, dock_w, area_h});
  }

  // Deliberately not reported: the rows below the pane area (the integrated
  // terminal, a dock, the status line). Those regions carry a background of
  // their own -- and the dock's own rule row is inside the height it reserves,
  // above the pane area -- so a rule between them and the pane is not needed,
  // and the pane would draw it in its own last row, which is a code row. A pane
  // stacked on another pane still comes through the loop above and keeps its
  // divider: two panes share one background, so the line is the only thing
  // separating them.
  return out;
}

void Editor::render_pane(const SplitPane &pane, int pane_index)
{
  int draw_w = std::max(1, pane.w);
  if (pane.h <= 0)
    return;

#ifdef JOT_TREESITTER
  // This buffer is on screen this frame, so its parse tree is worth keeping:
  // the stamp is the recency evict_stale_syntax_trees() orders by, and it also
  // protects the pane from being evicted on a frame that draws it.
  if (pane.buffer_id >= 0 && pane.buffer_id < (int)buffers.size())
  {
    buffers[(size_t)pane.buffer_id].ts_last_use_seq = ++ts_use_seq_;
  }
#endif

  // The pane's own tab strip, when it has one: its first chrome row, above the
  // winbar and the text (pane_tabs=auto gives the row to split panes and to
  // panes whose workspace strip is off; a lone pane leaves the tabs to row 0).
  render_pane_tab_strip(pane_index);

  // An inactive pane draws from its own remembered view (cursor, scroll,
  // selection) so two panes sharing one buffer can be scrolled and edited at
  // independent positions. The live buffer fields belong to the active pane;
  // they are swapped out for this pane's view for the duration of the draw
  // and restored afterwards.
  FileBuffer *view_override = nullptr;
  Cursor save_cursor{0, 0};
  int save_preferred_x = 0;
  Selection save_selection{{0, 0}, {0, 0}, false};
  int save_scroll_offset = 0;
  int save_scroll_x = 0;
  if (!pane.active && pane.view_buffer_id == pane.buffer_id && pane.buffer_id >= 0
      && pane.buffer_id < (int)buffers.size())
  {
    FileBuffer &buf = buffers[(size_t)pane.buffer_id];
    view_override = &buf;
    save_cursor = buf.cursor;
    save_preferred_x = buf.preferred_x;
    save_selection = buf.selection;
    save_scroll_offset = buf.scroll_offset;
    save_scroll_x = buf.scroll_x;
    buf.cursor = pane.view_cursor;
    buf.preferred_x = pane.view_preferred_x;
    buf.selection = pane.view_selection;
    buf.scroll_offset = pane.view_scroll_offset;
    buf.scroll_x = pane.view_scroll_x;
  }

  if (show_minimap && draw_w > 20)
  {
    draw_w = std::max(1, draw_w - minimap_width);
  }

  // The pane's breadcrumb row, when its buffer has one: the pane's own chrome,
  // drawn before the text because the text starts below it.
  render_winbar(pane, pane_index);

  // An image tab draws the picture inside the pane's own frame, the way any
  // other file draws its contents, instead of as a window-wide overlay.
  const std::string pane_path =
      (pane.buffer_id >= 0 && pane.buffer_id < (int)buffers.size())
          ? buffers[(size_t)pane.buffer_id].filepath
          : std::string();
  if (image_viewer.is_image_file(pane_path))
  {
    // The pane owns the picture now, so it keeps the viewer pointed at the
    // image it is showing: a closed or stale viewer reports no geometry and
    // draws nothing, which is the black panel.
    if (!image_viewer.is_active() || image_viewer.get_current() != pane_path)
    {
      image_viewer.open(pane_path);
    }
    render_image_viewer(pane);
  }
  else
  {
    render_buffer_content(pane, pane_index, pane.buffer_id);
  }

  if (show_minimap && pane.w > 20)
  {
    render_minimap(pane.x + draw_w, pane.y + 1, minimap_width, pane.h - 1, pane.buffer_id);
  }

  UIRect rect = {pane.x, pane.y, draw_w, pane.h};
  // Deliberately dim: the pane frame uses the quiet panel border color for
  // both focused and unfocused panes (focus is shown by the cursor and the
  // active tab marker instead of a loud border).
  //
  // Only the sides facing another region get ink: a separator belongs to the
  // region on its left/top, so two adjacent regions never draw two lines, and a
  // lone pane draws no frame at all (see pane_edges.h).
  //
  // The bottom rule keeps the pane's own background, like the other sides. It
  // used to borrow the status line's so the two would read as one band of
  // chrome, but that row is the pane's own last row: in the status colour it
  // made the two-row status line look three rows tall and hid the fact that the
  // row was the pane's.
  // While the dock is being resized, the separator it shares with this pane
  // lights up the way the sidebar's border does (sidebar.cpp).
  const int dock_w = effective_right_panel_width();
  const int render_w = ui ? ui->get_render_width() : 0;
  const bool dock_edge = right_panel_resize_dragging && dock_w > 0
                         && pane.x + draw_w >= render_w - dock_w;
  ui->draw_border(rect,
                  dock_edge ? theme.fg_active_border : theme.fg_panel_border,
                  theme.bg_panel_border,
                  pane_layout::border_edges(rect, pane_neighbours(pane, draw_w)));

  // The pane draws no tab strip of its own: the workspace strip owns row 0
  // (render_tabline), and this pane's text starts at pane_content_top.

  if (view_override != nullptr)
  {
    view_override->cursor = save_cursor;
    view_override->preferred_x = save_preferred_x;
    view_override->selection = save_selection;
    view_override->scroll_offset = save_scroll_offset;
    view_override->scroll_x = save_scroll_x;
  }
}
