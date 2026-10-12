#include "core/editor.h"
#include "ui/smooth_scroll.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

void Editor::handle_modeless_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  // The workspace strip's keys (Alt+1..9 and friends, jump-to-buffer mode):
  // first, so a jump letter is never read as a command of its own.
  if (handle_tabline_key(ch, is_ctrl, is_shift, is_alt))
  {
    return;
  }

  // An image tab's buffer is only an empty placeholder (see open_file). Letting
  // an edit key through would type into that placeholder, and the next save
  // would write it over the picture; navigation pans the preview instead of a
  // cursor. Everything that is neither editing nor scrolling falls through to
  // the normal handling, so the global shortcuts still work here.
  const bool image_buffer = current_buffer >= 0 && current_buffer < (int)buffers.size()
                            && image_viewer.is_image_file(buffers[(size_t)current_buffer].filepath);
  if (image_buffer && !is_ctrl && !is_alt)
  {
    int scroll_lines = 0;
    if (ch == 1008 || ch == 'k' || ch == 'K')
    {
      scroll_lines = -1;
    }
    else if (ch == 1009 || ch == 'j' || ch == 'J')
    {
      scroll_lines = 1;
    }
    else if (ch == 1015)
    {
      scroll_lines = -10;
    }
    else if (ch == 1016)
    {
      scroll_lines = 10;
    }
    else if (ch == 1012)
    {
      image_viewer.set_preview_scroll(0);
      needs_redraw = true;
      return;
    }
    else if (ch == 1013)
    {
      image_viewer.set_preview_scroll(image_viewer.preview_content_rows());
      needs_redraw = true;
      return;
    }
    if (scroll_lines != 0)
    {
      image_viewer.scroll_preview(scroll_lines);
      needs_redraw = true;
      return;
    }

    const bool editing_key = ch == '\n' || ch == 13 || ch == '\t' || ch == 9 || ch == 127 || ch == 8
                             || ch == 1001 || ch == 1017 || (ch >= 32 && ch < 1000);
    if (editing_key)
    {
      return;
    }
  }

  // The popup owns the bare arrows, Enter, Tab and Esc while it is up. A
  // chord is not one of those: Ctrl+Up/Down still scrolls the view (and
  // Ctrl+Left/Right still moves by word) instead of stepping the selection.
  if (lsp_completion_visible && !is_ctrl && !is_alt)
  {
    if (ch == 1008)
    {
      lsp_completion_selected = std::max(0, lsp_completion_selected - 1);
      update_lsp_completion_ghost();
      needs_redraw = true;
      return;
    }
    if (ch == 1009)
    {
      lsp_completion_selected =
          std::min(std::max(0, (int)lsp_completion_items.size() - 1), lsp_completion_selected + 1);
      update_lsp_completion_ghost();
      needs_redraw = true;
      return;
    }
    if (ch == '\n' || ch == 13 || ch == '\t' || ch == 9)
    {
      if (apply_selected_lsp_completion())
      {
        needs_redraw = true;
      }
      return;
    }
    if (ch == 27)
    {
      hide_lsp_completion();
      hide_lsp_signature();
      return;
    }
  }

  const bool modified_enter = (is_ctrl || is_alt) && (ch == '\n' || ch == 13);
  if (modified_enter)
  {
    hide_lsp_completion();
    if (is_shift)
    {
      insert_line_above();
    }
    else
    {
      insert_line_below();
    }
    return;
  }

  if (is_ctrl && is_shift && (ch == 'l' || ch == 'L'))
  {
    select_current_line();
    return;
  }
  if (is_ctrl && is_shift && (ch == 's' || ch == 'S'))
  {
    int saved = 0;
    for (int i = 0; i < (int)buffers.size(); i++)
    {
      if (!buffers[i].filepath.empty() && buffers[i].modified && save_buffer_at(i, false))
      {
        saved++;
      }
    }
    if (saved > 0)
    {
      set_message("Saved " + std::to_string(saved) + " file(s)");
    }
    else
    {
      set_message("No modified saved files");
    }
    needs_redraw = true;
    return;
  }
  if (is_ctrl && is_shift && (ch == 't' || ch == 'T'))
  {
    reopen_last_closed_buffer();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'u' || ch == 'U'))
  {
    transform_selection_uppercase();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'n' || ch == 'N'))
  {
    transform_selection_lowercase();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'f' || ch == 'F'))
  {
    hide_lsp_completion();
    if (search.open_scoped_replace_from_selection())
    {
      return;
    }
    show_project_search();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'd' || ch == 'D'))
  {
    duplicate_line();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'o' || ch == 'O'))
  {
    hide_lsp_completion();
    show_symbol_picker();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'm' || ch == 'M'))
  {
    hide_lsp_completion();
    show_diagnostics_picker();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'r' || ch == 'R'))
  {
    // Rename the symbol under the cursor: helix's Space+r prompt.
    open_rename_prompt();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'w' || ch == 'W'))
  {
    // Workspace symbols / workspace diagnostics: the project-wide versions of
    // Ctrl+Shift+O and Ctrl+Shift+M.
    hide_lsp_completion();
    show_workspace_symbols_picker();
    return;
  }
  if (is_ctrl && is_shift && (ch == 'x' || ch == 'X'))
  {
    hide_lsp_completion();
    show_workspace_diagnostics_picker();
    return;
  }
  // Ctrl+Shift+Z is redo wherever Ctrl+Y is. It has to be tested before the
  // Ctrl block below, which reads a bare 'z' as undo -- and it only arrives
  // apart from Ctrl+Z on terminals that report modifiers distinctly (kitty
  // protocol); elsewhere both are the same 0x1a byte, so this is an extra
  // binding rather than a replacement for Ctrl+Y.
  if (is_ctrl && is_shift && (ch == 'z' || ch == 'Z'))
  {
    redo();
    needs_redraw = true;
    return;
  }
  // Ctrl+Tab / Ctrl+Shift+Tab: cycle pane-local tabs.
  if (is_ctrl && (ch == '\t' || ch == 9))
  {
    if (is_shift)
    {
      if (cycle_local_tab(-1))
      {
        return;
      }
    }
    else
    {
      if (cycle_local_tab(1))
      {
        return;
      }
    }
  }
  if (is_ctrl && ch == 1017)
  { // Shift+Tab code path
    if (cycle_local_tab(-1))
    {
      return;
    }
  }

  if (is_ctrl)
  {
    switch (ch)
    {
    case 'o':
    case 'O':
      // Ctrl+O / Ctrl+I walk the jumplist. Ctrl+I is only distinguishable from
      // Tab where the terminal reports modifiers distinctly (kitty protocol);
      // 0x09 never arrives with the ctrl flag, so Tab keeps inserting.
      jump_back();
      return;
    case 'i':
    case 'I':
      jump_forward();
      return;
    case 'q':
    case 'Q':
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
    case 's':
    case 'S':
      save_file();
      needs_redraw = true;
      return;
    case 'z':
    case 'Z':
      undo();
      needs_redraw = true;
      return;
    case 'y':
    case 'Y':
      redo();
      needs_redraw = true;
      return;
    case 'a':
    case 'A':
      select_all();
      needs_redraw = true;
      return;
    case 'c':
    case 'C':
      hide_lsp_completion();
      copy();
      set_message("Copied");
      return;
    case 'x':
    case 'X':
      hide_lsp_completion();
      cut();
      set_message("Cut");
      return;
    case 'v':
    case 'V':
      hide_lsp_completion();
      paste();
      return;
    case 'b':
    case 'B':
      toggle_sidebar();
      return;
    case 'e':
    case 'E':
      telescope.open(telescope_launch_root());
      telescope.scan_async(task_queue_.get(), [this] { needs_redraw = true; });
      needs_redraw = true;
      return;
    case 'f':
    case 'F':
      hide_lsp_completion();
      search.open();
      needs_redraw = true;
      return;
    case 'r':
    case 'R':
      show_command_palette = true;
      command_palette_query = "openrecent ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
      return;
    case 'g':
    case 'G':
      show_command_palette = true;
      command_palette_query = "line ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
      return;
    case 'p':
    case 'P':
      toggle_command_palette();
      needs_redraw = true;
      return;
    case 'm':
    case 'M':
      toggle_minimap();
      needs_redraw = true;
      return;
    case 't':
    case 'T':
      open_theme_chooser();
      needs_redraw = true;
      return;
    case 'd':
    case 'D':
      select_next_occurrence();
      return;
    case 'k':
    case 'K':
      delete_line();
      return;
    case '/':
    case '?':
    case 31:
    case '_': // terminals report Ctrl+/ as byte 0x1f, decoded as Ctrl+_
      toggle_comment();
      return;
    case 'h':
    case 'H':
    case 'w':
    case 'W':
      delete_word_backward();
      return;
    case 127:
    case 8:
    case 23:
      delete_word_backward();
      return;
    case 1001:
      delete_word_forward();
      return;
    // Ctrl+Left / Ctrl+Right (Shift extends) are the word motions every other
    // editor answers this chord with, and the hand is already on the arrow
    // keys: the same movement Alt+H / Alt+L make. This is where the chord has
    // to be claimed, ahead of the bare arrows below, which would otherwise
    // take it as a plain one-cell step; the pane-resize block in dispatch.cpp
    // runs first and claims the same arrows, which is why that one is gated on
    // Shift (Ctrl+Shift+Arrow) and these are not. Deleting a word is
    // Ctrl+Delete / Ctrl+Backspace (case 1001), so the arrows stay free to
    // move.
    case 1010:
      hide_lsp_completion();
      move_word_forward(is_shift);
      return;
    case 1011:
      hide_lsp_completion();
      move_word_backward(is_shift);
      return;
    // Ctrl+Up / Ctrl+Down scroll the view a line at a time and leave the caret
    // where it is: reading through a file without losing the place, the wheel's
    // gesture with the hand still on the keys (and the same animation). The
    // caret walking out of the viewport is expected here -- the next movement
    // pulls the view back to it -- which is why this is not move_cursor.
    case 1008:
      hide_lsp_completion();
      if (scroll_view_smooth(-1, SmoothScroll::kWheelDurationMs))
      {
        needs_redraw = true;
      }
      return;
    case 1009:
      hide_lsp_completion();
      if (scroll_view_smooth(1, SmoothScroll::kWheelDurationMs))
      {
        needs_redraw = true;
      }
      return;
    // Ctrl+Home / Ctrl+End reach the file's ends (Shift selects): the vertical
    // counterpart of the pair above, and the chord the same editors use.
    case 1012:
      hide_lsp_completion();
      move_to_file_start(is_shift);
      return;
    case 1013:
      hide_lsp_completion();
      move_to_file_end(is_shift);
      return;
    case ' ':
      request_lsp_completion(true);
      return;
    }
    return;
  }

  // Some terminals send Ctrl+Space as NUL without the ctrl modifier flag.
  if (ch == 0)
  {
    request_lsp_completion(true);
    return;
  }

  if (ch == 27)
  {
    hide_lsp_completion();
    hide_lsp_signature();
    auto &buf = get_buffer();
    if (!buf.extra_carets.empty())
    {
      clear_extra_carets();
      return;
    }
    if (buf.selection.active)
    {
      clear_selection();
      needs_redraw = true;
    }
    return;
  }

  // Terminal fallback mappings for control bytes that may arrive without the
  // ctrl modifier bit on some terminals.
  if (ch == 19)
  { // Ctrl+S
    hide_lsp_completion();
    save_file();
    needs_redraw = true;
    return;
  }

  // Terminal fallback mappings for Ctrl+Backspace / Ctrl+/
  if (ch == 23)
  {
    hide_lsp_completion();
    delete_word_backward();
    return;
  }
  if (ch == 31)
  { // Ctrl+/ arriving as a raw control byte (no modifier flag)
    hide_lsp_completion();
    toggle_comment();
    return;
  }

  // VSCode-like line move shortcut.

  // Power user action shortcuts (modeless-friendly).
  if (is_alt && (ch == 'w' || ch == 'W'))
  {
    close_buffer();
    return;
  }
  if (is_alt && ch == 'g')
  {
    move_to_file_start(false);
    return;
  }
  if (is_alt && ch == 'G')
  {
    move_to_file_end(false);
    return;
  }
  if (is_alt && (ch == 'i' || ch == 'I'))
  {
    move_to_line_smart_start(false);
    return;
  }
  if (is_alt && (ch == 'a' || ch == 'A'))
  {
    move_to_line_end(false);
    return;
  }
  if (is_alt && (ch == 'h' || ch == 'H'))
  {
    move_word_backward(false);
    return;
  }
  if (is_alt && (ch == 'l' || ch == 'L'))
  {
    move_word_forward(false);
    return;
  }
  // Alt+D is the delete operator of the keymap grammar (see
  // runtime/lua/features/keymaps.lua); deleting a line is Alt+D l. Alt+Shift+D
  // below still duplicates the line.
  if (is_alt && ch == 'D')
  { // convenience: duplicate current line
    duplicate_line();
    return;
  }
  if (is_alt && (ch == 'n' || ch == 'N'))
  {
    create_new_buffer();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 's' || ch == 'S'))
  {
    save_file();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 'f' || ch == 'F'))
  {
    search.toggle();
    needs_redraw = true;
    return;
  }
  if (is_alt && ch == 'e')
  {
    goto_next_diagnostic(1);
    return;
  }
  if (is_alt && ch == 'E')
  {
    goto_next_diagnostic(-1);
    return;
  }
  if (is_alt && (ch == 'p' || ch == 'P'))
  {
    toggle_command_palette();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 'b' || ch == 'B'))
  {
    toggle_sidebar();
    return;
  }
  if (is_alt && (ch == 'm' || ch == 'M'))
  {
    toggle_minimap();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 't' || ch == 'T'))
  {
    open_theme_chooser();
    needs_redraw = true;
    return;
  }
  if (is_alt && ch == 'u')
  {
    transform_selection_uppercase();
    return;
  }
  if (is_alt && ch == 'U')
  {
    transform_selection_lowercase();
    return;
  }
  if (is_alt && (ch == 'o' || ch == 'O'))
  {
    sort_selected_lines();
    return;
  }

  // VSCode-like line move shortcut.
  if (is_alt && ch == 1008)
  {
    move_line_up();
    return;
  }
  if (is_alt && ch == 1009)
  {
    move_line_down();
    return;
  }

  if (ch == 1008)
  {
    hide_lsp_completion();
    move_cursor(0, -1, is_shift);
    return;
  }
  if (ch == 1009)
  {
    hide_lsp_completion();
    move_cursor(0, 1, is_shift);
    return;
  }
  if (ch == 1010)
  {
    hide_lsp_completion();
    move_cursor(1, 0, is_shift);
    return;
  }
  if (ch == 1011)
  {
    hide_lsp_completion();
    move_cursor(-1, 0, is_shift);
    return;
  }
  if (ch == 1012)
  {
    hide_lsp_completion();
    move_to_line_smart_start(is_shift);
    return;
  }
  if (ch == 1013)
  {
    hide_lsp_completion();
    move_to_line_end(is_shift);
    return;
  }
  if (ch == 1015)
  {
    hide_lsp_completion();
    hide_lsp_signature();
    move_cursor(0, -10, is_shift);
    return;
  }
  if (ch == 1016)
  {
    hide_lsp_completion();
    hide_lsp_signature();
    move_cursor(0, 10, is_shift);
    return;
  }

  // Alt+Backspace takes the word the caret is sitting on, where Ctrl+Backspace
  // (and Alt+D W) eat to the boundary. The Lua keymap registers the same chord,
  // so this is the fallback when no plugin config is loaded.
  if (is_alt && (ch == 127 || ch == 8))
  {
    hide_lsp_completion();
    delete_word_at_cursor();
    return;
  }

  if (ch == 127 || ch == 8)
  {
    bool had_completion = lsp_completion_visible;
    delete_char(false);
    needs_redraw = true;
    // Deleting inside the argument list can move the caret to a different
    // parameter: re-ask so the popup keeps the right one highlighted.
    if (lsp_signature_visible)
    {
      request_lsp_signature_help();
    }
    if (had_completion && refresh_lsp_completion_filter())
    {
      request_lsp_completion(false, '_');
    }
    else
    {
      hide_lsp_completion();
      request_lsp_completion(false, '_');
    }
    return;
  }
  if (ch == 1001)
  {
    hide_lsp_completion();
    delete_char(true);
    needs_redraw = true;
    return;
  }

  // The compilation buffer is a list of locations: Enter on a row jumps to the
  // one it names, the way RET does in Emacs' own *compilation* buffer.
  if ((ch == '\n' || ch == 13) && !is_ctrl && !is_alt && compile_buffer_focused())
  {
    compile_jump_from_row(get_buffer().cursor.y);
    return;
  }

  if (ch == '\n' || ch == 13)
  {
    hide_lsp_completion();
    new_line();
    needs_redraw = true;
    return;
  }

  if (ch == '\t' || ch == 9)
  {
    hide_lsp_completion();
    apply_default_tab();
    return;
  }

  // Shift+Tab (terminal escape \e[Z mapped in terminal.cpp)
  if (ch == 1017)
  {
    hide_lsp_completion();
    apply_default_shift_tab();
    return;
  }

  // A bare printable key is text. A chord is not: Alt+X must not type "x"
  // just because no keybind claims the chord, and neither must Ctrl+X when the
  // terminal reports it as a plain code point with the modifier attached
  // (kitty CSI-u does exactly that, for every Ctrl+letter the editor has not
  // bound). Shift is deliberately not excluded -- Shift+1 is "!".
  if (!is_ctrl && !is_alt && ch >= 32 && ch < 1000)
  {
    auto &buf = get_buffer();
    if (buf.selection.active)
      delete_selection();
    bool had_completion = lsp_completion_visible;
    bool inserted_html_closing_tag = insert_char((char)ch);
    char typed = (char)ch;

    if (inserted_html_closing_tag)
    {
      hide_lsp_completion();
      needs_redraw = true;
      return;
    }

    bool trigger_completion = std::isalnum((unsigned char)typed) || typed == '_' || typed == '.'
                              || typed == ':' || typed == '>' || typed == '<' || typed == '/';
    if (trigger_completion)
    {
      if (had_completion)
      {
        refresh_lsp_completion_filter();
      }
      request_lsp_completion(false, typed);
      // Typing the first argument of a call the editor auto-closed (or the
      // caret was moved back into an existing '(' ... ')') has no '(' or ','
      // keystroke left to trigger on - surface the popup from the argument
      // text itself when the caret is inside an open call.
      refresh_lsp_signature_if_in_call();
    }
    else
    {
      hide_lsp_completion();
      // Signature help rides along the call syntax: an open paren starts a
      // call, each comma advances the highlighted parameter, and the closing
      // paren dismisses the popup.
      if (typed == '(')
      {
        request_lsp_signature_help('(');
      }
      else if (typed == ')')
      {
        hide_lsp_signature();
      }
      else if (typed == ',')
      {
        request_lsp_signature_help(',');
      }
    }
    needs_redraw = true;
    return;
  }
}

