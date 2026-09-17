// Right-click context menu: hit-testing the file tree / sidebar rows
// and opening the menu at the click position.
#include "editor.h"
#include "column_utils.h"
#include "folding.h"
#include "input/mouse/mouse_internal.h"

using namespace mouse_internal;
#include <algorithm>
#include <string>
#include <vector>

namespace
{
static bool cursor_in_selection(const Selection &selection, const Cursor &pos)
{
  if (!selection.active)
    return false;
  Cursor start = selection.start;
  Cursor end = selection.end;
  if (compare_cursor_pos(end, start) < 0)
    std::swap(start, end);
  return compare_cursor_pos(start, pos) <= 0 && compare_cursor_pos(pos, end) <= 0;
}

static void flatten_nodes_for_mouse(std::vector<FileNode> &nodes, std::vector<FileNode *> &flat)
{
  for (auto &node : nodes)
  {
    flat.push_back(&node);
    if (node.is_dir && node.expanded)
    {
      flatten_nodes_for_mouse(node.children, flat);
    }
  }
}
} // namespace

bool Editor::open_context_menu_for_mouse(int x, int y)
{
  if (show_home_menu || panes.empty())
  {
    return false;
  }

  context_menu_target_buffer = -1;
  context_menu_target_pane = -1;
  context_menu_target_terminal = -1;
  context_menu_target_line = -1;
  context_menu_target_path.clear();
  context_menu_target_is_dir = false;

  if (show_integrated_terminal && !integrated_terminals.empty())
  {
    int panel_h = integrated_terminal_panel_h();
    int panel_y = integrated_terminal_panel_y();
    int panel_w = integrated_terminal_panel_w();
    int tab_y = panel_y + 1;
    if (x >= 0 && x < panel_w && y >= panel_y && y < panel_y + panel_h)
    {
      int target_terminal = current_integrated_terminal;
      bool on_tab = (y == tab_y || y == panel_y);
      if (on_tab)
      {
        int tab_x = 1;
        for (int i = 0; i < (int)integrated_terminals.size(); i++)
        {
          std::string base_label = integrated_terminals[i]->get_label().empty()
                                       ? "term " + std::to_string(i + 1)
                                       : integrated_terminals[i]->get_label();
          std::string label = " " + base_label + " ";
          int tab_w = (int)label.size() + 2;
          if (x >= tab_x && x < tab_x + tab_w)
          {
            target_terminal = i;
            break;
          }
          tab_x += tab_w;
          if (tab_x >= panel_w - 4)
            break;
        }
      }
      context_menu_target_terminal = target_terminal;
      std::vector<ContextMenuItem> items = {
          {"Focus Terminal", CONTEXT_ACTION_TERMINAL_FOCUS, target_terminal >= 0},
          {"New Terminal", CONTEXT_ACTION_TERMINAL_NEW, true},
          {"Reset Scroll", CONTEXT_ACTION_TERMINAL_RESET_SCROLL, target_terminal >= 0},
      };
      if (on_tab)
      {
        items.push_back({"Close Terminal", CONTEXT_ACTION_TERMINAL_CLOSE, target_terminal >= 0});
      }
      open_context_menu(x, y, CONTEXT_MENU_TERMINAL, items);
      return true;
    }
  }

  if (show_sidebar)
  {
    const ContentColumn col = content_column();
    int sidebar_w = effective_sidebar_width();
    if (x < sidebar_w && y >= col.top && y < col.bottom)
    {
      focus_state = FOCUS_SIDEBAR;
      if (!explorer_only() && active_sidebar_view == SIDEBAR_VIEW_GIT)
      {
        std::vector<GitSidebarRow> git_rows = build_git_sidebar_rows();
        int sidebar_row = y - col.top - 1;
        int row = sidebar_row + git_sidebar_scroll;
        if (sidebar_row >= 0 && row >= 0 && row < (int)git_rows.size())
        {
          git_sidebar_selected = row;
          context_menu_target_path = git_rows[(size_t)row].path;
          context_menu_target_is_dir = false;
        }
        bool has_target = !context_menu_target_path.empty();
        bool has_repo = has_git_repo();
        std::vector<ContextMenuItem> items = {
            {"Open", CONTEXT_ACTION_SIDEBAR_OPEN, has_target},
            {"Git Stage", CONTEXT_ACTION_GIT_STAGE, has_target && has_repo},
            {"Git Unstage", CONTEXT_ACTION_GIT_UNSTAGE, has_target && has_repo},
            {"Git Diff", CONTEXT_ACTION_GIT_DIFF, has_target && has_repo},
            {"Git Diff Staged", CONTEXT_ACTION_GIT_DIFF_STAGED, has_target && has_repo},
            {"Git Stage All", CONTEXT_ACTION_GIT_STAGE_ALL, has_repo},
            {"Refresh", CONTEXT_ACTION_GIT_REFRESH, true},
            {"Copy Path", CONTEXT_ACTION_SIDEBAR_COPY_PATH, has_target},
        };
        open_context_menu(x, y, CONTEXT_MENU_SIDEBAR, items);
      }
      else
      {
        std::vector<FileNode *> flat;
        flatten_nodes_for_mouse(file_tree, flat);
        int sidebar_row = y - topbar_height() - 1;
        int row = sidebar_row + file_tree_scroll;
        FileNode *node = nullptr;
        if (sidebar_row >= 0 && row >= 0 && row < (int)flat.size())
        {
          node = flat[row];
          file_tree_selected = row;
          context_menu_target_path = node->path;
          context_menu_target_is_dir = node->is_dir;
        }
        else
        {
          context_menu_target_path = root_dir;
          context_menu_target_is_dir = true;
        }
        bool has_target = !context_menu_target_path.empty();
        std::string open_label =
            node && node->is_dir ? (node->expanded ? "Collapse" : "Expand") : "Open";
        bool git_target = has_target && has_git_repo();
        std::vector<ContextMenuItem> items = {
            {open_label, CONTEXT_ACTION_SIDEBAR_OPEN, node != nullptr},
            {"New File", CONTEXT_ACTION_SIDEBAR_NEW_FILE, has_target},
            {"New Folder", CONTEXT_ACTION_SIDEBAR_NEW_FOLDER, has_target},
            {"Rename", CONTEXT_ACTION_SIDEBAR_RENAME, node != nullptr},
            {"Git Stage", CONTEXT_ACTION_GIT_STAGE, git_target},
            {"Git Unstage", CONTEXT_ACTION_GIT_UNSTAGE, git_target},
            {"Git Diff", CONTEXT_ACTION_GIT_DIFF, git_target},
            {"Git Diff Staged", CONTEXT_ACTION_GIT_DIFF_STAGED, git_target},
            {"Refresh", CONTEXT_ACTION_SIDEBAR_REFRESH, true},
            {"Copy Path", CONTEXT_ACTION_SIDEBAR_COPY_PATH, has_target},
        };
        open_context_menu(x, y, CONTEXT_MENU_SIDEBAR, items);
      }
      return true;
    }
  }

  int pane_index = -1;
  for (int i = 0; i < (int)panes.size(); i++)
  {
    const auto &candidate = panes[i];
    if (x >= candidate.x && x < candidate.x + candidate.w && y >= candidate.y
        && y < candidate.y + candidate.h)
    {
      pane_index = i;
      break;
    }
  }
  if (pane_index < 0)
  {
    return false;
  }

  if (pane_index != current_pane)
  {
    activate_pane(pane_index);
  }
  auto &pane = get_pane(current_pane);
  auto &buf = get_buffer(pane.buffer_id);
  context_menu_target_pane = current_pane;

  if (y == pane.y && !buffers.empty())
  {
    int draw_w = std::max(1, pane.w);
    if (show_minimap && draw_w > 20)
    {
      draw_w = std::max(1, draw_w - minimap_width);
    }
    FileTabLayout tabs = build_file_tab_layout(pane, draw_w);
    for (const auto &tab : tabs.segments)
    {
      if (x >= tab.x && x < tab.end_x)
      {
        context_menu_target_buffer = tab.buffer_id;
        std::vector<ContextMenuItem> items = {
            {"Save", CONTEXT_ACTION_SAVE_BUFFER, tab.buffer_id >= 0},
            {"Close Buffer", CONTEXT_ACTION_CLOSE_BUFFER, tab.buffer_id >= 0},
        };
        open_context_menu(x, y, CONTEXT_MENU_TAB, items);
        return true;
      }
    }
  }

  const int line_num_width = 7;
  const int code_start_x = pane.x + 1 + line_num_width;
  const int content_top = pane.y + tab_height;
  const int visible_rows = std::max(1, pane.h - tab_height - 1);
  if (y >= content_top && y < content_top + visible_rows)
  {
    int rel_y = std::clamp(y - content_top, 0, visible_rows - 1);
    refresh_folds(buf);
    int click_y = buffer_line_for_visible_row(buf, buf.scroll_offset, rel_y);
    if (click_y >= 0 && click_y < (int)buf.line_count())
    {
      const std::string &clicked_line = buf.line(click_y);
      int rel_visual_x = std::max(0, x - code_start_x);
      int start_visual = compute_visual_column(clicked_line, buf.scroll_x, tab_size);
      int click_visual = start_visual + rel_visual_x;
      // Inlay hints insert cells on screen; subtract them so the click lands
      // on the logical column the user actually sees. Compare against the
      // ORIGINAL click column (hint positions live in rendered space).
      const int raw_click_visual = click_visual;
      for (const auto &hw : lsp_inlay_hints_visual(buf.filepath, click_y, clicked_line, tab_size))
      {
        if (hw.first <= raw_click_visual)
        {
          click_visual -= hw.second;
        }
      }
      int click_x = visual_to_logical_column(clicked_line, click_visual, tab_size);
      click_x = std::clamp(click_x, 0, (int)clicked_line.length());
      Cursor clicked = {click_x, click_y};
      if (!cursor_in_selection(buf.selection, clicked))
      {
        buf.cursor = clicked;
        buf.preferred_x = buf.cursor.x;
        buf.selection = {clicked, clicked, false};
        ensure_cursor_visible();
      }
      context_menu_target_buffer = pane.buffer_id;
      context_menu_target_line = click_y;
      std::vector<ContextMenuItem> items = {
          {"Copy", CONTEXT_ACTION_COPY, true},
          {"Cut", CONTEXT_ACTION_CUT, true},
          {"Paste", CONTEXT_ACTION_PASTE, !clipboard.empty()},
      };
      if (Folding::fold_at_or_before_line(buf.fold_ranges, click_y) >= 0)
      {
        items.push_back({"Toggle Fold", CONTEXT_ACTION_TOGGLE_FOLD, true});
      }
      focus_state = FOCUS_EDITOR;
      open_context_menu(x, y, CONTEXT_MENU_EDITOR, items);
      return true;
    }
  }

  return false;
}

