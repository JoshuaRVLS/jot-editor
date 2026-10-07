#ifndef JOT_FEATURES_STATUS_CLOCK_H
#define JOT_FEATURES_STATUS_CLOCK_H

// The two time labels the statusline shows: the local wall clock and how long
// this editing session has been running.
//
// Pure formatting, deliberately: both the painter (src/render/status_line.cpp)
// and the frame-loop gate that decides when the bar needs repainting
// (Editor::status_time_due_soon) have to agree on the exact text, and a
// repaint is bought by comparing those texts. Keeping the strings in one place
// is what makes "the label changed" the same question in both.
#include <ctime>
#include <string>

namespace status_clock
{
  // Local wall-clock time as the bar shows it: 24-hour "HH:MM" (e.g. "21:04").
  // Always five cells, so the segment does not jitter as the digits change.
  std::string format_clock(std::time_t wall);

  // How long the session has been running, in the coarsest unit that still
  // answers "how long?": seconds under a minute, minutes under an hour, then
  // hours with the minutes behind them ("2h 41m"). Negative input (a clock that
  // went backwards) reads as zero rather than a negative label.
  std::string format_duration(long long elapsed_ms);
} // namespace status_clock

#endif // JOT_FEATURES_STATUS_CLOCK_H
