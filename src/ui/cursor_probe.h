#ifndef UI_CURSOR_PROBE_H
#define UI_CURSOR_PROBE_H

// The two writes of a cursor-position (DSR) probe: the only way left to learn the
// viewport size when ioctl and $COLUMNS report something stale (a multiplexer, a
// terminal whose size changed without a SIGWINCH).
//
// A probe has to *move* the cursor to read its position, and the one cell whose
// report is the whole viewport is the bottom-right corner (999;999 -- terminals
// clamp it). That park is a cursor move like any other: with the cursor on
// screen it flashes up in the corner for the length of the round trip, which is
// what a reader sees as the caret jumping to the bottom right every time the
// probe runs (main_loop's slow fallback runs it every couple of seconds, and a
// SIGWINCH runs it too). So the probe hides the cursor for the round trip and
// puts back exactly the visibility it found -- DECTCEM is not part of DECSC, so
// the restore has to say so itself.
//
// Kept free of Terminal so the bytes can be pinned by a test on their own.

#include <string>

namespace jot_ui
{
  // Hide the cursor, save it (DECSC), park it on (park_x, park_y) and ask for
  // its position (DSR). One write: the park can never land without the hide
  // that precedes it.
  std::string cursor_probe_park_bytes(int park_x, int park_y);

  // Put the cursor back (DECRC) and, when it was on screen before the probe,
  // show it again. Never the other way round: a show before the DECRC would
  // flash the caret in the corner this probe parked it in.
  std::string cursor_probe_restore_bytes(bool cursor_shown);
} // namespace jot_ui

#endif // UI_CURSOR_PROBE_H
