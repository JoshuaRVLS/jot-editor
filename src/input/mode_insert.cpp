#include "editor.h"
#include <cctype>

void Editor::handle_insert_mode(int ch, bool is_ctrl, bool is_shift,
                                bool is_alt) {
  auto switch_to_local_tab = [&](int target_index) -> bool {
    auto &pane = get_pane();
    if (pane.tab_buffer_ids.empty()) {
      return false;
    }
    target_index = std::clamp(target_index, 0,
                              (int)pane.tab_buffer_ids.size() - 1);
    int buffer_id = pane.tab_buffer_ids[target_index];
    if (buffer_id < 0 || buffer_id >= (int)buffers.size()) {
      return false;
    }
    pane.buffer_id = buffer_id;
    current_buffer = buffer_id;
    focus_state = FOCUS_EDITOR;
    clamp_cursor(buffer_id);
    ensure_cursor_visible();
    needs_redraw = true;
    return true;
  };

  auto cycle_local_tab = [&](int delta) -> bool {
    auto &pane = get_pane();
    if (pane.tab_buffer_ids.size() <= 1) {
      return false;
    }
    int current_idx = 0;
    for (int i = 0; i < (int)pane.tab_buffer_ids.size(); i++) {
      if (pane.tab_buffer_ids[i] == pane.buffer_id) {
        current_idx = i;
        break;
      }
    }
    int n = (int)pane.tab_buffer_ids.size();
    int next_idx = (current_idx + delta) % n;
    if (next_idx < 0) {
      next_idx += n;
    }
    return switch_to_local_tab(next_idx);
  };

  if (lsp_completion_visible) {
    if (ch == 1008) {
      lsp_completion_selected =
          std::max(0, lsp_completion_selected - 1);
      needs_redraw = true;
      return;
    }
    if (ch == 1009) {
      lsp_completion_selected = std::min(
          std::max(0, (int)lsp_completion_items.size() - 1),
          lsp_completion_selected + 1);
      needs_redraw = true;
      return;
    }
    if (ch == '\n' || ch == 13 || ch == '\t' || ch == 9) {
      if (apply_selected_lsp_completion()) {
        needs_redraw = true;
      }
      return;
    }
    if (ch == 27) {
      hide_lsp_completion();
      needs_redraw = true;
      return;
    }
  }

  if (is_ctrl && is_shift && (ch == 'l' || ch == 'L')) {
    select_current_line();
    return;
  }
  if (is_ctrl && is_shift && (ch == 's' || ch == 'S')) {
    int saved = 0;
    for (int i = 0; i < (int)buffers.size(); i++) {
      if (!buffers[i].filepath.empty() && buffers[i].modified &&
          save_buffer_at(i, false)) {
        saved++;
      }
    }
    if (saved > 0) {
      set_message("Saved " + std::to_string(saved) + " file(s)");
    } else {
      set_message("No modified saved files");
    }
    needs_redraw = true;
    return;
  }
  if (is_ctrl && is_shift && (ch == 't' || ch == 'T')) {
    reopen_last_closed_buffer();
    return;
  }
  // Ctrl+Tab / Ctrl+Shift+Tab: cycle pane-local tabs.
  if (is_ctrl && (ch == '\t' || ch == 9)) {
    if (is_shift) {
      if (cycle_local_tab(-1)) {
        return;
      }
    } else {
      if (cycle_local_tab(1)) {
        return;
      }
    }
  }
  if (is_ctrl && ch == 1017) { // Shift+Tab code path
    if (cycle_local_tab(-1)) {
      return;
    }
  }

  if (is_ctrl) {
    switch (ch) {
    case 'q':
    case 'Q': {
      if (panes.size() > 1) {
        close_pane();
      } else {
        bool unsaved = false;
        for (const auto &b : buffers) {
          if (b.modified) {
            unsaved = true;
            break;
          }
        }
        if (unsaved) {
          show_quit_prompt = true;
          needs_redraw = true;
        } else {
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
      copy();
      return;
    case 'x':
    case 'X':
      cut();
      needs_redraw = true;
      return;
    case 'v':
    case 'V':
      paste();
      needs_redraw = true;
      return;
    case 'b':
    case 'B':
      toggle_sidebar();
      return;
    case 'e':
    case 'E':
      telescope.open(root_dir.empty() ? "." : root_dir);
      waiting_for_space_f = false;
      needs_redraw = true;
      return;
    case 'f':
    case 'F':
      hide_lsp_completion();
      toggle_search();
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
      duplicate_line();
      return;
    case 'k':
    case 'K':
      delete_line();
      return;
    case '/':
    case '?':
    case 31:
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
    case 0:
    case ' ':
      request_lsp_completion(true);
      return;
    }
    return;
  }

  // Some terminals send Ctrl+Space as NUL without the ctrl modifier flag.
  if (ch == 0) {
    request_lsp_completion(true);
    return;
  }

  if (ch == 27) {
    hide_lsp_completion();
    clear_selection();
    needs_redraw = true;
    return;
  }

  // Terminal fallback mappings for control bytes that may arrive without the
  // ctrl modifier bit on some terminals.
  if (ch == 19) { // Ctrl+S
    hide_lsp_completion();
    save_file();
    needs_redraw = true;
    return;
  }

  // Terminal fallback mappings for Ctrl+Backspace / Ctrl+/
  if (ch == 23) {
    hide_lsp_completion();
    delete_word_backward();
    return;
  }
  if (ch == 31) {
    hide_lsp_completion();
    toggle_comment();
    return;
  }

  // VSCode-like line move shortcut.
  // Power user tab shortcuts (pane-local):
  // - Alt+, / Alt+.
  // - Alt+1..9
  // - Alt+0 (last tab)
  if (is_alt && (ch == ',' || ch == '<')) {
    if (cycle_local_tab(-1)) {
      return;
    }
  }
  if (is_alt && (ch == '.' || ch == '>')) {
    if (cycle_local_tab(1)) {
      return;
    }
  }
  if (is_alt && ch >= '1' && ch <= '9') {
    int target = (ch - '1');
    if (switch_to_local_tab(target)) {
      return;
    }
  }
  if (is_alt && ch == '0') {
    auto &pane = get_pane();
    if (!pane.tab_buffer_ids.empty() &&
        switch_to_local_tab((int)pane.tab_buffer_ids.size() - 1)) {
      return;
    }
  }

  // Power user action shortcuts (modeless-friendly).
  if (is_alt && (ch == 'w' || ch == 'W')) {
    close_buffer();
    return;
  }
  if (is_alt && ch == 'g') { // vim-ish: gg
    move_to_file_start(false);
    return;
  }
  if (is_alt && ch == 'G') { // vim-ish: G
    move_to_file_end(false);
    return;
  }
  if (is_alt && (ch == 'i' || ch == 'I')) { // vim-ish: ^
    move_to_line_smart_start(false);
    return;
  }
  if (is_alt && (ch == 'a' || ch == 'A')) { // vim-ish: $
    move_to_line_end(false);
    return;
  }
  if (is_alt && (ch == 'h' || ch == 'H')) { // vim-ish: b
    move_word_backward(false);
    return;
  }
  if (is_alt && (ch == 'l' || ch == 'L')) { // vim-ish: w
    move_word_forward(false);
    return;
  }
  if (is_alt && ch == 'd') { // vim-ish: dd
    delete_line();
    return;
  }
  if (is_alt && ch == 'D') { // convenience: duplicate current line
    duplicate_line();
    return;
  }
  if (is_alt && (ch == 'n' || ch == 'N')) {
    create_new_buffer();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 's' || ch == 'S')) {
    save_file();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 'f' || ch == 'F')) {
    toggle_search();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 'p' || ch == 'P')) {
    toggle_command_palette();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 'b' || ch == 'B')) {
    toggle_sidebar();
    return;
  }
  if (is_alt && (ch == 'm' || ch == 'M')) {
    toggle_minimap();
    needs_redraw = true;
    return;
  }
  if (is_alt && (ch == 't' || ch == 'T')) {
    open_theme_chooser();
    needs_redraw = true;
    return;
  }

  // VSCode-like line move shortcut.
  if (is_alt && ch == 1008) {
    move_line_up();
    return;
  }
  if (is_alt && ch == 1009) {
    move_line_down();
    return;
  }

  if (ch == 1008) {
    hide_lsp_completion();
    move_cursor(0, -1, is_shift);
    return;
  }
  if (ch == 1009) {
    hide_lsp_completion();
    move_cursor(0, 1, is_shift);
    return;
  }
  if (ch == 1010) {
    hide_lsp_completion();
    move_cursor(1, 0, is_shift);
    return;
  }
  if (ch == 1011) {
    hide_lsp_completion();
    move_cursor(-1, 0, is_shift);
    return;
  }
  if (ch == 1012) {
    hide_lsp_completion();
    move_to_line_smart_start(is_shift);
    return;
  }
  if (ch == 1013) {
    hide_lsp_completion();
    move_to_line_end(is_shift);
    return;
  }
  if (ch == 1015) {
    hide_lsp_completion();
    move_cursor(0, -10, is_shift);
    return;
  }
  if (ch == 1016) {
    hide_lsp_completion();
    move_cursor(0, 10, is_shift);
    return;
  }

  if (ch == 127 || ch == 8) {
    hide_lsp_completion();
    delete_char(false);
    needs_redraw = true;
    request_lsp_completion(false, '_');
    return;
  }
  if (ch == 1001) {
    hide_lsp_completion();
    delete_char(true);
    needs_redraw = true;
    return;
  }

  if (ch == '\n' || ch == 13) {
    hide_lsp_completion();
    new_line();
    needs_redraw = true;
    return;
  }

  if (ch == '\t' || ch == 9) {
    hide_lsp_completion();
    auto &buf = get_buffer();
    if (buf.selection.active) {
      indent_selection();
    } else {
      insert_char('\t');
    }
    needs_redraw = true;
    return;
  }

  // Shift+Tab (terminal escape \e[Z mapped in terminal.cpp)
  if (ch == 1017) {
    hide_lsp_completion();
    auto &buf = get_buffer();
    if (buf.selection.active) {
      outdent_selection();
    }
    needs_redraw = true;
    return;
  }

  if (ch >= 32 && ch < 1000) {
    auto &buf = get_buffer();
    if (buf.selection.active)
      delete_selection();
    insert_char((char)ch);
    char typed = (char)ch;
    bool trigger_completion =
        std::isalnum((unsigned char)typed) || typed == '_' || typed == '.' ||
        typed == ':' || typed == '>';
    if (trigger_completion) {
      request_lsp_completion(false, typed);
    } else {
      hide_lsp_completion();
    }
    needs_redraw = true;
    return;
  }
}
