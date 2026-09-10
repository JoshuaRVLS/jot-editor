// Push native UI state into the Lua layer once per frame so Lua-rendered
// surfaces (palette, side panels, ...) always see fresh editor state.
#include "editor.h"
#include "jot/lua/api.h"
#include "ui/components.h"
#include <string>

void Editor::sync_lua_ui_surfaces()
{
  if (!lua_api)
  {
    lua_ui_prev_command_palette = false;
    lua_ui_prev_quick_pick = false;
    lua_ui_prev_popup = false;
    lua_ui_prev_save_prompt = false;
    lua_ui_prev_quit_prompt = false;
    lua_ui_prev_tree_sitter_status = false;
    lua_ui_prev_telescope = false;
    lua_ui_prev_lsp_completion = false;
    lua_ui_prev_lsp_signature = false;
    lua_ui_prev_context_menu = false;
    lua_ui_prev_menu_dropdown = false;
    lua_ui_prev_search = false;
    lua_ui_prev_home = false;
    lua_ui_prev_sidebar = false;
    lua_ui_prev_side_panel = false;
    lua_ui_prev_settings = false;
    return;
  }
  auto sync = [&](bool visible, bool &prev, const char *name)
  {
    if (prev && !visible)
    {
      lua_api->emit_lua_ui_close(name);
    }
    prev = visible;
  };
  sync(show_command_palette, lua_ui_prev_command_palette, "command_palette");
  sync(show_quick_pick, lua_ui_prev_quick_pick, "quick_pick");
  sync(popup.visible && popup.presentation == POPUP_MODAL, lua_ui_prev_popup, "popup");
  sync(show_save_prompt, lua_ui_prev_save_prompt, "save_prompt");
  sync(show_quit_prompt, lua_ui_prev_quit_prompt, "quit_prompt");
  sync(show_tree_sitter_status_modal, lua_ui_prev_tree_sitter_status, "tree_sitter_status");
  sync(show_lsp_status_modal, lua_ui_prev_lsp_status, "lsp_status");
  sync(telescope.is_active(), lua_ui_prev_telescope, "telescope");
  sync(lsp_completion_visible && !lsp_completion_items.empty(),
       lua_ui_prev_lsp_completion,
       "lsp_completion");
  sync(lsp_signature_visible && !lsp_signature_result.signatures.empty(),
       lua_ui_prev_lsp_signature,
       "lsp_signature");
  sync(show_context_menu, lua_ui_prev_context_menu, "context_menu");
  sync(show_menu_bar_dropdown, lua_ui_prev_menu_dropdown, "menu_dropdown");
  sync(show_search, lua_ui_prev_search, "search_panel");
  sync(show_home_menu, lua_ui_prev_home, "home_screen");
  // A zoomed terminal owns the whole pane area: the sidebar and right-dock
  // floats must be torn down while it is up (frame.cpp also skips their
  // native paints) so nothing paints over the fullscreen terminal.
  sync(show_sidebar && !terminal_zoom_active, lua_ui_prev_sidebar, "sidebar");
  sync(show_right_panel && !terminal_zoom_active, lua_ui_prev_side_panel, "side_panel");
  // The settings float must be torn down when the menu closes (Esc, click
  // outside, :settings toggle): without the close emit the panel stays on
  // screen even though the scrim is gone.
  sync(show_settings_menu, lua_ui_prev_settings, "settings");
}

