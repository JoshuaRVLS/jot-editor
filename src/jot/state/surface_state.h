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
// and home menu, the settings menu, the tree-sitter status modal, the popup
// surface, and the remembered visibility that tells the Lua UI kit when one
// closed (so a handler gets its fn(nil) exactly once).
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct SurfaceState
{
  bool show_command_palette = false;
  // Scroll anchor of the visible palette window (follow-window: the selected
  // row stays visible, but the window only moves when selection leaves it,
  // so mouse hover over a visible row never shifts the list under the
  // pointer). Kept separate from the selection like quick_pick_scroll.
  int command_palette_scroll = 0;
  std::string command_palette_query;
  // Query text remembered when the palette closes with Esc; restored on the
  // next Ctrl+P open so an abandoned search can be picked up where it left
  // off. Cleared when the palette closes with an empty input.
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
  // Interactive LSP rename: the prompt is seeded with the identifier under the
  // cursor and commits through lsp_rename_symbol.
  // Uninitialised, this decides whether the rename prompt renders from
  // whatever happened to be in memory -- it showed up over a blank editor.
  bool show_rename_prompt = false;
  std::string rename_prompt_input;

  // Which-key style keybind helper. It appears automatically when the user
  // presses a chord that is a prefix of longer plugin keymap sequences (e.g.
  // "Ctrl+T" when "Ctrl+T N" / "Ctrl+T D" exist) and lists the next chord
  // options above the status line. which_key_path holds the canonical chords
  // pressed so far (e.g. {"Ctrl+T", "N"}) -- never empty while open.
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

  // Cell-based settings menu (:settings, Ctrl+, in GUI mode). Lists every
  // config key (defaults + Lua-registered) with its current value; bools
  // toggle on Enter, ints/strings edit through an inline input row.
  bool show_settings_menu = false;
  int settings_selected = 0;
  int settings_scroll = 0;
  int settings_panel_x = 0;
  int settings_panel_y = 0;
  int settings_panel_w = 0;
  int settings_panel_h = 0;
  std::vector<SettingsEntry> settings_entries;

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
