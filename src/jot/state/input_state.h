#ifndef JOT_STATE_INPUT_STATE_H
#define JOT_STATE_INPUT_STATE_H

#include "jot/model/buffer.h" // Cursor
#include "jot/model/input.h"  // MouseSelectionMode
#include <vector>

// What the input path remembers between events: the live mouse selection, the
// click/hover tracking that turns a second press into a double-click and a
// resting pointer into a highlight, and the keystroke bookkeeping (the recent
// keys the keymap chords are matched against, and the press counter).
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct InputState
{
  bool mouse_selecting = false;
  MouseSelectionMode mouse_selection_mode = MOUSE_SELECT_CHAR;
  Cursor mouse_start;
  Cursor mouse_anchor_end;
  int mouse_press_screen_x = 0;
  int mouse_press_screen_y = 0;
  int mouse_press_buf_x = 0;
  int mouse_press_buf_y = 0;
  bool mouse_drag_started = false;

  // Double-click detection per surface: the last press time/position, and how
  // many presses landed in a row (2 = word, 3 = line).
  long long last_left_click_ms;
  Cursor last_left_click_pos;
  int last_left_click_count = 0;

  long long last_sidebar_click_ms;
  int last_sidebar_click_row = 0;
  long long last_git_panel_click_ms = 0;
  int last_git_panel_click_row = -1;
  // Mouse hover tracking (motion events): the model row under the pointer in
  // the git panel and the hovered right-dock tab, -1 when none. Purely
  // visual -- selection is never overwritten by hover.
  int git_panel_hover_row = -1;
  int right_panel_hover_tab = -1;
  long long last_tab_click_ms;
  int last_tab_clicked_index = 0;

  std::vector<int> recent_keys;
  long long keyboard_press_count;
  int easter_egg_timer = 0;
};

#endif
