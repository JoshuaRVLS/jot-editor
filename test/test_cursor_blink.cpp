// The caret's blink clock (src/ui/cursor_blink.cpp).
//
// Before this existed the two frontends disagreed and neither honoured the
// setting: the terminal handed the phase to the emulator's blinking DECSCUSR
// shape (so cursor_blink_ms did nothing), while the GUI's visibility flag never
// toggled, leaving the caret permanently solid. One clock now drives both, and
// these cases pin its phase, its hold, and its resumption after input.
#include "ui/cursor_blink.h"

#include <catch2/catch_test_macros.hpp>

using jot_ui::blink_phase_visible;

TEST_CASE("Cursor blink alternates on the configured period", "[jot]")
{
  const long long anchor = 10'000;
  const int period = 500;

  // Visible half of the cycle.
  REQUIRE(blink_phase_visible(anchor, anchor, period, false));
  REQUIRE(blink_phase_visible(anchor + 1, anchor, period, false));
  REQUIRE(blink_phase_visible(anchor + 499, anchor, period, false));
  // Hidden half.
  REQUIRE_FALSE(blink_phase_visible(anchor + 500, anchor, period, false));
  REQUIRE_FALSE(blink_phase_visible(anchor + 999, anchor, period, false));
  // And back: the phase is periodic, not a one-shot fade-out.
  REQUIRE(blink_phase_visible(anchor + 1000, anchor, period, false));
  REQUIRE_FALSE(blink_phase_visible(anchor + 1500, anchor, period, false));
  REQUIRE(blink_phase_visible(anchor + 2000, anchor, period, false));
}

TEST_CASE("Cursor blink rate follows the setting", "[jot]")
{
  const long long anchor = 0;
  // A slower period must actually blink slower: at 300 ms the caret has already
  // hidden, while at 800 ms it is still in its visible half.
  REQUIRE_FALSE(blink_phase_visible(300, anchor, 300, false));
  REQUIRE(blink_phase_visible(300, anchor, 800, false));
  REQUIRE_FALSE(blink_phase_visible(800, anchor, 800, false));
}

TEST_CASE("A zero or negative period means a solid caret", "[jot]")
{
  // 0 is the documented "never blink" value from cursor_blink_ms.
  REQUIRE(blink_phase_visible(0, 0, 0, false));
  REQUIRE(blink_phase_visible(123'456, 0, 0, false));
  REQUIRE(blink_phase_visible(123'456, 0, -5, false));
  // Even at a moment the phase would otherwise be hidden.
  REQUIRE(blink_phase_visible(500, 0, 0, false));
}

TEST_CASE("Holding the caret visible overrides the phase", "[jot]")
{
  // The hold is what makes the caret solid while typing (restart_blink sets it)
  // and for the steady_* cursor styles.
  REQUIRE(blink_phase_visible(500, 0, 500, true));
  REQUIRE(blink_phase_visible(1500, 0, 500, true));
  REQUIRE_FALSE(blink_phase_visible(500, 0, 500, false));
}

TEST_CASE("A caret that just moved lands visible", "[jot]")
{
  // restart_blink re-anchors to now on every keystroke and caret move, so the
  // caret must be visible immediately after -- otherwise typing would flicker it
  // out from under the user's cursor.
  const long long now = 987'654;
  REQUIRE(blink_phase_visible(now, now, 500, false));
  REQUIRE(blink_phase_visible(now + 10, now, 500, false));
}

TEST_CASE("Blink survives an anchor from the future", "[jot]")
{
  // Clock skew (or a stale anchor) must not wrap into a random half: the phase
  // is treated as just started.
  REQUIRE(blink_phase_visible(1'000, 5'000, 500, false));
  // Well past the anchor, the normal phase resumes.
  REQUIRE_FALSE(blink_phase_visible(5'500, 5'000, 500, false));
}
