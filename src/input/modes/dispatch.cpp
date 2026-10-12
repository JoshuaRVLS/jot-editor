#include "core/editor.h"
#include <algorithm>
#include <cctype>

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  // Keep the cursor solid while typing: restart the blink clock and
  // suspend blinking briefly on every keypress.
  restart_blink();
  // A keypress is the editor's activity signal for the coding-time chip
  // (extras/coding_time/wiring.cpp): it is where the local total learns the user is
  // here, and it is the WakaTime plugin spec's allowed stand-in for a
  // modified-file event. It is also the only place every editing mode passes
  // through, so nothing has to remember to report itself.
  note_editing_activity(false);
  clear_debugger_breakpoint_hover();

  // The macro chords (Alt+Z q/p/l) are handled here rather than at the end with
  // the other modeless bindings so they keep working while a prompt owns the
  // keyboard: a recording has to be stoppable from wherever the user is, and
  // the prompts return before anything below them runs. A build with Lua gets
  // the same three chords from runtime/lua/features/keymaps.lua, which the
  // frontend consults before this dispatch (so a config can rebind them); this
  // is the fallback, and the Alt+Z press alone only arms the prefix.
  if (handle_macro_key(ch, is_ctrl, is_shift, is_alt))
  {
    return;
  }

  // A quit confirmation owns every answer key, regardless of which surface had
  // focus when Ctrl+Q opened it.
  if (show_quit_prompt)
  {
    if (ch == 'y' || ch == 'Y' || ch == '\n' || ch == 13)
    {
      show_quit_prompt = false;
      needs_redraw = true;
      running = false;
    }
    else if (ch == 'n' || ch == 'N' || ch == 27)
    {
      show_quit_prompt = false;
      needs_redraw = true;
      set_message("Quit cancelled");
    }
    return;
  }

  // The status-line prompt owns the keyboard while it is up: its keys are read
  // before any surface or buffer shortcut, so a stray chord cannot edit the
  // buffer underneath the line being typed on there.
  if (handle_minibuffer_input(ch, is_ctrl, is_shift, is_alt))
  {
    return;
  }

  // Terminals encode Ctrl+` inconsistently. Accept common variants:
  // - explicit Ctrl modifier + '`'/'~' (and fallback Ctrl+\ for layouts where
  //   backtick is hard to emit)
  // - control code 30 (Ctrl+^ / sometimes emitted for Ctrl+`)
  bool ctrl_backtick =
      (is_ctrl
       && (ch == '`' || original_ch == '`' || ch == '~' || original_ch == '~' || ch == '\\'
           || original_ch == '\\' || ch == '|' || original_ch == '|'))
      || ch == 28 || original_ch == 28 || ch == 30 || original_ch == 30;
  if (ctrl_backtick)
  {
    if (show_home_menu)
    {
      show_home_menu = false;
    }
    toggle_integrated_terminal();
    return;
  }

  // Fallback for terminals where Ctrl+` cannot be emitted reliably.
  bool alt_backtick = is_alt
                      && (ch == '`' || original_ch == '`' || ch == '\\' || original_ch == '\\'
                          || ch == '|' || original_ch == '|');
  if (alt_backtick)
  {
    if (show_home_menu)
    {
      show_home_menu = false;
    }
    toggle_integrated_terminal();
    return;
  }

  if (show_settings_menu)
  {
    handle_settings_input(ch);
    return;
  }

  if (show_home_menu)
  {
    if (handle_home_menu_input(ch, is_ctrl, is_shift, is_alt))
    {
      return;
    }
  }

  if (show_menu_bar_dropdown)
  {
    handle_menu_bar_input(ch);
    return;
  }

  if (show_context_menu)
  {
    handle_context_menu_input(ch);
    return;
  }

  if (handle_winbar_menu_input(ch))
  {
    return; // the winbar's drop-down owns the keyboard while it is up
  }

  if (show_tree_sitter_status_modal && handle_tree_sitter_status_input(ch))
  {
    return;
  }

  if (show_lsp_status_modal)
  {
    handle_lsp_status_input(ch);
    return;
  }

  if (show_quick_pick)
  {
    handle_quick_pick_input(ch);
    return;
  }

  const bool ctrl_q =
      (is_ctrl && (ch == 'q' || ch == 'Q' || original_ch == 'q' || original_ch == 'Q')) || ch == 17
      || original_ch == 17;
  if (ctrl_q)
  {
    if (close_active_floating_ui())
    {
      return;
    }
    if (show_right_panel)
    {
      // Close the right dock (git / debugger / outline / plugin panels)
      // instead of quitting - Ctrl+Q is the natural close-panel chord and
      // matches the panel's own q key.
      show_right_panel = false;
      active_right_panel_tab = RIGHT_PANEL_DEBUG;
      active_plugin_panel.clear();
      git_panel.pending_confirm.clear();
      git_panel.return_after_diff = false;
      focus_state = FOCUS_EDITOR;
      set_message("Right panel closed");
      needs_redraw = true;
      return;
    }
    if (panes.size() > 1)
    {
      close_pane();
    }
    else
    {
      bool unsaved = false;
      for (const auto &b : buffers)
      {
        if (b.modified)
        {
          unsaved = true;
          break;
        }
      }
      if (unsaved)
      {
        show_quit_prompt = true;
        needs_redraw = true;
      }
      else
      {
        running = false;
      }
    }
    return;
  }

  // Global dock toggles, VS Code's chords:
  //   Ctrl+B      primary sidebar (left dock)
  //   Ctrl+Alt+B  secondary sidebar (right dock); Ctrl+Shift+B stays as an alias
  //   Ctrl+J      bottom panel
  // They run before the focused integrated terminal sees the key so they keep
  // working while the shell owns focus -- the same commandsToSkipShell
  // behaviour VS Code gives its panel toggles.
  if (is_ctrl && (ch == 'b' || ch == 'B' || original_ch == 'b' || original_ch == 'B'))
  {
    if (is_alt || is_shift || ch == 'B' || original_ch == 'B')
    {
      toggle_right_panel();
    }
    else
    {
      toggle_sidebar();
    }
    return;
  }
  if (is_ctrl && (ch == 'j' || ch == 'J' || original_ch == 'j' || original_ch == 'J'))
  {
    toggle_bottom_panel();
    return;
  }
  //   Ctrl+Shift+M  bottom panel, on its Problems view
  if (is_ctrl && is_shift && (ch == 'm' || ch == 'M' || original_ch == 'm' || original_ch == 'M'))
  {
    show_problems_panel();
    return;
  }

  // The floating terminal owns the keys while its box is up: it is above the
  // dock both on screen and here.
  if (show_floating_terminal)
  {
    handle_floating_terminal_input(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  IntegratedTerminal *active_terminal = get_integrated_terminal();
  if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
  {
    handle_integrated_terminal_input(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  // The bottom panel owns its *plain* keys while it has focus, so the shell is
  // never handed j/k/Enter and the buffer underneath never sees them either. A
  // modified chord is a global shortcut (Alt+S saves, Ctrl+Shift+M reopens
  // this panel, Ctrl+B hides the explorer) and goes on to the normal handling
  // below, the same way the explorer lets those chords through. Swallowing
  // them here made the panel a keyboard dead zone: with the Problems list
  // focused, Ctrl+S and every Alt keybind did nothing at all.
  if (show_integrated_terminal && focus_state == FOCUS_BOTTOM_PANEL && !is_ctrl && !is_alt)
  {
    handle_bottom_panel_input(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  // Global pane keybinds (before modeless editing shortcuts).
  // Focus:
  // - Ctrl+Alt+Arrow
  if (is_ctrl && is_alt
      && (ch == 1008 || ch == 1009 || ch == 1010 || ch == 1011 || original_ch == 1008
          || original_ch == 1009 || original_ch == 1010 || original_ch == 1011))
  {
    bool focused = false;
    if (ch == 1011 || original_ch == 1011)
    {
      focused = focus_pane_direction('h');
    }
    else if (ch == 1009 || original_ch == 1009)
    {
      focused = focus_pane_direction('j');
    }
    else if (ch == 1008 || original_ch == 1008)
    {
      focused = focus_pane_direction('k');
    }
    else if (ch == 1010 || original_ch == 1010)
    {
      focused = focus_pane_direction('l');
    }

    if (!focused)
    {
      set_message("No pane in that direction");
    }
    return;
  }

  // Resize:
  // - Ctrl+Shift+H/J/K/L
  // - Ctrl+Shift+Arrow
  // The Shift is what keeps the arrows navigational: Ctrl+Left / Ctrl+Right
  // are the word motions and Ctrl+Up / Ctrl+Down scroll the view
  // (handle_modeless_input), and this block runs first, so an ungated arrow
  // arm would take all four chords for good. Ctrl+Shift+Arrow still resizes,
  // which is what the H/J/K/L arm always did.
  // Alt is deliberately excluded: Ctrl+Alt+H/J/K/L are the split keys below,
  // and on kitty-protocol terminals they decode to the uppercase letters,
  // which would otherwise be swallowed here as a resize.
  if (is_ctrl && !is_alt && (is_shift || std::isupper((unsigned char)ch))
      && (ch == 'h' || ch == 'H' || ch == 'j' || ch == 'J' || ch == 'k' || ch == 'K'
          || ch == 'l' || ch == 'L' || ch == 1008 || ch == 1009 || ch == 1010 || ch == 1011
          || original_ch == 1008 || original_ch == 1009 || original_ch == 1010
          || original_ch == 1011))
  {
    bool resized = false;
    if (ch == 'h' || ch == 'H' || original_ch == 'h' || original_ch == 'H' || ch == 1011
        || original_ch == 1011)
    {
      resized = resize_current_pane_direction('h', 2);
    }
    else if (ch == 'j' || ch == 'J' || original_ch == 'j' || original_ch == 'J' || ch == 1009
             || original_ch == 1009)
    {
      resized = resize_current_pane_direction('j', 1);
    }
    else if (ch == 'k' || ch == 'K' || original_ch == 'k' || original_ch == 'K' || ch == 1008
             || original_ch == 1008)
    {
      resized = resize_current_pane_direction('k', 1);
    }
    else if (ch == 'l' || ch == 'L' || original_ch == 'l' || original_ch == 'L' || ch == 1010
             || original_ch == 1010)
    {
      resized = resize_current_pane_direction('l', 2);
    }

    if (resized)
    {
      set_message("Pane resized");
      return;
    }
    set_message("Resize unavailable in this direction");
    return;
  }

  // Split left/down/up/right: Alt+Shift+Arrow.
  //
  // The pane vocabulary itself is the Lua keymap family Ctrl+W
  // (runtime/lua/features/keymaps.lua), which which-key lists and a config can
  // rebind; what is left here is the native fallback for a build without Lua,
  // on the same four chords. Arrows and not letters on purpose: an arrow report
  // carries its modifiers as a real terminal sequence (CSI 1;4A) and as an
  // arrow keycode in the GUI, while Shift+Alt+letter is swallowed before the
  // terminal sees it on any desktop that binds Alt+Shift (hyprland, KDE, XKB's
  // grp:alt_shift_toggle), and arrives in the GUI as a lowercase keycode that a
  // case-sensitive letter match (the old Alt+Shift+H/J/K/L here) never fired
  // on. Alt+Shift+Arrow was chosen over Ctrl+Alt+Arrow because Ctrl+Alt+Arrow
  // is pane focus (above), and over Ctrl+Shift+Arrow, which resizes.
  if (is_alt && is_shift
      && (ch == 1008 || ch == 1009 || ch == 1010 || ch == 1011 || original_ch == 1008
          || original_ch == 1009 || original_ch == 1010 || original_ch == 1011))
  {
    if (ch == 1011 || original_ch == 1011)
    {
      split_pane_left();
    }
    else if (ch == 1009 || original_ch == 1009)
    {
      split_pane_down();
    }
    else if (ch == 1008 || original_ch == 1008)
    {
      split_pane_up();
    }
    else
    {
      split_pane_right();
    }
    return;
  }

  // The floating shell: Alt+Shift+T (the docked ones are Ctrl+` and
  // Ctrl+Shift+T).
  if (is_alt && is_shift
      && (ch == 'T' || original_ch == 'T' || ch == 't' || original_ch == 't'))
  {
    toggle_floating_terminal();
    return;
  }

  if (is_alt)
  {
    const bool alt_h = ch == 'h' || ch == 'H' || original_ch == 'h' || original_ch == 'H';
    const bool alt_j = ch == 'j' || ch == 'J' || original_ch == 'j' || original_ch == 'J';
    const bool alt_k = ch == 'k' || ch == 'K' || original_ch == 'k' || original_ch == 'K';
    const bool alt_l = ch == 'l' || ch == 'L' || original_ch == 'l' || original_ch == 'L';
    const bool alt_direction = alt_h || alt_j || alt_k || alt_l;

    if (focus_state == FOCUS_SIDEBAR && alt_l)
    {
      focus_state = FOCUS_EDITOR;
      set_message("Focused editor");
      needs_redraw = true;
      return;
    }

    bool focused = false;
    if (alt_h)
    {
      focused = focus_pane_direction('h');
    }
    else if (alt_l)
    {
      focused = focus_pane_direction('l');
    }
    else if (alt_k)
    {
      focused = focus_pane_direction('k');
    }
    else if (alt_j)
    {
      focused = focus_pane_direction('j');
    }

    if (focused)
    {
      return;
    }
    if (focus_state == FOCUS_EDITOR && alt_h && show_sidebar)
    {
      focus_state = FOCUS_SIDEBAR;
      set_message("Focused explorer");
      needs_redraw = true;
      return;
    }
    if (alt_direction)
    {
      // Alt+H and Alt+L are also the word motions (see the Navigation table in
      // docs/FEATURES.md), and this is where they stop being dead keys: with
      // nothing in that direction to focus -- a single pane, no sidebar -- the
      // chord falls through to handle_modeless_input below and moves the
      // caret. Alt+J / Alt+K have no such second meaning, so they report
      // rather than do nothing.
      if (!alt_h && !alt_l)
      {
        set_message("No pane in that direction");
        return;
      }
    }
  }

  // Reserve Ctrl+S for save.
  if ((is_ctrl && (ch == 's' || ch == 'S')) || ch == 19 || original_ch == 19)
  {
    save_file();
    needs_redraw = true;
    return;
  }

  // Save prompt input should always receive keystrokes first.
  if (show_save_prompt)
  {
    handle_save_prompt(ch);
    return;
  }
  // ...and so does the rename prompt, for the same reason: its text field would
  // otherwise be read as editor commands.
  if (show_rename_prompt)
  {
    handle_rename_prompt(ch);
    return;
  }
  if (show_sidebar && focus_state == FOCUS_SIDEBAR)
  {
    if (ch == 27)
    {
      focus_state = FOCUS_EDITOR;
      needs_redraw = true;
      return;
    }
    // Keep global/editor shortcuts usable while explorer is focused: a
    // modified chord is a shortcut, not a tree command, so Ctrl *and* Alt are
    // routed through the modeless editor handlers. Dropping Alt here silently
    // killed every Alt keybind (save, search, palette, buffer close, pane
    // focus) whenever the explorer had focus -- which is where `jot <dir>`
    // starts and where clicking any file in the tree leaves the focus -- while
    // Ctrl kept working, so it read as "Alt does not work in this terminal".
    bool ctrl_control_byte = (ch >= 1 && ch <= 26 && ch != 9 && ch != 10 && ch != 13);
    if (is_ctrl || is_alt || ctrl_control_byte || ch == 23 || ch == 12)
    {
      handle_modeless_input(ch, is_ctrl, is_shift, is_alt);
      return;
    }
    handle_sidebar_input(ch);
    return;
  }

  if (ch == 12)
  {
    focus_state = FOCUS_EDITOR;
    ui->invalidate();
    needs_redraw = true;
    return;
  }

  if (search.visible())
  {
    search.handle_panel_input(ch, is_ctrl, is_shift);
    return;
  }

  if (show_command_palette)
  {
    handle_command_palette(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  // The dock owns its keys only while it has focus. `jot.ui.panel` opens a
  // plugin tab without moving the focus there, so with the caret still in the
  // buffer a plain q or Esc is text: reading them as the panel's close keys
  // closed the LeetCode panel mid-word and swallowed the keystroke.
  const bool right_panel_focused = focus_state == FOCUS_RIGHT_PANEL;

  if (show_right_panel && right_panel_focused && active_right_panel_tab == RIGHT_PANEL_GIT
      && !is_ctrl && !is_alt)
  {
    const bool shift = is_shift || std::isupper((unsigned char)ch);
    if (ch == 'q' || ch == 'Q' || ch == 27)
    {
      toggle_git_panel();
      return;
    }
    if (ch == 'j' || ch == 'J' || ch == 1009 || ch == 14)
    {
      git_panel_move_selection(1);
      return;
    }
    if (ch == 'k' || ch == 'K' || ch == 1008 || ch == 16)
    {
      git_panel_move_selection(-1);
      return;
    }
    if (ch == ',')
    {
      git_panel_page(-1);
      return;
    }
    if (ch == '.')
    {
      git_panel_page(1);
      return;
    }
    if (ch == '<' || ch == 1012)
    {
      git_panel_jump_to_end(false);
      return;
    }
    if (ch == '>' || ch == 1013)
    {
      git_panel_jump_to_end(true);
      return;
    }
    if (ch == ' ')
    {
      git_panel_primary();
      return;
    }
    if (ch == '\n' || ch == 13)
    {
      git_panel_open_diff_selected();
      return;
    }
    if (ch == 'a' && !shift)
    {
      git_panel_stage_all();
      return;
    }
    if ((ch == 'a' && shift) || ch == 'A')
    {
      git_panel_unstage_all();
      return;
    }
    if (ch == 'c')
    {
      git_panel_commit_prompt();
      return;
    }
    if (ch == 'd')
    {
      git_panel_discard_or_delete();
      return;
    }
    if (ch == 's')
    {
      git_panel_stash_push();
      return;
    }
    if (ch == 'g' && !shift)
    {
      git_panel_stash_pop();
      return;
    }
    if (ch == 'n')
    {
      git_panel_new_branch_prompt();
      return;
    }
    if (ch == 'm')
    {
      git_panel_merge_prompt();
      return;
    }
    if (ch == 'f')
    {
      git_panel_fetch();
      return;
    }
    if ((ch == 'p' && shift) || ch == 'P')
    {
      git_panel_push();
      return;
    }
    if (ch == 'p' && !shift)
    {
      git_panel_pull();
      return;
    }
    if (ch == 'r')
    {
      refresh_git_status(true);
      git_panel_refresh();
      return;
    }
    if (ch == 'y')
    {
      git_panel_copy();
      return;
    }
    if (ch == '2')
    {
      git_panel_switch_view(2);
      return;
    }
    if (ch == '3')
    {
      git_panel_switch_view(3);
      return;
    }
    if (ch == '4')
    {
      git_panel_switch_view(4);
      return;
    }
    if (ch == '5')
    {
      git_panel_switch_view(5);
      return;
    }
    if (ch == '?')
    {
      set_message("Git panel: j/k move · space stage/checkout · a/A stage-all · c commit · "
                  "d discard/delete · s stash · g pop · n new branch · m merge · "
                  "f fetch · p/P pull/push · y copy · r refresh · 2-5 views");
      return;
    }
    return;
  }

  if (show_right_panel && right_panel_focused && active_right_panel_tab == RIGHT_PANEL_GIT_DIFF
      && !is_ctrl && !is_alt)
  {
    if (ch == 'q' || ch == 'Q' || ch == 27)
    {
      close_git_diff_panel();
      set_message("git diff closed");
      return;
    }

    if (ch == 'j' || ch == 'J' || ch == 1009 || ch == 14)
    {
      scroll_git_diff_panel(1);
      return;
    }

    if (ch == 'k' || ch == 'K' || ch == 1008 || ch == 16)
    {
      scroll_git_diff_panel(-1);
      return;
    }

    if (ch == 'r' || ch == 'R')
    {
      open_git_diff_panel(git_diff_panel.path, git_diff_panel.staged);
      return;
    }
    return;
  }

  if (show_right_panel && right_panel_focused && active_right_panel_tab == RIGHT_PANEL_SYMBOLS
      && !is_ctrl && !is_alt)
  {
    if (ch == 'q' || ch == 'Q' || ch == 27)
    {
      close_outline_panel();
      set_message("Outline closed");
      return;
    }
    if (ch == '\n' || ch == 13)
    {
      outline_jump_selected();
      return;
    }
    if (ch == 'j' || ch == 'J' || ch == 1009 || ch == 14)
    {
      outline_move_selection(1);
      return;
    }
    if (ch == 'k' || ch == 'K' || ch == 1008 || ch == 16)
    {
      outline_move_selection(-1);
      return;
    }
    if (ch == 1003)
    { // Left: fold the selection's own subtree
      outline_collapse_or_expand(false);
      return;
    }
    if (ch == 1004)
    { // Right: expand it, or walk down into it when already open
      outline_collapse_or_expand(true);
      return;
    }
    if (ch == 1012)
    { // Home
      outline_panel.selected = 0;
      needs_redraw = true;
      return;
    }
    if (ch == 1013)
    { // End
      outline_panel.selected = std::max(0, (int)outline_panel.visible.size() - 1);
      needs_redraw = true;
      return;
    }
    return;
  }

  if (show_right_panel && right_panel_focused && active_right_panel_tab == RIGHT_PANEL_PLUGIN
      && !is_ctrl && !is_alt)
  {
    if (ch == 'q' || ch == 'Q' || ch == 27)
    {
      active_plugin_panel.clear();
      close_right_panel_tab(RIGHT_PANEL_PLUGIN);
      set_message("Plugin panel closed");
      return;
    }
  }

  handle_modeless_input(ch, is_ctrl, is_shift, is_alt);
  return;
}
