#ifndef JOT_STATE_NAVIGATION_STATE_H
#define JOT_STATE_NAVIGATION_STATE_H

#include "jot/model/session.h" // JumpLocation
#include <vector>

// Navigation history: where the cursor has been, and the jump that is waiting
// for its file to finish opening.
//
// Every jump records where it landed, so back/forward walk the places the
// cursor has been rather than one LSP-only stack: the history has a cursor of
// its own (`jump_index`), and a new jump drops whatever was ahead of it, the
// way a new branch replaces a redo tail.
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct NavigationState
{
  std::vector<JumpLocation> jump_history;
  int jump_index = -1;
  // Set while a restore is in flight: restoring moves the cursor too, and that
  // must not be recorded as a new jump.
  bool jump_restoring = false;
  // A location waiting to be restored. The cursor can only be placed once the
  // file is open, and open_file can finish asynchronously, so the jump is armed
  // here and applied from open_file (and again by its caller, which is
  // idempotent -- the flag is cleared on apply).
  bool jump_pending = false;
  JumpLocation jump_pending_location;
};

#endif