// ---------------------------------------------------------------------------
// The status-line prompt (the minibuffer)
// ---------------------------------------------------------------------------
//
// Emacs' minibuffer, kept on the editor's own last row. The ex commands are
// the way in -- :compile asks what to build ("Compile command: make -j8"),
// :isearch starts an incremental search ("I-search: main"), :findfile asks for
// a file ("Find file: src/render/frame.cpp") -- and the Alt+Q family in
// runtime/lua/features/keymaps.lua is the chord onto the same three (Alt+Q c /
// Alt+Q s / Alt+Q f, with Alt+Q k stopping a build).
// The prompt owns the row and the keyboard while it is up, and what was typed
// goes to the command that opened it -- which is why the compile prompt holds
// any shell command at all: the editor never parses it, the shell does.
//
// The keys are read here rather than in the buffer's editing path because a
// prompt is an input surface: Enter submits, Esc abandons, Tab completes, and
// every printable byte extends the input at the caret.

void Editor::open_minibuffer(MinibufferKind kind,
                             const std::string &label,
                             const std::string &prefill)
{
  show_minibuffer = true;
  minibuffer_kind = kind;
  minibuffer_label = label;
  minibuffer_input = prefill;
  minibuffer_cursor = (int)prefill.size();
  minibuffer_matches.clear();
  minibuffer_selected = 0;
  isearch_failed = false;
  needs_redraw = true;
}

