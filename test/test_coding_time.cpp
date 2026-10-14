// The local coding-time store and the statusline chip it feeds
// (features/coding_time.h, and the segments in src/render/status_line.cpp).
//
// The store is what keeps the chip worth showing with WakaTime off: it is the
// time coded today in the workspace in front of the user, carried over from
// earlier sessions. Two halves are pinned here, and they are the two that would
// go wrong quietly. The rule -- time is credited between two activity ticks, and
// a gap past the cap is dropped -- because a tracker that counts a night of
// nothing produces a number nobody can use. And the storage, because the whole
// point of it is that reopening the editor later today continues the total
// rather than restarting it.
#include "core/editor.h"
#include "extras/coding_time/probe.h"
#include "extras/status_clock.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

using namespace coding_time;

namespace
{
  const char *kClockGlyph = "\U000F0150"; // nf-md-clock_outline (WakaTime)
  const char *kTimerGlyph = "\U000F051B"; // nf-md-timer_outline (the local store)

  constexpr long long kSecond = 1000;
  constexpr long long kMinute = 60 * kSecond;

  std::string seed_config_home()
  {
    char home[] = "/tmp/jot_coding_time_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
    return home;
  }

  std::string read_file(const std::string &path)
  {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
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
} // namespace

TEST_CASE("Coding time: a gap past the idle cap is not work", "[jot][coding-time]")
{
  // The plain case: the gap since the last credit is credited whole when the
  // user was last seen inside it, and only the gap -- not the whole session.
  REQUIRE(credit_for(kSecond, 0, 0) == kSecond);
  REQUIRE(credit_for(10 * kSecond, 0, 9 * kSecond) == kSecond);
  // Never more than the cap, however long the editor has been up.
  REQUIRE(credit_for(10 * kMinute, 9 * kMinute, 0) == kIdleCapMs);
  // Past the cap the user has walked away, and none of it is charged: this is
  // the line that keeps a machine left on overnight out of the total.
  REQUIRE(credit_for(kIdleCapMs + 1, 0, 0) == 0);
  REQUIRE(credit_for(8 * 60 * kMinute, 0, 0) == 0);
  // Exactly at the cap the user is still here.
  REQUIRE(credit_for(kIdleCapMs, 0, 0) == kIdleCapMs);
  // A clock that went backwards, or a second look inside the same instant.
  REQUIRE(credit_for(5000, 5000, 5000) == 0);
  REQUIRE(credit_for(4000, 5000, 5000) == 0);
}

TEST_CASE("Coding time: the counter credits between ticks and stops when idle",
          "[jot][coding-time]")
{
  Counter counter;
  // Opened at t=0 with no activity: the first tick credits what has passed.
  counter.last_activity_ms = 0;
  counter.last_credit_ms = 0;
  REQUIRE(counter.advance(10 * kSecond) == 10 * kSecond);
  REQUIRE(counter.live_ms(12 * kSecond) == 12 * kSecond); // a look, not a commit
  REQUIRE(counter.total_ms == 10 * kSecond);

  // Idle past the cap: the total stops where it was, however often it is looked
  // at. That is the difference between this and a wall-clock timer.
  counter.advance(2 * kMinute + 10 * kSecond);
  REQUIRE(counter.total_ms == 10 * kSecond);
  REQUIRE(counter.live_ms(60 * kMinute) == 10 * kSecond);

  // A keystroke after the idle stretch restarts the window: the absence is not
  // charged, and counting resumes from here.
  counter.mark_activity(60 * kMinute);
  REQUIRE(counter.total_ms == 10 * kSecond);
  counter.advance(60 * kMinute + kSecond);
  REQUIRE(counter.total_ms == 11 * kSecond);
}

TEST_CASE("Coding time: a live look at the window counts the time since the tick",
          "[jot][coding-time]")
{
  // What the bar paints is `live_ms`, so the label moves every second while the
  // user is here instead of only when the frame loop commits.
  Counter counter;
  counter.mark_activity(0);
  REQUIRE(counter.live_ms(5 * kSecond) == 5 * kSecond);
  counter.advance(5 * kSecond);
  REQUIRE(counter.total_ms == 5 * kSecond);
  REQUIRE(counter.live_ms(6 * kSecond) == 6 * kSecond);
  // Nothing double-counted: committing what the live reading already showed
  // leaves the total where the label was.
  counter.mark_activity(6 * kSecond);
  REQUIRE(counter.total_ms == 6 * kSecond);
}

TEST_CASE("Coding time: the file is one line per workspace and day", "[jot][coding-time]")
{
  Totals totals;
  const std::string here = key_for("/work/jot", "2026-09-28");
  set_total(totals, here, 90 * kMinute);
  set_total(totals, key_for("/work/other", "2026-09-28"), 5 * kMinute);
  set_total(totals, key_for("/work/jot", "2026-09-27"), 30 * kMinute);
  REQUIRE(total_for(totals, here) == 90 * kMinute);
  REQUIRE(total_for(totals, key_for("/work/jot", "2026-09-26")) == 0);

  const Totals reread = parse(serialize(totals));
  REQUIRE(reread == totals);
  REQUIRE(workspace_of(here) == "/work/jot");
  REQUIRE(serialize(totals).find("/work/jot\t2026-09-28\t5400000\n") != std::string::npos);

  // A total that falls to nothing takes its line with it rather than leaving an
  // empty row to grow forever.
  Totals empty;
  set_total(empty, here, 0);
  REQUIRE(empty.empty());

  // A line the parser cannot read costs the rest of the file nothing, and a key
  // is rebuilt from its fields so a hand edit's stray blank is not a second
  // entry for the same place.
  const Totals ragged = parse("nonsense\n/work/jot\t2026-09-28\tx\n/ work \t2026-09-28\t1000\n\n");
  REQUIRE(ragged.size() == 1);
  REQUIRE(total_for(ragged, key_for("/ work ", "2026-09-28")) == 1000);
}

TEST_CASE("Coding time: one project is one key, however it is spelled", "[jot][coding-time]")
{
  const std::string day = "2026-09-28";
  const std::string project = key_for("/work/jot", day);
  // The explorer's absolute path and the same path with a trailing separator are
  // one place, not two: one project's day cannot live on two lines, or neither
  // line is the total the chip reports.
  REQUIRE(key_for("/work/jot/", day) == project);
  REQUIRE(key_for("/work//jot", day) == project);
  // A session started in the project's own directory keeps the "." it was
  // opened with, and that is the same place as its absolute path.
  REQUIRE(key_for(".", day) == key_for(fs::current_path().string(), day));

  // Rows that fold onto one key add up, so a file already carrying two spellings
  // still reports the day's real total.
  const std::string text = "/work/jot\t" + day + "\t60000\n/work/jot/\t" + day
                           + "\t30000\n/work/jot\t2026-09-27\t1000\n";
  const Totals totals = parse(text);
  REQUIRE(totals.size() == 2);
  REQUIRE(total_for(totals, project) == 90000);
  // A row of nothing adds nothing rather than dropping the day with it.
  REQUIRE(total_for(parse("/work/jot\t" + day + "\t0\n"), project) == 0);
  REQUIRE(parse("/work/jot\t" + day + "\t0\n").empty());
}

TEST_CASE("Coding time: the store lives under the config home", "[jot][coding-time]")
{
  const std::string home = seed_config_home();
  REQUIRE(store_path() == (fs::path(home) / "configs" / "coding_time.tsv").string());

  Totals totals;
  set_total(totals, key_for("/work/jot", "2026-09-28"), 12 * kMinute);
  REQUIRE(save_totals(store_path(), totals));

  Totals loaded;
  REQUIRE(load_totals(store_path(), loaded));
  REQUIRE(loaded == totals);
  // A file that is not there is an empty store, not a failed read of one.
  loaded.clear();
  REQUIRE_FALSE(load_totals(store_path() + ".missing", loaded));
  REQUIRE(loaded.empty());
}

TEST_CASE("Coding time: the date is the local day", "[jot][coding-time]")
{
  // Two moments on the same local day share a key, and the label is the day the
  // user is living in rather than a UTC one that could be tomorrow.
  const std::time_t now = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char expected[16];
  std::snprintf(
      expected, sizeof(expected), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  REQUIRE(local_date((long long)now) == expected);
  REQUIRE(local_date((long long)now).size() == 10);
}

TEST_CASE("Coding time: the chip shows the local total for this workspace", "[jot][coding-time]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 30);
  e.set_workspace_root_for_test("/work/jot");
  // The chip ships off; the key is what puts it on the bar.
  e.config_set_for_test("status_coding_time", "true");
  e.set_coding_time_ms_for_test(12 * kMinute);
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(status_row(e).find(" 12m ") != std::string::npos);
  REQUIRE(status_row(e).find(kTimerGlyph) != std::string::npos);
}

TEST_CASE("Coding time: WakaTime's total is what the chip shows when it has one",
          "[jot][coding-time]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  e.apply_resize_for_test(120, 30);
  e.set_workspace_root_for_test("/work/jot");
  e.config_set_for_test("status_coding_time", "true");
  e.set_coding_time_ms_for_test(12 * kMinute);

  // Off (and with the site configured but nothing answered), the chip is the
  // local total -- the fallback, and the whole reason the store runs either way.
  e.set_wakatime_state_for_test(false, true, "");
  e.render_for_test();
  REQUIRE(status_row(e).find(" 12m ") != std::string::npos);
  REQUIRE(status_row(e).find(kTimerGlyph) != std::string::npos);

  // The moment the cli answers, its total is what the chip reports, and the
  // glyph says which of the two sources is speaking.
  e.set_wakatime_state_for_test(true, true, "2 hrs 5 mins");
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(status_row(e).find(" 2 hrs 5 mins ") != std::string::npos);
  REQUIRE(status_row(e).find(kClockGlyph) != std::string::npos);
  REQUIRE(status_row(e).find(" 12m ") == std::string::npos);

  // A cli that can only say "0 secs" has no total to report: its queued
  // heartbeats have not reached the server yet, so the bar keeps the local
  // total and its glyph rather than sitting on a zero while the user codes.
  e.set_wakatime_state_for_test(true, true, "0 secs");
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(status_row(e).find(" 12m ") != std::string::npos);
  REQUIRE(status_row(e).find(kTimerGlyph) != std::string::npos);
  REQUIRE(status_row(e).find("0 secs") == std::string::npos);
  REQUIRE(status_row(e).find(kClockGlyph) == std::string::npos);

  // An empty answer (offline, or an account with nothing logged yet) puts the
  // local total back rather than leaving a blank chip.
  e.set_wakatime_state_for_test(true, true, "");
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(status_row(e).find(" 12m ") != std::string::npos);
  REQUIRE(status_row(e).find(kTimerGlyph) != std::string::npos);
}

TEST_CASE("Coding time: the total survives a restart for the same workspace", "[jot][coding-time]")
{
  const std::string home = seed_config_home();
  const std::string today = local_date(std::time(nullptr));

  {
    Editor e;
    e.set_home_menu_visible(false);
    e.set_workspace_root_for_test("/work/jot");
    e.set_coding_time_ms_for_test(45 * kMinute);
    e.render_frame_for_test(); // opens the window on this key
  } // the editor shuts down here, which is where the total is written

  // On disk under the workspace and the day, which is what makes the number
  // worth showing: it is the day's total, not this process's.
  const std::string written = read_file(store_path());
  REQUIRE(written.find(key_for("/work/jot", today) + "\t2700000") != std::string::npos);

  {
    Editor e;
    e.set_home_menu_visible(false);
    e.set_workspace_root_for_test("/work/jot");
    e.config_set_for_test("status_coding_time", "true");
    e.apply_resize_for_test(120, 30);
    e.render_frame_for_test();
    e.render_for_test();
    REQUIRE(status_row(e).find(" 45m ") != std::string::npos);
  }

  {
    // Another workspace has its own line, and starting there does not show the
    // first one's time -- the total is per workspace, as the store's key says.
    Editor e;
    e.set_home_menu_visible(false);
    e.set_workspace_root_for_test("/work/other");
    e.config_set_for_test("status_coding_time", "true");
    e.apply_resize_for_test(120, 30);
    e.render_frame_for_test();
    e.render_for_test();
    REQUIRE(status_row(e).find(" 45m ") == std::string::npos);
    REQUIRE(status_row(e).find(" 0s ") != std::string::npos);
  }
  REQUIRE(read_file(store_path()).find(today) != std::string::npos);
}
