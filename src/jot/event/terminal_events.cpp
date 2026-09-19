// Terminal event dispatch: paste, resize, focus, key, mouse, and terminal-input events.
#include "editor.h"
#include "jot/event/keymap_candidates.h"
#include "jot/lua/api.h"

void Editor::handle_terminal_event(const Event &ev)
{
  if (ev.type == EVENT_REDRAW)
  {
    return;
  }

  if (ev.type == EVENT_PASTE)
  {
    IntegratedTerminal *active_terminal = get_integrated_terminal();
    if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
    {
      active_terminal->send_text(ev.paste.text ? ev.paste.text : "");
      needs_redraw = true;
      return;
    }
    cancel_lsp_mouse_hover();
    clipboard = ev.paste.text ? ev.paste.text : "";
    paste();
    return;
  }

  if (ev.type == EVENT_RESIZE)
  {
    apply_resize(ev.resize.width, ev.resize.height);
    return;
  }

  // Window focus reporting (DECSET 1004): while unfocused the compositor
  // may repaint our surface from a stale buffer (transparency/blur mixes
  // whatever is behind the window into the cached frame), and the
  // cell-diff renderer then skips repainting rows it believes are already
  // correct. Forgetting the last written frame forces every row back out
  // on top of whatever is (or isn't) there — no ESC[2J clear, which races
  // compositor surface teardown and blanks the window instead of fixing
  // it. The repaint is spread over three passes (~50ms apart): kitty with
  // dynamic opacity + Hyprland transparency needs more than one frame for
  // the surface opacity to settle after refocus; a single repaint lands
  // while the surface is still recompositing and the gaps keep the stale
  // background. Focus-out needs no repaint; it only marks state for a
  // future focus-in.
  if (ev.type == EVENT_FOCUS_IN)
  {
    // Window focus is also the Discord presence's idle clock: coming back
    // restores a presence that the idle timeout had cleared.
    discord.note_focus(true, jot_discord::monotonic_ms());
    ui->forget_last_frame();
    needs_redraw = true;
    // Second and third repaints after the surface settles: each re-forgets
    // so the full-row pass really re-emits (a plain needs_redraw would be
    // a no-op once the model matches the baseline again). One-shot timers;
    // render() consumes needs_redraw, the timer only re-arms it.
    event_loop_.set_timeout(60,
                            [this]
                            {
                              ui->forget_last_frame();
                              needs_redraw = true;
                            });
    event_loop_.set_timeout(150,
                            [this]
                            {
                              ui->forget_last_frame();
                              needs_redraw = true;
                            });
    return;
  }

  if (ev.type == EVENT_FOCUS_OUT)
  {
    discord.note_focus(false, jot_discord::monotonic_ms());
    return;
  }

  if (ev.type == EVENT_KEY)
  {
    keyboard_press_count++;
    // Typing proves the window is focused: it cancels an idle stretch (so a
    // terminal that misreports focus cannot leave the presence cleared) and
    // restores it on the next poll.
    discord.note_focus(true, jot_discord::monotonic_ms());
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
    // A key can arrive before a delayed mouse release. Preserve the current
    // selection, but prevent that release from restoring an obsolete cursor.
    mouse_selecting = false;
    mouse_drag_started = false;
    int ch = ev.key.key;
    bool is_ctrl = ev.key.ctrl;
    bool is_shift = ev.key.shift;
    bool is_alt = ev.key.alt;
    int original_ch = ch;

    if (lua_api && lua_api->float_input(ch, is_ctrl, is_shift, is_alt))
    {
      needs_redraw = true;
      return;
    }

    bool ctrl_q_shortcut =
        (is_ctrl && (ch == 'q' || ch == 'Q' || original_ch == 'q' || original_ch == 'Q'))
        || ch == 17 || original_ch == 17;
    if (ctrl_q_shortcut)
    {
      if (show_command_palette)
      {
        // While the palette is open, Ctrl+Q dismisses it instead of closing
        // a pane or quitting the editor underneath it.
        handle_command_palette(27, is_ctrl, is_shift, is_alt);
        needs_redraw = true;
        return;
      }
      handle_input('q', is_ctrl, is_shift, is_alt, original_ch);
      return;
    }

    bool toggle_terminal_shortcut = (is_ctrl && (ch == '`' || ch == '~' || ch == '\\' || ch == '|'))
                                    || ch == 28 || original_ch == 28 || ch == 30
                                    || original_ch == 30;
    if (toggle_terminal_shortcut)
    {
      toggle_integrated_terminal();
      return;
    }

    IntegratedTerminal *active_terminal = get_integrated_terminal();
    if (show_integrated_terminal && active_terminal && active_terminal->is_focused())
    {
      handle_integrated_terminal_input(ch, is_ctrl, is_shift, is_alt);
      return;
    }

    if (is_ctrl && ch >= 1 && ch <= 26)
    {
      ch = ch + 96;
    }

    // Bare-modifier synthetic codes (1021 = Ctrl held down, 1022 = Ctrl
    // released) are a Windows-Terminal-only delivery and have no equivalent
    // on POSIX/kitty-protocol terminals, so the hold-Ctrl helper view they
    // drove has been removed. They are inert here on every platform.
    if (ch == 1021 || ch == 1022)
    {
      return;
    }

    if (popup.visible && popup.presentation == POPUP_MODAL)
    {
      handle_popup_input(ch);
      return;
    }

    if (show_menu_bar_dropdown)
    {
      handle_menu_bar_input(ch);
    }
    else if (show_context_menu)
    {
      handle_context_menu_input(ch);
    }
    else if (show_tree_sitter_status_modal && handle_tree_sitter_status_input(ch))
    {
      // consumed by the tree-sitter status panel
    }
    else if (show_lsp_status_modal)
    {
      handle_lsp_status_input(ch);
    }
    else if (show_command_palette)
    {
      handle_command_palette(ch, is_ctrl, is_shift, is_alt);
    }
    else if (search.visible())
    {
      search.handle_panel_input(ch, is_ctrl, is_shift);
    }
    else if (telescope.is_active())
    {
      handle_telescope(ch);
    }
    else if (show_quick_pick)
    {
      handle_input(ch, is_ctrl, is_shift, is_alt, original_ch);
    }
    else
    {
      if (lua_api && show_which_key)
      {
        // A multi-chord keymap group is active: the pressed key picks the next
        // chord. handle_which_key_input returns false (after closing the
        // helper) when the key is not a valid next step, so it falls through
        // to normal plugin/built-in handling below (e.g. types text).
        if (handle_which_key_input(ch, is_ctrl, is_shift, is_alt, original_ch))
        {
          needs_redraw = true;
          return;
        }
      }
      if (lua_api)
      {
        auto candidates = event_internal::plugin_key_candidates(ch, is_ctrl, is_shift, is_alt, original_ch);
        event_internal::log_keymap_debug(ch, is_ctrl, is_shift, is_alt, original_ch, candidates);
        for (const auto &candidate : candidates)
        {
          if (lua_api->run_plugin_keymap(candidate, "editor"))
          {
            needs_redraw = true;
            return;
          }
          // A chord that prefixes longer keymap sequences (e.g. "Ctrl+T" for
          // "Ctrl+T N") starts the sequence; the next chord completes it.
          if (lua_api->plugin_keymap_is_prefix(candidate, "editor"))
          {
            open_which_key(candidate);
            needs_redraw = true;
            return;
          }
        }
      }
      handle_input(ch, is_ctrl, is_shift, is_alt, original_ch);
    }
    return;
  }

  if (ev.type == EVENT_MOUSE)
  {
    int button = ev.mouse.button;
    // SGR wheel encoding: low two bits are the wheel axis (0 = vertical,
    // 1 = horizontal after masking modifiers), bits 2-4 are Shift/Alt/Ctrl.
    // So shift+wheel-up arrives as 68, shift+wheel-down as 69, etc. Strip
    // the modifiers (and motion bit) before comparing the base button.
    const int wheel_base = button & ~0x3C;
    const bool shift_held = ev.mouse.shift;
    // Horizontal scroll adjusts scroll_x instead: raw buttons 66/67, or
    // Shift+vertical wheel (the fallback terminals send with no horizontal
    // encoding).
    bool is_h_wheel = (wheel_base == 66 || wheel_base == 67)
                      || (shift_held && (wheel_base == 64 || wheel_base == 65));
    bool is_wheel = (wheel_base >= 64 && wheel_base <= 67);

    // A click outside the helper dismisses it (like Esc).
    if (show_which_key && ev.mouse.pressed && wheel_base != 64 && wheel_base != 65)
    {
      close_which_key();
    }

    if (lua_api
        && lua_api->float_mouse(ev.mouse.x,
                                ev.mouse.y,
                                button,
                                ev.mouse.pressed,
                                ev.mouse.released,
                                (button & 0x20) != 0,
                                ev.mouse.ctrl,
                                ev.mouse.shift,
                                ev.mouse.alt))
    {
      needs_redraw = true;
      return;
    }


    if (telescope.is_active())
    {
      bool is_click = ev.mouse.pressed && ((button & 0x03) == 0);
      static long long last_telescope_click_ms = 0;
      static int last_telescope_click_x = -1;
      static int last_telescope_click_y = -1;
      long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count();
      bool is_double_click =
          is_click && last_telescope_click_ms > 0 && now_ms - last_telescope_click_ms <= 350
          && last_telescope_click_x == ev.mouse.x && last_telescope_click_y == ev.mouse.y;
      if (is_click)
      {
        last_telescope_click_ms = now_ms;
        last_telescope_click_x = ev.mouse.x;
        last_telescope_click_y = ev.mouse.y;
      }
      if (handle_telescope_mouse(ev.mouse.x,
                                   ev.mouse.y,
                                   is_click,
                                   is_double_click,
                                   wheel_base == 64,
                                   wheel_base == 65))
      {
        return;
      }
    }

    if (is_h_wheel && !telescope.is_active() && !show_command_palette && !search.visible())
    {
      bool left = (wheel_base == 66) || (shift_held && wheel_base == 64);
      bool right = (wheel_base == 67) || (shift_held && wheel_base == 65);
      handle_mouse_input(ev.mouse.x, ev.mouse.y, false, false, false, left, right);
    }
    else if (is_wheel && !telescope.is_active() && !show_command_palette && !search.visible())
    {
      handle_mouse_input(ev.mouse.x, ev.mouse.y, false, wheel_base == 64, wheel_base == 65);
    }
    else
    {
      struct
      {
        int x, y;
        int bstate;
        bool ctrl;
        bool shift;
        bool alt;
      } mevent;
      mevent.x = ev.mouse.x;
      mevent.y = ev.mouse.y;
      mevent.ctrl = ev.mouse.ctrl;
      mevent.shift = ev.mouse.shift;
      mevent.alt = ev.mouse.alt;
      int bstate = 0;

      int button_code = ev.mouse.button & 0x03;
      bool is_motion = (ev.mouse.button & 0x20) != 0;

      if (is_motion)
      {
        if (show_context_menu || button_code == 0 || button_code == 3)
        {
          bstate = 32;
        }
        else
        {
          bstate = 0;
        }
      }
      else if (ev.mouse.pressed)
      {
        if (button_code == 0)
          bstate = 1;
        else if (button_code == 2)
          bstate = 3;
        else if (button_code == 1)
          bstate = 4;
        else
          bstate = 1;
      }
      else if (ev.mouse.released)
      {
        bstate = 2;
      }

      mevent.bstate = bstate;
      handle_mouse(&mevent);
    }
  }
}
