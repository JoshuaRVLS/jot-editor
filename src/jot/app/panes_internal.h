// Small geometry helpers shared by the pane layout/resize/navigation modules.
#pragma once

#include "jot/editor_models.h"
#include <algorithm>

namespace pane_internal
{
inline constexpr int kMinPaneWidth = 12;
inline constexpr int kMinPaneHeight = 3;

inline int pane_center_x(const SplitPane &pane)
{
  return pane.x + pane.w / 2;
}

inline int pane_center_y(const SplitPane &pane)
{
  return pane.y + pane.h / 2;
}

inline int overlap_amount(int a_start, int a_len, int b_start, int b_len)
{
  int a_end = a_start + a_len;
  int b_end = b_start + b_len;
  return std::max(0, std::min(a_end, b_end) - std::max(a_start, b_start));
}
} // namespace pane_internal