void Editor::close_minibuffer()
{
  show_minibuffer = false;
  minibuffer_kind = MINIBUFFER_NONE;
  minibuffer_label.clear();
  minibuffer_input.clear();
  minibuffer_cursor = 0;
  minibuffer_matches.clear();
  minibuffer_selected = 0;
  needs_redraw = true;
}

void Editor::open_compile_prompt()
{
  open_minibuffer(MINIBUFFER_COMPILE, "Compile command: ", default_compile_command());
}

void Editor::open_find_file_prompt()
{
  open_minibuffer(MINIBUFFER_FIND_FILE, "Find file: ");
}

void Editor::open_isearch_prompt()
{
  if (buffers.empty())
  {
    return;
  }
  FileBuffer &buf = get_buffer();
  // Where Esc puts the caret back, and where a changed query searches from:
  // the incremental search restarts at the origin on every keystroke, so
  // deleting a character walks back to the shorter query's match.
  isearch_origin_line = buf.cursor.y;
  isearch_origin_col = buf.cursor.x;
  isearch_origin_scroll = buf.scroll_offset;
  open_minibuffer(MINIBUFFER_ISEARCH, isearch_regex ? "Regexp I-search: " : "I-search: ");
}

bool Editor::handle_minibuffer_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  (void)is_shift;
  if (!show_minibuffer)
  {
    return false;
  }
  auto input_changed = [&]()
  {
    if (minibuffer_kind == MINIBUFFER_ISEARCH)
    {
      isearch_step(1, true);
    }
    needs_redraw = true;
  };

  if (ch == 27)
  {
    if (minibuffer_kind == MINIBUFFER_ISEARCH)
    {
      isearch_cancel();
    }
    else
    {
      close_minibuffer();
    }
    return true;
  }
  if (ch == '\n' || ch == 13)
  {
    submit_minibuffer();
    return true;
  }
  if (ch == '\t' || ch == 9)
  {
    minibuffer_complete();
    return true;
  }
  if (ch == 127 || ch == 8)
  {
    if (minibuffer_cursor > 0)
    {
      minibuffer_input.erase((size_t)minibuffer_cursor - 1, 1);
      minibuffer_cursor--;
      input_changed();
    }
    return true;
  }
  // 1003 / 1004 are the decoded Right / Left arrow codes: the caret sits inside
  // the input, not only at its end.
  if (ch == 1004)
  {
    minibuffer_cursor = std::max(0, minibuffer_cursor - 1);
    needs_redraw = true;
    return true;
  }
  if (ch == 1003)
  {
    minibuffer_cursor = std::min((int)minibuffer_input.size(), minibuffer_cursor + 1);
    needs_redraw = true;
    return true;
  }
  // Ctrl+S / Ctrl+R keep searching, the way pressing them repeatedly does in
  // Emacs: the query stays as it is and the caret moves to the next match.
  if (is_ctrl && (ch == 's' || ch == 'S' || ch == 19))
  {
    if (minibuffer_kind == MINIBUFFER_ISEARCH)
    {
      isearch_step(1, false);
    }
    return true;
  }
  if (is_ctrl && (ch == 'r' || ch == 'R' || ch == 18))
  {
    if (minibuffer_kind == MINIBUFFER_ISEARCH)
    {
      isearch_step(-1, false);
    }
    return true;
  }
  if (is_alt && (ch == 'r' || ch == 'R'))
  {
    // The pattern toggle. Emacs spells it C-M-s / C-M-r; Alt+R is the chord
    // this editor has free, and the label says which mode is on.
    if (minibuffer_kind == MINIBUFFER_ISEARCH)
    {
      isearch_regex = !isearch_regex;
      minibuffer_label = isearch_regex ? "Regexp I-search: " : "I-search: ";
      isearch_step(1, true);
    }
    return true;
  }
  if (!is_ctrl && !is_alt && ch >= 32 && ch < 127)
  {
    minibuffer_input.insert((size_t)minibuffer_cursor, 1, (char)ch);
    minibuffer_cursor++;
    input_changed();
    return true;
  }
  // The prompt owns every other key while it is up: a stray chord must not edit
  // the buffer the prompt is being typed over.
  return true;
}

