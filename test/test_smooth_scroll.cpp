// Smooth scrolling: features/smooth_scroll.h is neoscroll.nvim's animation
// model, and jot/app/smooth_scroll.cpp drives it off the frame clock.
//
// The model half is upstream's, so it is asserted against upstream's own
// numbers: neoscroll schedules the k-th of n lines at
// `duration * (1 - (1 - k/n)^(1/2))` for quadratic (scroll.lua), which puts the
// animation's half-way point at k/n = 0.75 -- the position curve is that
// mapping's inverse, and ease-out rather than ease-in.
//
// The editor half is asserted on explicit timestamps (the advance hook takes
// the clock as an argument), so the curve is checked without waiting on a real
// frame timer.
#include "editor.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_smooth_scroll_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void load_long_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_smooth_scroll_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    for (int i = 0; i < 400; i++)
    {
      out << "int value_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
  }

  void load_short_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_smooth_scroll_short_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << "int a = 1;\nint b = 2;\n";
    out.close();
    e.load_file(path);
  }

  void make_wide(Editor &e)
  {
    e.apply_resize_for_test(120, 40);
  }

  // Smooth scrolling is off by default, so a case that wants the animation asks
  // for it explicitly instead of relying on the shipped default (which the
  // pty probe covers end to end, with an empty settings.conf).
  void set_smooth(Editor &e, bool enabled)
  {
    e.config_set_for_test("smooth_scroll", enabled ? "true" : "false");
    e.apply_config_live_for_test();
  }

  // The easing setting reaches the animation only through the live config
  // apply, so every case that changes it has to put it back.
  void set_easing(Editor &e, const std::string &name)
  {
    e.config_set_for_test("smooth_scroll_easing", name);
    e.apply_config_live_for_test();
  }
} // namespace

TEST_CASE("Easing names are upstream's, and unknown names are rejected", "[jot]")
{
  SmoothScroll::Easing easing = SmoothScroll::Easing::Sine;
  REQUIRE(SmoothScroll::easing_from_name("linear", easing));
  REQUIRE(easing == SmoothScroll::Easing::Linear);
  REQUIRE(SmoothScroll::easing_from_name("LINEAR", easing));
  REQUIRE(easing == SmoothScroll::Easing::Linear);
  REQUIRE(SmoothScroll::easing_from_name("Quadratic", easing));
  REQUIRE(easing == SmoothScroll::Easing::Quadratic);
  REQUIRE(SmoothScroll::easing_from_name("cubic", easing));
  REQUIRE(easing == SmoothScroll::Easing::Cubic);
  REQUIRE(SmoothScroll::easing_from_name("quartic", easing));
  REQUIRE(easing == SmoothScroll::Easing::Quartic);
  REQUIRE(SmoothScroll::easing_from_name("quintic", easing));
  REQUIRE(easing == SmoothScroll::Easing::Quintic);
  REQUIRE(SmoothScroll::easing_from_name("circular", easing));
  REQUIRE(easing == SmoothScroll::Easing::Circular);
  REQUIRE(SmoothScroll::easing_from_name("sine", easing));
  REQUIRE(easing == SmoothScroll::Easing::Sine);

  // Upstream's spellings round-trip, so a message can name the active curve.
  for (SmoothScroll::Easing e :
       {SmoothScroll::Easing::Linear,
        SmoothScroll::Easing::Quadratic,
        SmoothScroll::Easing::Cubic,
        SmoothScroll::Easing::Quartic,
        SmoothScroll::Easing::Quintic,
        SmoothScroll::Easing::Circular,
        SmoothScroll::Easing::Sine})
  {
    SmoothScroll::Easing parsed = SmoothScroll::Easing::Linear;
    REQUIRE(SmoothScroll::easing_from_name(SmoothScroll::easing_name(e), parsed));
    REQUIRE(parsed == e);
  }

  // An unknown name must not half-apply: the caller keeps its default.
  REQUIRE_FALSE(SmoothScroll::easing_from_name("bounce", easing));
  REQUIRE_FALSE(SmoothScroll::easing_from_name("", easing));
  REQUIRE(easing == SmoothScroll::Easing::Sine);
}

