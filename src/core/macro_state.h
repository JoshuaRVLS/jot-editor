#ifndef JOT_STATE_MACRO_STATE_H
#define JOT_STATE_MACRO_STATE_H

#include <map>
#include <vector>

// Recordable macros (src/input/macros.cpp): one keystroke as the key path sees
// it, the registers those keystrokes are filed under, and the bookkeeping the
// recorder and the player need.
//
// A macro stores *keys*, not the edits they produced. Replaying feeds them back
// through the frontend's key path (events/terminal_events.cpp), so a replayed
// chord is matched by exactly the keymap the live one was -- a Lua key too --
// and the macro does whatever that keymap does now, on whatever buffer is in
// front of the user.
struct MacroKey
{
  int ch = 0;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

struct MacroState
{
  // One register per letter a-z. A recording appends to the register it was
  // started on; replay reads a copy, so a macro that starts another recording
  // cannot invalidate the list being walked.
  std::map<char, std::vector<MacroKey>> macro_registers;
  // The register `:macrorecord` writes to (and the one Alt+Z q uses when no
  // name is given), the last one played (`@`), and 0 for "none yet".
  char macro_register = 'q';
  char macro_last = 0;
  bool macro_recording = false;
  // True while a replay is running: the recorder ignores everything then, so a
  // macro cannot record itself (and a play inside a play is refused).
  bool macro_replaying = false;
  // The Alt+Z press is waiting for its command letter (the native fallback in
  // src/input/modes/dispatch.cpp; the Lua keymap handles the chord first when
  // one is loaded).
  bool macro_chord_armed = false;
  // The recorder's own filter: the letter after an Alt+Z press is a macro
  // command, not part of the recording, so it is skipped (and the flag cleared
  // by the very next key, whichever path consumed the chord).
  bool macro_skip_next_key = false;
};

#endif // JOT_STATE_MACRO_STATE_H