namespace
{
  // A match of `query` in `line` at or after `from`, with the length it covers
  // so the match can be highlighted. Case matters only when the query carries
  // an uppercase letter, which is how Emacs' own search reads it.
  bool minibuffer_search_line(const std::string &line,
                              const std::string &query,
                              int from,
                              bool regex_mode,
                              int &col,
                              int &len)
  {
    if (query.empty())
    {
      return false;
    }
    bool case_sensitive = false;
    for (unsigned char c : query)
    {
      if (std::isupper(c))
      {
        case_sensitive = true;
        break;
      }
    }
    const int start = std::clamp(from, 0, (int)line.size());
    if (regex_mode)
    {
      std::regex pattern;
      try
      {
        std::regex::flag_type flags = std::regex::ECMAScript;
        if (!case_sensitive)
        {
          flags |= std::regex::icase;
        }
        pattern = std::regex(query, flags);
      }
      catch (const std::regex_error &)
      {
        return false;
      }
      std::smatch match;
      const std::string tail = line.substr((size_t)start);
      if (!std::regex_search(tail, match, pattern) || match.length(0) == 0)
      {
        return false;
      }
      col = start + (int)match.position(0);
      len = (int)match.length(0);
      return true;
    }
    std::string haystack = line;
    std::string needle = query;
    if (!case_sensitive)
    {
      for (char &c : haystack)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      for (char &c : needle)
      {
        c = (char)std::tolower((unsigned char)c);
      }
    }
    const size_t found = haystack.find(needle, (size_t)start);
    if (found == std::string::npos)
    {
      return false;
    }
    col = (int)found;
    len = (int)needle.size();
    return true;
  }
} // namespace