TEST_CASE("The position curve is upstream's easing inverted", "[jot]")
{
  const auto close = [](double a, double b) { return std::fabs(a - b) < 1e-9; };

  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Linear, 0.25), 0.25));
  // Half of the duration covers three quarters of the distance for quadratic:
  // upstream's k-th line of n lands at 1-(1-k/n)^(1/2), which is 0.5 at k/n=0.75.
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Quadratic, 0.5), 0.75));
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Cubic, 0.5), 0.875));
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Quartic, 0.5), 0.9375));
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Quintic, 0.5), 0.96875));
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Circular, 0.5),
                std::sqrt(0.75)));
  REQUIRE(close(SmoothScroll::position_fraction(SmoothScroll::Easing::Sine, 0.5),
                std::sin(3.14159265358979323846 / 4.0)));

  for (SmoothScroll::Easing e :
       {SmoothScroll::Easing::Linear,
        SmoothScroll::Easing::Quadratic,
        SmoothScroll::Easing::Cubic,
        SmoothScroll::Easing::Quartic,
        SmoothScroll::Easing::Quintic,
        SmoothScroll::Easing::Circular,
        SmoothScroll::Easing::Sine})
  {
    // The endpoints are exact: a frame that lands on the end of the animation
    // has to place the viewport on the target, never a line short of it.
    REQUIRE(SmoothScroll::position_fraction(e, 0.0) == 0.0);
    REQUIRE(SmoothScroll::position_fraction(e, 1.0) == 1.0);
    // Out-of-order timestamps (a slow frame, a clock that ran backwards) clamp
    // instead of extrapolating.
    REQUIRE(SmoothScroll::position_fraction(e, -0.5) == 0.0);
    REQUIRE(SmoothScroll::position_fraction(e, 2.0) == 1.0);

    double previous = -1.0;
    for (int step = 0; step <= 100; step++)
    {
      const double fraction = SmoothScroll::position_fraction(e, step / 100.0);
      REQUIRE(fraction >= 0.0);
      REQUIRE(fraction <= 1.0);
      REQUIRE(fraction >= previous);
      previous = fraction;
    }

    // Ease-out: all of these cover at least half the distance by mid-animation.
    REQUIRE(SmoothScroll::position_fraction(e, 0.5) >= 0.5);
  }
}

TEST_CASE("An in-flight scroll is extended, capped and reversed like upstream", "[jot]")
{
  // A scroll that starts an animation: the whole request becomes the target.
  SmoothScroll::InFlight anim;
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/0, /*lines=*/3) == 3);
  REQUIRE_FALSE(anim.continuous);

  // A second notch mid-flight extends the target rather than restarting it.
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/1, /*lines=*/3) == 6);

  // Reversing while a long scroll is still pending decelerates instead of
  // restarting: the target lands one request short of where the viewport is.
  anim = SmoothScroll::InFlight{};
  anim.target = 20;
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/2, /*lines=*/-3) == 5);

  // Reversing a short hop just takes the distance off the target.
  anim = SmoothScroll::InFlight{};
  anim.target = 3;
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/2, /*lines=*/-3) == 0);

  // Held wheel: past five requests of backlog the clamp engages and the
  // animation stops chasing, so the viewport can never be more than two
  // requests behind the wheel.
  anim = SmoothScroll::InFlight{};
  anim.target = 40;
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/0, /*lines=*/3) == 6);
  REQUIRE(anim.continuous);

  // ... and the clamp sticks for the rest of the burst.
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/1, /*lines=*/3) == 7);
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/2, /*lines=*/3) == 8);

  // A request with no distance leaves the running animation alone.
  anim = SmoothScroll::InFlight{};
  anim.target = 9;
  REQUIRE(SmoothScroll::merge_target(anim, /*relative=*/4, /*lines=*/0) == 9);
}

