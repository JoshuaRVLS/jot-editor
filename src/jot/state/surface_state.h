#ifndef JOT_STATE_SURFACE_STATE_H
#define JOT_STATE_SURFACE_STATE_H

#include "jot/model/input.h"     // ContextMenuItem, ContextMenuSurface
#include "jot/model/menu.h"      // MenuBarSegment, HomeMenuEntry
#include "jot/model/popup.h"     // Popup
#include "jot/model/quick_pick.h" // CommandPaletteSuggestion, QuickPickKind, QuickPickItem, SettingsEntry
#include "jot/model/tasks.h"     // TreeSitterInstallJob
#include "telescope.h"           // Telescope
#include <string>
#include <vector>

// The surfaces that float over the editor: the command palette, quick pick,
// telescope, the text prompts (save / quit / rename), which-key, the menu bar
// and home menu, the settings menu, the status modals, the popup surface, and
// the remembered visibility that tells the Lua UI kit when one closed.
struct SurfaceState
{
  bool show_command_palette = false;
  // Scroll anchor of the palette grid: the first visible row of every column.
  // The window only moves when the selection leaves it, so a hover over a
  // visible row never shifts the grid under the pointer.
  int command_palette_scroll = 0;
  std::string command_palette_query;
  // Query text remembered when the palette closes with Esc, restored on the
  // next Ctrl+P open; cleared when the palette closes with an empty input.
  std::string command_palette_last_query;
  std::vector<CommandPaletteSuggestion> command_palette_results;
  int command_palette_selected = 0;
  bool command_palette_theme_mode = false;
  std::string command_palette_theme_original;

  QuickPickKind quick_pick_kind = QUICK_PICK_NONE;
  bool show_quick_pick = false;
  // Scroll anchor of the visible quick-pick window (follow-window, same
  // contract as command_palette_scroll).
  int quick_pick_scroll = 0;
  std::string quick_pick_title;
  std::string quick_pick_query;
  std::vector<QuickPickItem> quick_pick_all_items;
  std::vector<QuickPickItem> quick_pick_items;
  int quick_pick_selected = 0;

  Telescope telescope;

  bool show_save_prompt = false;
  std::string save_prompt_input;
  bool show_quit_prompt = false;
  // Interactive LSP rename: seeded with the identifier under the cursor, commits
  // through lsp_rename_symbol. The flag keeps the prompt off a blank editor.
  bool show_rename_prompt = false;
  std::string rename_prompt_input;

  // Which-key style keybind helper, opened when a chord is a prefix of longer
  // keymaps ("Ctrl+T" when "Ctrl+T N" exists). which_key_path holds the chords
  // pressed so far, never empty while open.
  bool show_which_key = false;
  std::vector<std::string> which_key_path;
  int which_key_selected = 0;

  // Lua UI surface close-tracking: remembered visibility from the previous
  // frame so render() can notify handlers (fn(nil)) when a surface closes.
  bool lua_ui_prev_command_palette = false;
  bool lua_ui_prev_quick_pick = false;
  bool lua_ui_prev_popup = false;
  bool lua_ui_prev_save_prompt = false;
  bool lua_ui_prev_rename_prompt = false;
  bool lua_ui_prev_quit_prompt = false;
  bool lua_ui_prev_tree_sitter_status = false;
  bool lua_ui_prev_lsp_status = false;
  bool lua_ui_prev_telescope = false;
  bool lua_ui_prev_lsp_completion = false;
  bool lua_ui_prev_lsp_signature = false;
  bool lua_ui_prev_context_menu = false;
  bool lua_ui_prev_menu_dropdown = false;
  bool lua_ui_prev_search = false;
  bool lua_ui_prev_home = false;
  bool lua_ui_prev_sidebar = false;
  bool lua_ui_prev_side_panel = false;
  bool lua_ui_prev_settings = false;
  bool lua_ui_prev_winbar = false;
  bool lua_ui_prev_winbar_menu = false;

  bool show_menu_bar_dropdown = false;
  int menu_bar_active = 0;
  int menu_bar_selected = 0;
  std::vector<MenuBarSegment> menu_bar_segments;

  bool show_home_menu = false;
  int home_menu_selected = 0;
  int home_menu_panel_x = 0;
  int home_menu_panel_y = 0;
  int home_menu_panel_w = 0;
  int home_menu_panel_h = 0;
  std::vector<HomeMenuEntry> home_menu_entries;
  // Typed filter on the home screen: the query itself, and whether typing was
  // armed by `/` so that a letter which is also a shortcut filters instead of
  // firing.
  std::string home_filter;
  bool home_filter_armed = false;
  // The query the rows in home_menu_entries were built for, so input can tell
  // when the list it would act on is a frame behind the one on screen.
  std::string home_model_filter;

  // Cell-based settings menu (:settings, Ctrl+, in GUI mode): every config key
  // with its value, filtered by the search bar. Bools toggle, ints step and
  // edit, enums cycle, strings edit inline.
  bool show_settings_menu = false;
  // The search bar's text, and the entries it keeps (indices into
  // settings_entries). settings_selected is a position in *this* list, so
  // navigation walks matches only.
  std::string settings_query;
  std::vector<int> settings_filtered;
  int settings_selected = 0;
  // The rows actually painted: the selectable list with a section header folded
  // in wherever the group changes. settings_scroll is a row in *this* list, since
  // headers take up screen room.
  std::vector<SettingsRow> settings_rows;
  int settings_scroll = 0;
  int settings_panel_x = 0;
  int settings_panel_y = 0;
  int settings_panel_w = 0;
  int settings_panel_h = 0;
  std::vector<SettingsEntry> settings_entries;
  // The choices drop-down an Enum row opens on Enter: the cursor in it, the box
  // the render pass placed, and the window's first option once it scrolls.
  bool settings_dropdown_open = false;
  int settings_dropdown_index = 0;
  int settings_dropdown_scroll = 0;
  int settings_dropdown_x = 0;
  int settings_dropdown_y = 0;
  int settings_dropdown_w = 0;
  int settings_dropdown_h = 0;

  bool show_tree_sitter_status_modal = false;
  int tree_sitter_status_scroll = 0;
  // Hovered row inside the status modals (mouse motion), -1 when none.
  int tree_sitter_status_hover_row = -1;
  std::vector<TreeSitterInstallJob> tree_sitter_install_jobs;

  // Context menu (right click / the contextmenu command): the surface it was
  // opened for, its items and box, and what it was opened on.
  bool show_context_menu = false;
  ContextMenuSurface context_menu_surface = CONTEXT_MENU_NONE;
  std::vector<ContextMenuItem> context_menu_items;
  int context_menu_x = 0;
  int context_menu_y = 0;
  int context_menu_w = 0;
  int context_menu_h = 0;
  int context_menu_selected = 0;
  int context_menu_target_buffer = 0;
  int context_menu_target_pane = 0;
  int context_menu_target_terminal = 0;
  int context_menu_target_line = 0;
  std::string context_menu_target_path;
  bool context_menu_target_is_dir = false;

  Popup popup;
};

#endif