void Editor::isearch_step(int direction, bool from_origin)
{
  if (buffers.empty())
  {
    return;
  }
  FileBuffer &buf = get_buffer();
  if (isearch_decoration != 0)
  {
    remove_decoration(buf, isearch_decoration);
    isearch_decoration = 0;
  }
  if (minibuffer_input.empty())
  {
    isearch_failed = false;
    needs_redraw = true;
    return;
  }
  const int line_count = (int)buf.line_count();
  if (line_count <= 0)
  {
    return;
  }

  // An extended query searches from the origin, a repeated Ctrl+S from just
  // past the match the caret is on: that is the difference between adding a
  // character and asking for the next hit.
  const int start_line =
      std::clamp(from_origin ? isearch_origin_line : buf.cursor.y, 0, line_count - 1);
  const int start_col = from_origin ? std::max(0, isearch_origin_col) : buf.cursor.x + 1;
  int found_line = -1;
  int found_col = 0;
  int found_len = 0;
  for (int step = 0; step < line_count; step++)
  {
    const int current = start_line + direction * step;
    if (current < 0 || current >= line_count)
    {
      break;
    }
    const std::string text = buf.line(current);
    const int limit = std::clamp(
        step == 0 ? start_col : (direction >= 0 ? 0 : (int)text.size()), 0, (int)text.size());
    if (direction >= 0)
    {
      int col = 0;
      int len = 0;
      if (minibuffer_search_line(text, minibuffer_input, limit, isearch_regex, col, len))
      {
        found_line = current;
        found_col = col;
        found_len = len;
        break;
      }
      continue;
    }
    // Backwards: the useful hit is the last one before the caret, so the whole
    // line is walked and the newest match inside the limit wins.
    int best_col = -1;
    int best_len = 0;
    int scan = 0;
    while (scan <= limit)
    {
      int col = 0;
      int len = 0;
      if (!minibuffer_search_line(text, minibuffer_input, scan, isearch_regex, col, len)
          || col >= limit)
      {
        break;
      }
      best_col = col;
      best_len = len;
      scan = col + std::max(1, len);
    }
    if (best_col >= 0)
    {
      found_line = current;
      found_col = best_col;
      found_len = best_len;
      break;
    }
  }

  if (found_line < 0)
  {
    isearch_failed = true;
    needs_redraw = true;
    return;
  }
  isearch_failed = false;
  buf.cursor = {found_col, found_line};
  buf.preferred_x = found_col;
  if (current_pane >= 0 && current_pane < (int)panes.size())
  {
    const int rows = std::max(1, pane_viewport_h(panes[(size_t)current_pane]));
    if (found_line < buf.scroll_offset)
    {
      buf.scroll_offset = found_line;
    }
    else if (found_line >= buf.scroll_offset + rows)
    {
      buf.scroll_offset = found_line - rows + 1;
    }
  }
  Decoration decoration;
  decoration.id = ++isearch_decoration_id;
  decoration.row = found_line;
  decoration.col = found_col;
  decoration.width = found_len;
  decoration.hl = "CurSearch";
  decoration.priority = 2;
  const std::uint64_t decoration_id = decoration.id;
  add_decoration(buf, std::move(decoration));
  isearch_decoration = decoration_id;
  needs_redraw = true;
}

