// The statusline's two time chips: the local clock and how long was coded today
// (features/status_clock.cpp for the formatters, features/coding_time.h for the
// number the second chip shows, and the segments in src/render/status_line.cpp).
//
// Three things are pinned here. The formatters, because they are pure and the
// unit boundaries are exactly where a label would read wrong ("59m" becoming
// "1h 0m", a 12-hour value leaking an AM/PM-shaped hour). The wiring, so the
// chips are available and the two config keys turn them on and off (a fresh
// install ships with both off -- the bar is the file's news). And the
// repaint gate, which is the part that cannot be seen in a screenshot: these
// are the only labels on the bar that move with no input at all, so the frame
// loop -- and not a timer of its own -- has to notice them move. Planting a
// total on the local store is what stands in for having waited.
#include "editor.h"
#include "extras/status_clock.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_status_clock_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  long long now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  // The painted status row, as text. The bar is the last row of the grid.
  std::string status_row(Editor &e)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    const int y = e.ui_height_for_test() - 1;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  }

  // Every row, for the cases that assert a chip is gone from the whole screen
  // (a dropped segment leaves no trace, but a stale one could be anywhere).
  std::string whole_screen(Editor &e)
  {
    UI *ui = e.ui_for_test();
    std::string out;
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      for (int x = 0; x < e.ui_width_for_test(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        out += cell ? cell->ch : " ";
      }
      out += "\n";
    }
    return out;
  }

  // The Nerd Fonts glyphs the chips lead with (verified against the patched
  // font's cmap: nf-md-clock_outline / nf-md-timer_outline). Asserted so an
  // icon cannot silently disappear from a chip that is otherwise correct.
  const char *kClockGlyph = "\U000F0150";
  const char *kTimerGlyph = "\U000F051B";
} // namespace

TEST_CASE("Status clock: the duration reads in the coarsest unit that answers", "[jot][statusline]")
{
  using status_clock::format_duration;
  REQUIRE(format_duration(0) == "0s");
  REQUIRE(format_duration(999) == "0s");
  REQUIRE(format_duration(1000) == "1s");
  REQUIRE(format_duration(42000) == "42s");
  REQUIRE(format_duration(59999) == "59s");
  REQUIRE(format_duration(60000) == "1m");
  REQUIRE(format_duration(61000) == "1m");
  REQUIRE(format_duration(3599000) == "59m");
  REQUIRE(format_duration(3600000) == "1h 00m");
  REQUIRE(format_duration(3661000) == "1h 01m");
  REQUIRE(format_duration(3600000LL * 100) == "100h 00m");
  // A clock that went backwards (a suspend/resume the steady clock did not
  // carry) reads as zero rather than as a negative label.
  REQUIRE(format_duration(-5000) == "0s");
}

TEST_CASE("Status clock: local time is a fixed-width 24-hour label", "[jot][statusline]")
{
  // mktime against a broken-down local time, so the case does not depend on the
  // machine's zone the way an epoch constant would.
  std::tm tm{};
  tm.tm_year = 126; // 2026
  tm.tm_mon = 8;    // September
  tm.tm_mday = 21;
  tm.tm_sec = 5;
  tm.tm_isdst = -1;

  tm.tm_hour = 21;
  tm.tm_min = 4;
  REQUIRE(status_clock::format_clock(std::mktime(&tm)) == "21:04");

  // Midnight, and an hour that a 12-hour format would have collapsed to "9".
  tm.tm_hour = 0;
  tm.tm_min = 0;
  REQUIRE(status_clock::format_clock(std::mktime(&tm)) == "00:00");

  tm.tm_hour = 9;
  tm.tm_min = 4;
  REQUIRE(status_clock::format_clock(std::mktime(&tm)) == "09:04");

  // Always five cells: the segment must not jitter as the digits change.
  REQUIRE(status_clock::format_clock(std::mktime(&tm)).size() == 5);
}

TEST_CASE("Status clock: both chips are painted at the right end of the bar", "[jot][statusline]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 30);
  // Both chips ship off; the keys are what a user puts back.
  e.config_set_for_test("status_clock", "true");
  e.config_set_for_test("status_coding_time", "true");
  e.set_coding_time_ms_for_test(61000);
  e.request_redraw_for_test();
  e.render_for_test();

  const std::string row = status_row(e);
  // The clock is read from the wall clock, so the expected label is the same
  // formatter the painter used; the coding chip comes from the local store,
  // which the hook put in the "1m" bucket.
  REQUIRE(row.find(status_clock::format_clock(std::time(nullptr))) != std::string::npos);
  REQUIRE(row.find(" 1m ") != std::string::npos);
  REQUIRE(row.find(kClockGlyph) != std::string::npos);
  REQUIRE(row.find(kTimerGlyph) != std::string::npos);

  // An hour in, and the label follows the same local total.
  e.set_coding_time_ms_for_test(3725000);
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(status_row(e).find(" 1h 02m ") != std::string::npos);
}

TEST_CASE("Status clock: the config keys take the chips back off", "[jot][statusline]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 30);
  e.set_coding_time_ms_for_test(61000);

  e.config_set_for_test("status_clock", "false");
  e.config_set_for_test("status_coding_time", "false");
  e.render_for_test();
  const std::string without = whole_screen(e);
  REQUIRE(without.find(kClockGlyph) == std::string::npos);
  REQUIRE(without.find(kTimerGlyph) == std::string::npos);
  REQUIRE(without.find(" 1m ") == std::string::npos);

  // render() is a no-op unless a frame is wanted, exactly as in the running
  // editor, so switching a setting back on has to ask for one.
  e.config_set_for_test("status_clock", "true");
  e.config_set_for_test("status_coding_time", "true");
  e.request_redraw_for_test();
  e.render_for_test();
  const std::string with = whole_screen(e);
  REQUIRE(with.find(kClockGlyph) != std::string::npos);
  REQUIRE(with.find(kTimerGlyph) != std::string::npos);
}

TEST_CASE("Status clock: a moved label buys the frame that shows it", "[jot][statusline]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 30);
  e.render_frame_for_test();

  // With both chips off there is nothing to keep fresh, so a frame asks for no
  // more frames: this is the branch that would otherwise repaint an idle editor
  // once a second forever. The local total is still being advanced -- that is
  // what the store is for -- but a label nobody is showing buys no frame.
  e.config_set_for_test("status_clock", "false");
  e.config_set_for_test("status_coding_time", "false");
  e.set_coding_time_ms_for_test(61000);
  e.clear_needs_redraw_for_test();
  e.render_frame_for_test();
  REQUIRE_FALSE(e.needs_redraw_for_test());

  // On, and with the local total moved on since the label that was last up: the
  // frame that notices the move paints the new label itself. That is what keeps
  // the chips current with nothing typed, and it is why the ask sits before the
  // paint rather than after it (main_loop.cpp). The label goes from the seconds
  // it read at the frame above to the moved-over minute, so the move is certain
  // -- as is the paint, which is asserted on the row rather than on a pending
  // redraw: there is nothing left pending.
  e.config_set_for_test("status_clock", "true");
  e.config_set_for_test("status_coding_time", "true");
  e.render_frame_for_test();
  e.clear_needs_redraw_for_test();
  e.set_coding_time_ms_for_test(121000);
  e.render_frame_for_test();
  REQUIRE(status_row(e).find(" 2m ") != std::string::npos);
}
