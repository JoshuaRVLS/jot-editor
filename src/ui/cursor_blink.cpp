#include "ui/cursor_blink.h"

namespace jot_ui
{
bool blink_phase_visible(long long now_ms,
                         long long anchor_ms,
                         int period_ms,
                         bool hold_visible)
{
  if (hold_visible || period_ms <= 0)
  {
    return true;
  }
  long long elapsed = now_ms - anchor_ms;
  if (elapsed < 0)
  {
    // Clock skew (or an anchor from a different clock): treat the phase as just
    // started instead of wrapping to a random half.
    elapsed = 0;
  }
  return ((elapsed / period_ms) % 2) == 0;
}
} // namespace jot_ui
