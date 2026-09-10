// Grid resize plumbing shared by both frontends.
//
// One place owns what a size change means for the editor, so the terminal path
// (SIGWINCH/ioctl) and the GUI path (SDL window events) cannot drift apart:
// re-dimension the UI, re-fit every pane, and notify Lua exactly once.
//
// Resize *bursts* are coalesced by the callers' input drains (see
// Editor::pump_gui_events and the terminal stdin watcher), not here, because
// only the drains can see that several size changes arrived within one wake and
// that only the last one matters.
#include "editor.h"
#include "jot/lua/api.h"

void Editor::apply_resize(int cols, int rows)
{
  if (!ui)
  {
    return;
  }
  // UI::resize schedules a single full repaint of the next frame. That replaces
  // the old invalidate()-per-resize, which emitted a whole-screen clear
  // (ESC[2J) on every step of a live drag and is what made resizing flash.
  ui->resize(cols, rows);
  update_pane_layout();
  needs_redraw = true;
  if (lua_api)
  {
    lua_api->fire_autocmd("UIResize", "", -1);
  }
}