TEST_CASE("A duration scales with the distance left and with the multiplier", "[jot]")
{
  constexpr double kOne = 1.0;
  // One notch of three lines at the wheel's base duration.
  REQUIRE(SmoothScroll::duration_ms(100, kOne, 3, 3) == 100);
  // Twice the backlog takes twice as long (upstream's distance-scaled mappings).
  REQUIRE(SmoothScroll::duration_ms(100, kOne, 6, 3) == 200);
  REQUIRE(SmoothScroll::duration_ms(100, kOne, 1, 3) == 33);
  REQUIRE(SmoothScroll::duration_ms(100, kOne, -6, 3) == 200);
  // duration_multiplier, exactly as upstream scales it.
  REQUIRE(SmoothScroll::duration_ms(100, 0.5, 3, 3) == 50);
  REQUIRE(SmoothScroll::duration_ms(100, 2.0, 3, 3) == 200);
  // A missing/zero/negative multiplier is upstream's 1.0, not a frozen scroll.
  REQUIRE(SmoothScroll::duration_ms(100, 0.0, 3, 3) == 100);
  REQUIRE(SmoothScroll::duration_ms(100, -2.0, 3, 3) == 100);
  // Degenerate inputs still yield a duration a frame timer can hit.
  REQUIRE(SmoothScroll::duration_ms(100, kOne, 0, 0) == 100);
  REQUIRE(SmoothScroll::duration_ms(0, kOne, 3, 3) == 1);
}

TEST_CASE("Smooth scrolling is off until a config turns it on", "[jot]")
{
  // Seed the scratch config home (the shared probe), then read the shipped
  // default from a fresh editor: the shared one has had settings written into
  // it by the cases above, and this is about what a user gets out of the box.
  (void)probe_editor();
  Editor e;
  REQUIRE_FALSE(e.smooth_scroll_enabled_for_test());

  load_long_file(e);
  make_wide(e);
  e.buffer_for_test().scroll_offset = 10;
  // The wheel's entry point lands the notch in the one call, as it did before
  // the animation existed.
  REQUIRE(e.scroll_view_smooth_for_test(/*lines=*/3, /*base_ms=*/100));
  REQUIRE(e.buffer_for_test().scroll_offset == 13);
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());

  // ... and the animation is one setting away.
  set_smooth(e, true);
  e.buffer_for_test().scroll_offset = 10;
  REQUIRE(e.scroll_view_smooth_for_test(3, 100));
  REQUIRE(e.buffer_for_test().scroll_offset == 10);
  REQUIRE(e.smooth_scroll_active_for_test());
}

TEST_CASE("A wheel notch eases the viewport over the frame clock", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  make_wide(e);
  set_smooth(e, true);
  e.buffer_for_test().scroll_offset = 10;

  REQUIRE(e.scroll_view_smooth_for_test(/*lines=*/3, /*base_ms=*/100));
  REQUIRE(e.smooth_scroll_active_for_test());
  REQUIRE(e.smooth_scroll_target_for_test() == 13);
  REQUIRE(e.smooth_scroll_duration_for_test() == 100);

  const long long start = e.smooth_scroll_start_ms_for_test();

  // The first frame of the animation is where it started: no jump.
  REQUIRE_FALSE(e.advance_smooth_scroll_for_test(start));
  REQUIRE(e.buffer_for_test().scroll_offset == 10);
  REQUIRE(e.smooth_scroll_active_for_test());

  // Linear easing: half the duration covers half the three-line distance.
  REQUIRE(e.advance_smooth_scroll_for_test(start + 50));
  REQUIRE(e.buffer_for_test().scroll_offset == 12);
  REQUIRE(e.smooth_scroll_active_for_test());

  // Settles exactly on the target and stops owing frames.
  REQUIRE(e.advance_smooth_scroll_for_test(start + 100));
  REQUIRE(e.buffer_for_test().scroll_offset == 13);
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());
  REQUIRE_FALSE(e.advance_smooth_scroll_for_test(start + 130));
}

