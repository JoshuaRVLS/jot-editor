// The blink wiring, end to end: does the editor actually blink the caret, and
// does cursor_blink_ms control it?
//
// It did not. The terminal handed the phase to the emulator's blinking DECSCUSR
// shape and the GUI's visibility flag was sticky-true, so the documented
// cursor_blink_ms setting was dead config in both frontends -- the caret never
// blinked at all. These cases drive the real frame loop and watch the caret's
// terminal state, which is what the setting is supposed to change.
#include "editor.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_cursor_blink_io_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void load_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_cursor_blink_io_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    for (int i = 0; i < 200; i++)
    {
      out << "int value_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(120, 40);
    e.scroll_cursor_to_for_test(5, 0);
  }

  // Whether the caret's terminal bytes currently show it. The caret has a cell
  // on screen throughout these cases, so a hide means the blink phase, not the
  // "scrolled out of view" state.
  bool caret_shown(Editor &e)
  {
    return e.ui_for_test()->cursor_sequence().find("?25h") != std::string::npos;
  }

  // Samples the caret across a window comfortably longer than `period_ms`, so
  // whatever the anchor happens to be, both halves of the cycle are visited.
  struct BlinkSample
  {
    bool saw_shown = false;
    bool saw_hidden = false;
  };

  BlinkSample sample_blink(Editor &e, int period_ms, int window_ms)
  {
    BlinkSample s;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(window_ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
      e.render_frame_for_test();
      if (caret_shown(e))
      {
        s.saw_shown = true;
      }
      else
      {
        s.saw_hidden = true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, period_ms / 4)));
    }
    return s;
  }
} // namespace

TEST_CASE("The caret blinks on the configured period", "[jot]")
{
  Editor &e = probe_editor();
  load_file(e);

  // A short period, then a window spanning several cycles: the caret must be
  // seen both on and off. Before the shared clock existed it was always on.
  e.config_set_for_test("cursor_blink_ms", "40");
  const BlinkSample fast = sample_blink(e, 40, 400);
  REQUIRE(fast.saw_shown);
  REQUIRE(fast.saw_hidden);
}

TEST_CASE("cursor_blink_ms = 0 keeps the caret solid", "[jot]")
{
  Editor &e = probe_editor();
  load_file(e);

  // Documented off switch: no blinking, whatever the elapsed time.
  e.config_set_for_test("cursor_blink_ms", "0");
  const BlinkSample solid = sample_blink(e, 40, 300);
  REQUIRE(solid.saw_shown);
  REQUIRE_FALSE(solid.saw_hidden);
}

TEST_CASE("A slower period blinks more slowly", "[jot]")
{
  Editor &e = probe_editor();
  load_file(e);

  // One cycle at 200 ms takes 400 ms, so a 100 ms window cannot contain a full
  // off phase; the same window at 15 ms contains several. This is what makes the
  // setting meaningful rather than decorative.
  e.config_set_for_test("cursor_blink_ms", "200");
  const BlinkSample slow = sample_blink(e, 200, 100);
  REQUIRE_FALSE(slow.saw_hidden);

  e.config_set_for_test("cursor_blink_ms", "15");
  const BlinkSample brisk = sample_blink(e, 15, 200);
  REQUIRE(brisk.saw_hidden);
}