void Editor::isearch_cancel()
{
  if (!buffers.empty())
  {
    FileBuffer &buf = get_buffer();
    if (isearch_decoration != 0)
    {
      remove_decoration(buf, isearch_decoration);
      isearch_decoration = 0;
    }
    buf.cursor = {isearch_origin_col, isearch_origin_line};
    buf.preferred_x = isearch_origin_col;
    buf.scroll_offset = isearch_origin_scroll;
  }
  close_minibuffer();
}

void Editor::submit_minibuffer()
{
  const MinibufferKind kind = minibuffer_kind;
  size_t start = minibuffer_input.find_first_not_of(" \t");
  size_t end = minibuffer_input.find_last_not_of(" \t");
  const std::string text = (start == std::string::npos)
                               ? std::string()
                               : minibuffer_input.substr(start, end - start + 1);

  if (kind == MINIBUFFER_ISEARCH)
  {
    // Accepting leaves the caret on the match the search reached.
    if (!buffers.empty() && isearch_decoration != 0)
    {
      remove_decoration(get_buffer(), isearch_decoration);
      isearch_decoration = 0;
    }
    close_minibuffer();
    return;
  }
  if (kind == MINIBUFFER_COMPILE)
  {
    close_minibuffer();
    if (text.empty())
    {
      set_message("No build command", true);
      return;
    }
    // The line is the shell's, not the editor's: `make`, `cmake --build build`,
    // `cargo test -- --nocapture` all arrive here as they were typed.
    compile_run(text);
    return;
  }
  if (kind == MINIBUFFER_FIND_FILE)
  {
    close_minibuffer();
    if (text.empty())
    {
      set_message("No file name", true);
      return;
    }
    namespace fs = std::filesystem;
    fs::path path(text);
    if (path.is_relative())
    {
      std::error_code ec;
      const fs::path base =
          (root_dir.empty() || root_dir == ".") ? fs::current_path(ec) : fs::path(root_dir);
      path = base / path;
    }
    open_file(path.lexically_normal().string());
    return;
  }
  close_minibuffer();
}

