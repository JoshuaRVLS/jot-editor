#include "editor.h"
#include <algorithm>
#include <cctype>

void Editor::handle_input(int ch, bool is_ctrl, bool is_shift, bool is_alt, int original_ch)
{
  // Keep the cursor solid while typing: restart the blink clock and
  // suspend blinking briefly on every keypress.
  restart_blink();
  clear_debugger_breakpoint_hover();

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
      // instead of quitting — Ctrl+Q is the natural close-panel chord and
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

  IntegratedTerminal *active_terminal = get_integrated_terminal();
  if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
  {
    handle_integrated_terminal_input(ch, is_ctrl, is_shift, is_alt);
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
  // - Ctrl+Arrow
  // Alt is deliberately excluded: Ctrl+Alt+H/J/K/L are the split keys below,
  // and on kitty-protocol terminals they decode to the uppercase letters,
  // which would otherwise be swallowed here as a resize.
  if (is_ctrl && !is_alt
      && (((is_shift || std::isupper((unsigned char)ch))
           && (ch == 'h' || ch == 'H' || ch == 'j' || ch == 'J' || ch == 'k' || ch == 'K'
               || ch == 'l' || ch == 'L'))
          || ch == 1008 || ch == 1009 || ch == 1010 || ch == 1011 || original_ch == 1008
          || original_ch == 1009 || original_ch == 1010 || original_ch == 1011))
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

  // Split and pane operations:
  // - Alt+Shift+H/J/K/L (split left/down/up/right)
  // - Alt+Shift+Q (close pane / quit), Alt+Shift+E (equalize),
  //   Alt+Shift+Z (zoom), Alt+Shift+X (swap)
  // Alt+letter is pane focus (handled below); Shift distinguishes split from
  // focus. Alt+Shift was chosen over Ctrl+Alt because Ctrl+Alt+letter is not
  // delivered at all on Alacritty for Windows (winit drops the text for
  // Ctrl+Alt+[a-z]), while Alt+Shift+letter is plain ESC + uppercase and
  // works everywhere. Alt+Shift always decodes to an uppercase letter, so
  // only the uppercase forms are matched.
  if (is_alt && is_shift)
  {
    if (ch == 'Q' || original_ch == 'Q')
    {
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
    if (ch == 'H' || original_ch == 'H')
    {
      split_pane_left();
      return;
    }
    if (ch == 'J' || original_ch == 'J')
    {
      split_pane_down();
      return;
    }
    if (ch == 'K' || original_ch == 'K')
    {
      split_pane_up();
      return;
    }
    if (ch == 'L' || original_ch == 'L')
    {
      split_pane_right();
      return;
    }
    // Pane operations: equalize, zoom, swap. Kept on Alt+Shift like the
    // splits above so the whole pane vocabulary shares one modifier family.
    if (ch == 'E' || original_ch == 'E')
    {
      equalize_panes();
      return;
    }
    if (ch == 'Z' || original_ch == 'Z')
    {
      toggle_pane_zoom();
      return;
    }
    if (ch == 'X' || original_ch == 'X')
    {
      swap_panes();
      return;
    }
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
      set_message("No pane in that direction");
      return;
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

  // Selection manipulation on the same Alt+Shift layer. Names chosen to read
  // from the command: K(eep) primary, P(rimary) rotate, D(own)/B(ack) caret,
  // S(plit) lines, M(all) occurrences.
  if (is_alt && is_shift && (ch == 'k' || ch == 'K'))
  {
    keep_primary_selection();
    return;
  }
  if (is_alt && is_shift && (ch == 'p' || ch == 'P'))
  {
    rotate_primary_selection(1);
    return;
  }
  if (is_alt && is_shift && (ch == 'd' || ch == 'D'))
  {
    add_caret_on_adjacent_line(1);
    return;
  }
  if (is_alt && is_shift && (ch == 'b' || ch == 'B'))
  {
    add_caret_on_adjacent_line(-1);
    return;
  }
  if (is_alt && is_shift && (ch == 's' || ch == 'S'))
  {
    split_selection_on_newlines();
    return;
  }
  if (is_alt && is_shift && (ch == 'm' || ch == 'M'))
  {
    select_all_occurrences();
    return;
  }

  // Tree-sitter textobjects. Alt+O/Alt+I are helix's chords but both are taken
  // here (sort lines, smart line start), so the selection pair lives on the
  // Alt+Shift layer the other structural edits use.
  if (is_alt && is_shift && (ch == 'o' || ch == 'O'))
  {
    expand_selection_to_node();
    return;
  }
  if (is_alt && is_shift && (ch == 'i' || ch == 'I'))
  {
    shrink_selection_to_node();
    return;
  }
  if (is_alt && is_shift && (ch == 'f' || ch == 'F'))
  {
    select_textobject("function", true);
    return;
  }
  if (is_alt && is_shift && (ch == 'c' || ch == 'C'))
  {
    select_textobject("class", true);
    return;
  }
  if (is_alt && is_shift && (ch == 'a' || ch == 'A'))
  {
    select_textobject("argument", true);
    return;
  }

  // Global sidebar toggles should work regardless of current focus:
  // Ctrl+B opens the left explorer, Ctrl+Shift+B the right dock.
  if (is_ctrl && (ch == 'b' || ch == 'B'))
  {
    if (is_shift || ch == 'B')
    {
      toggle_right_panel();
    }
    else
    {
      toggle_sidebar();
    }
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
    // Keep global/editor shortcuts usable while explorer is focused.
    // Ctrl-based keybinds are routed through modeless editor handlers.
    bool ctrl_control_byte = (ch >= 1 && ch <= 26 && ch != 9 && ch != 10 && ch != 13);
    if (is_ctrl || ctrl_control_byte || ch == 23 || ch == 12)
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

  if (show_quit_prompt)
  {
    if (ch == 'y' || ch == 'Y' || ch == '\n')
    {
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

  if (show_search)
  {
    handle_search_panel(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  if (show_command_palette)
  {
    handle_command_palette(ch, is_ctrl, is_shift, is_alt);
    return;
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT && !is_ctrl && !is_alt)
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

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_GIT_DIFF && !is_ctrl && !is_alt)
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

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_SYMBOLS && !is_ctrl && !is_alt)
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
    if (ch == 1012)
    { // Home
      outline_panel.selected = 0;
      needs_redraw = true;
      return;
    }
    if (ch == 1013)
    { // End
      outline_panel.selected = std::max(0, (int)outline_panel.symbols.size() - 1);
      needs_redraw = true;
      return;
    }
    return;
  }

  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_PLUGIN && !is_ctrl && !is_alt)
  {
    if (ch == 'q' || ch == 'Q' || ch == 27)
    {
      active_plugin_panel.clear();
      close_right_panel_tab(RIGHT_PANEL_PLUGIN);
      set_message("Plugin panel closed");
      return;
    }
  }

  if (image_viewer.is_active())
  {
    if (ch == 'q' || ch == 27)
    {
      image_viewer.close();
      needs_redraw = true;
    }
    return;
  }

  handle_modeless_input(ch, is_ctrl, is_shift, is_alt);
  return;
}
