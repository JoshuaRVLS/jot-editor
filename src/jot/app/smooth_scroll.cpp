// neoscroll.nvim's smooth scrolling, driven by jot's frame clock.
//
// The animation only ever moves the viewport (scroll_offset), never the caret:
// that is what the mouse wheel does upstream's `<C-e>`/`<C-y>` mapping does,
// and it keeps the caret semantics of every other navigation path in the
// editor untouched. Anything that scrolls by other means -- a jump, a fold,
// `ensure_cursor_visible` dragging the viewport back to the caret -- invalidates
// the animation's premise, and the drift probe in advance_smooth_scroll() drops
// it rather than fighting over the offset.
#include "editor.h"
#include "folding.h"
#include "smooth_scroll.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
  long long steady_now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // The viewport offset `lines` visible lines away from `from`, clamped to the
  // pane's scroll limits: fold-aware, and stopped at the last screenful so a
  // scroll never parks the text past the end of the file (upstream's stop_eof).
  //
  // The caller's prepared view is used for both halves. Preparing one here
  // instead would index the collapsed ranges once per *line* the step crosses
  // -- five full scans of a file's range vector per wheel notch on a file with
  // a range per brace pair, which is what made wheel scrolling cost more than
  // the frame it triggered.
  int offset_after_lines(const FileBuffer &buf,
                         const Folding::FoldView &fold_view,
                         int view_h,
                         int from,
                         int lines)
  {
    const int line_count = (int)buf.line_count();
    const int moved = fold_view.advance_visible_lines(from, lines, line_count);
    return fold_view.clamp_scroll_offset(moved, view_h, line_count);
  }
} // namespace

bool Editor::scroll_view_smooth(int lines, int base_ms)
{
  if (lines == 0 || panes.empty())
  {
    return false;
  }

  SplitPane &pane = get_pane();
  FileBuffer &buf = get_buffer(pane.buffer_id);
  refresh_folds(buf);

  const int view_h = std::max(1, pane.h - tab_height);
  const int before = buf.scroll_offset;
  // One prepared fold view for the whole gesture step: both the walk to the
  // destination and the clamp answer from it.
  const Folding::FoldView fold_view(buf.fold_ranges);
  const int destination = offset_after_lines(buf, fold_view, view_h, before, lines);
  const int step = destination - before;

  // The GUI frontend already eases each pane's content shift pixel by pixel
  // (ui/gui's neovide-style slide), so a second, line-stepped animation on top
  // would only multiply the viewports it has to retain. Smooth scrolling is a
  // terminal feature.
  if (!smooth_scroll_enabled_ || gui_mode)
  {
    cancel_smooth_scroll();
    if (step == 0)
    {
      return false;
    }
    buf.scroll_offset = destination;
    return true;
  }

  // An animation whose pane moved to another buffer, or whose viewport was
  // moved by something else since the last frame, is no longer describing
  // what is on screen: drop it and start over from where the viewport is.
  if (smooth_scroll_.active
      && (smooth_scroll_.buffer_id != pane.buffer_id
          || buf.scroll_offset != smooth_scroll_.applied))
  {
    cancel_smooth_scroll();
  }

  if (step == 0)
  {
    // Already at the edge: there is no further distance to hand the animation,
    // and a running one keeps the schedule it already has.
    return smooth_scroll_.active && smooth_scroll_.in_flight.target != before;
  }

  if (!smooth_scroll_.active)
  {
    smooth_scroll_.active = true;
    smooth_scroll_.buffer_id = pane.buffer_id;
    smooth_scroll_.in_flight = SmoothScroll::InFlight{};
    smooth_scroll_.in_flight.target = before;
    smooth_scroll_.easing = smooth_scroll_easing_;
    smooth_scroll_.applied = before;
  }

  smooth_scroll_.notch_lines = std::max(1, std::abs(step));
  SmoothScroll::merge_target(smooth_scroll_.in_flight, before, step);

  const int pending = smooth_scroll_.in_flight.target - before;
  if (pending == 0)
  {
    cancel_smooth_scroll();
    return false;
  }

  // Re-anchor at the current position for the distance still to cover. Upstream
  // re-divides the remaining lines over the mapping's duration when a scroll
  // arrives mid-animation; anchoring the same way keeps the position continuous
  // and the per-notch speed constant (with the default linear easing the two
  // are the same motion).
  smooth_scroll_.from = before;
  smooth_scroll_.applied = before;
  smooth_scroll_.start_ms = steady_now_ms();
  smooth_scroll_.duration_ms = SmoothScroll::duration_ms(
      base_ms, smooth_scroll_duration_multiplier_, pending, smooth_scroll_.notch_lines);
  return true;
}

bool Editor::advance_smooth_scroll(long long now_ms)
{
  if (!smooth_scroll_.active)
  {
    return false;
  }

  // The pane left this buffer (tab switch, file opened over it) or the
  // viewport moved on its own (a jump, a fold, the caret pulled into view):
  // either way this animation is stale. It is dropped without writing an
  // offset, so whoever moved the viewport keeps the last word.
  if (panes.empty())
  {
    cancel_smooth_scroll();
    return false;
  }
  SplitPane &pane = get_pane();
  if (pane.buffer_id != smooth_scroll_.buffer_id || pane.buffer_id < 0
      || pane.buffer_id >= (int)buffers.size() || !smooth_scroll_enabled_)
  {
    cancel_smooth_scroll();
    return false;
  }
  FileBuffer &buf = buffers[(size_t)pane.buffer_id];
  if (buf.scroll_offset != smooth_scroll_.applied)
  {
    cancel_smooth_scroll();
    return false;
  }

  const long long elapsed = std::max<long long>(0, now_ms - smooth_scroll_.start_ms);
  const double progress = smooth_scroll_.duration_ms > 0
                              ? std::min(1.0, (double)elapsed / (double)smooth_scroll_.duration_ms)
                              : 1.0;
  const double fraction = SmoothScroll::position_fraction(smooth_scroll_.easing, progress);
  const double span = (double)(smooth_scroll_.in_flight.target - smooth_scroll_.from);
  const int view_h = std::max(1, pane.h - tab_height);
  const int wanted = (int)std::lround((double)smooth_scroll_.from + span * fraction);
  const int clamped =
      Folding::clamp_scroll_offset(buf.fold_ranges, wanted, view_h, (int)buf.line_count());

  const bool moved = clamped != buf.scroll_offset;
  buf.scroll_offset = clamped;
  smooth_scroll_.applied = clamped;
  if (progress >= 1.0)
  {
    // Settled: the viewport is on the target and no tick is owed.
    cancel_smooth_scroll();
  }
  return moved;
}

void Editor::cancel_smooth_scroll()
{
  smooth_scroll_.active = false;
  smooth_scroll_.buffer_id = -1;
  smooth_scroll_.from = 0;
  smooth_scroll_.applied = -1;
  smooth_scroll_.notch_lines = 1;
  smooth_scroll_.in_flight = SmoothScroll::InFlight{};
  smooth_scroll_.start_ms = 0;
  smooth_scroll_.duration_ms = 0;
}
