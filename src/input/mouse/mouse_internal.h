// Shared helpers for the mouse modules: cursor ordering for selection
// clamping in the buffer interaction paths.
#pragma once

#include "jot/types.h"

namespace mouse_internal
{
  inline int compare_cursor_pos(const Cursor &a, const Cursor &b)
{
if (a.y != b.y)
  return (a.y < b.y) ? -1 : 1;
if (a.x != b.x)
  return (a.x < b.x) ? -1 : 1;
return 0;
}
} // namespace mouse_internal