TEST_CASE("A configured easing changes the curve, and off means no animation", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  make_wide(e);

  set_smooth(e, true);
  set_easing(e, "quadratic");
  REQUIRE(std::string(e.smooth_scroll_easing_for_test()) == "quadratic");
  e.buffer_for_test().scroll_offset = 0;
  REQUIRE(e.scroll_view_smooth_for_test(/*lines=*/10, /*base_ms=*/100));
  const long long start = e.smooth_scroll_start_ms_for_test();
  // Quadratic covers 75% of the distance at half the duration, so an eased
  // animation is ahead of the linear one at the same timestamp.
  REQUIRE(e.advance_smooth_scroll_for_test(start + 50));
  REQUIRE(e.buffer_for_test().scroll_offset == 8);
  REQUIRE(e.advance_smooth_scroll_for_test(start + 100));
  REQUIRE(e.buffer_for_test().scroll_offset == 10);

  // An unknown easing name falls back to linear instead of disabling anything.
  set_easing(e, "bounce");
  REQUIRE(std::string(e.smooth_scroll_easing_for_test()) == "linear");

  // Switched off: the same request jumps to the destination in one call.
  set_easing(e, "linear");
  set_smooth(e, false);
  REQUIRE_FALSE(e.smooth_scroll_enabled_for_test());
  e.buffer_for_test().scroll_offset = 10;
  REQUIRE(e.scroll_view_smooth_for_test(/*lines=*/3, /*base_ms=*/100));
  REQUIRE(e.buffer_for_test().scroll_offset == 13);
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());

  // Turned off mid-animation: the animation is dropped where it stands.
  set_smooth(e, true);
  e.buffer_for_test().scroll_offset = 0;
  REQUIRE(e.scroll_view_smooth_for_test(3, 100));
  REQUIRE(e.smooth_scroll_active_for_test());
  set_smooth(e, false);
  REQUIRE_FALSE(e.advance_smooth_scroll_for_test(e.smooth_scroll_start_ms_for_test() + 50));
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());
}

TEST_CASE("A viewport moved by something else drops the animation", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  make_wide(e);
  set_smooth(e, true);

  e.buffer_for_test().scroll_offset = 0;
  REQUIRE(e.scroll_view_smooth_for_test(/*lines=*/3, /*base_ms=*/100));
  const long long start = e.smooth_scroll_start_ms_for_test();

  // A jump (search result, jumplist, fold, the caret pulled back into view)
  // writes the offset behind the animation's back.
  e.buffer_for_test().scroll_offset = 50;
  REQUIRE_FALSE(e.advance_smooth_scroll_for_test(start + 50));
  REQUIRE(e.buffer_for_test().scroll_offset == 50);
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());

  // The wheel then picks up from where the viewport actually is.
  REQUIRE(e.scroll_view_smooth_for_test(3, 100));
  REQUIRE(e.smooth_scroll_target_for_test() == 53);
}

TEST_CASE("A wheel at the end of the file neither moves nor animates", "[jot]")
{
  Editor &e = probe_editor();
  // A file shorter than the viewport is already at both of its edges.
  load_short_file(e);
  make_wide(e);
  set_smooth(e, true);
  e.buffer_for_test().scroll_offset = 0;

  REQUIRE_FALSE(e.scroll_view_smooth_for_test(/*lines=*/3, /*base_ms=*/100));
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());
  REQUIRE(e.buffer_for_test().scroll_offset == 0);

  REQUIRE_FALSE(e.scroll_view_smooth_for_test(/*lines=*/-3, /*base_ms=*/100));
  REQUIRE_FALSE(e.smooth_scroll_active_for_test());
  REQUIRE(e.buffer_for_test().scroll_offset == 0);
}