void Editor::minibuffer_complete()
{
  if (minibuffer_kind != MINIBUFFER_FIND_FILE)
  {
    return;
  }
  namespace fs = std::filesystem;
  const std::string typed = minibuffer_input;
  const size_t slash = typed.find_last_of("/\\");
  const std::string dir_part =
      slash == std::string::npos ? std::string() : typed.substr(0, slash + 1);
  const std::string stem = slash == std::string::npos ? typed : typed.substr(slash + 1);

  std::error_code ec;
  fs::path base(dir_part);
  if (base.is_relative())
  {
    const fs::path root =
        (root_dir.empty() || root_dir == ".") ? fs::current_path(ec) : fs::path(root_dir);
    base = root / base;
  }
  std::vector<std::string> names;
  for (const auto &entry : fs::directory_iterator(base, ec))
  {
    const std::string name = entry.path().filename().string();
    if (name.empty())
    {
      continue;
    }
    // Dotfiles stay out of the list unless the reader asked for them by typing
    // the dot: a completion that offers `.git` for `g` is noise.
    if (name[0] == '.' && (stem.empty() || stem[0] != '.'))
    {
      continue;
    }
    if (name.rfind(stem, 0) != 0)
    {
      continue;
    }
    names.push_back(name + (entry.is_directory(ec) ? "/" : ""));
  }
  std::sort(names.begin(), names.end());
  if (names.empty())
  {
    set_message("No match for \"" + stem + "\"", true);
    return;
  }

  const auto set_input = [&](const std::string &value)
  {
    minibuffer_input = dir_part + value;
    minibuffer_cursor = (int)minibuffer_input.size();
  };
  minibuffer_matches = names;
  needs_redraw = true;
  if (names.size() == 1)
  {
    minibuffer_selected = 0;
    set_input(names[0]);
    return;
  }
  // Several candidates: extend to what they all share, and once there is
  // nothing left to extend, Tab walks them.
  std::string common = names[0];
  for (const std::string &name : names)
  {
    size_t i = 0;
    while (i < common.size() && i < name.size() && common[i] == name[i])
    {
      i++;
    }
    common.resize(i);
  }
  if (common.size() > stem.size())
  {
    minibuffer_selected = 0;
    set_input(common);
    return;
  }
  if (minibuffer_selected < 0 || minibuffer_selected >= (int)names.size())
  {
    minibuffer_selected = 0;
  }
  set_input(names[(size_t)minibuffer_selected]);
  minibuffer_selected = (minibuffer_selected + 1) % (int)names.size();
}
