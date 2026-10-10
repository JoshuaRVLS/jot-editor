// GUI pane slide motion (src/gui/gui_scroll_math.h).
//
// Pins the curve behind wheel scrolling: a critically damped spring whose
// retargeting keeps the velocity the glide already had. The offset ease it
// replaced kicked by delta/tau on every notch and flipped that kick instantly
// on a reversal, which is what made wheel scrolling read as the content being
// yanked around.
#include "gui/gui_scroll_math.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace jot_gui;

namespace
{
  // Drive the curve at a fixed frame rate, one step per frame.
  std::vector<SlideState> run(const SlideState &from, float target, float dt, int steps)
  {
    std::vector<SlideState> out;
    SlideState s = from;
    for (int i = 0; i < steps; i++)
    {
      s = slide_step(s, target, dt, kSlideOmega);
      out.push_back(s);
    }
    return out;
  }
} // namespace

TEST_CASE("A glide from rest reaches its target without overshooting it", "[jot]")
{
  const auto states = run({0.0f, 0.0f}, 100.0f, 1.0f / 60.0f, 60);
  float prev = 0.0f;
  for (const auto &s : states)
  {
    // Monotonic and strictly below the target: critically damped from rest.
    REQUIRE(s.pos_px >= prev);
    REQUIRE(s.pos_px <= 100.0f);
    prev = s.pos_px;
  }
  // The last state is on the target: one second at omega 26 is far past the
  // ~200 ms a notch takes.
  REQUIRE(slide_settled(states.back(), 100.0f));
}

TEST_CASE("The glide lands the same whatever the frame rate", "[jot]")
{
  // The step is closed form for a fixed target, so one long frame and many
  // short ones must agree; the animation cannot run faster at higher Hz.
  const SlideState one = slide_step({0.0f, 0.0f}, 75.0f, 0.1f, kSlideOmega);
  SlideState many{0.0f, 0.0f};
  for (int i = 0; i < 10; i++)
  {
    many = slide_step(many, 75.0f, 0.01f, kSlideOmega);
  }
  REQUIRE(std::abs(one.pos_px - many.pos_px) < 0.01f);
  REQUIRE(std::abs(one.vel_px - many.vel_px) < 0.5f);
}

TEST_CASE("Retargeting mid-glide keeps the velocity it already had", "[jot]")
{
  // Mid-flight, the wheel adds more distance. The exponential offset ease
  // this replaced jumped the velocity by delta/tau on the spot; the spring
  // only bends it (acceleration), so a sub-millisecond retarget is nearly
  // free of velocity change.
  SlideState s{0.0f, 0.0f};
  s = slide_step(s, 100.0f, 0.05f, kSlideOmega);
  const float v_before = s.vel_px;
  s = slide_step(s, 130.0f, 0.0001f, kSlideOmega);
  REQUIRE(std::abs(s.vel_px - v_before) < 5.0f);
}

TEST_CASE("A reversal decelerates through zero instead of snapping back", "[jot]")
{
  // Flying up at 800 px/s when the target flips far below: a millisecond in,
  // the content is still moving the way it was going, only slower. The offset
  // ease this replaced reversed its velocity on the spot, which is the yank.
  SlideState s{0.0f, 800.0f};
  s = slide_step(s, -200.0f, 0.001f, kSlideOmega);
  REQUIRE(s.pos_px > 0.0f); // still gliding the way it was going
  REQUIRE(s.vel_px > 0.0f); // and slowing, not reversed in one frame
  for (int i = 0; i < 200 && !slide_settled(s, -200.0f); i++)
  {
    s = slide_step(s, -200.0f, 1.0f / 60.0f, kSlideOmega);
  }
  REQUIRE(slide_settled(s, -200.0f));
}

TEST_CASE("Settled means stopped on the target, not merely passing it", "[jot]")
{
  REQUIRE(slide_settled({100.0f + 0.1f, 0.5f}, 100.0f));
  REQUIRE_FALSE(slide_settled({100.0f, 5.0f}, 100.0f)); // through it at speed
  REQUIRE_FALSE(slide_settled({100.0f + 1.0f, 0.0f}, 100.0f));
  REQUIRE(slide_active({100.0f + 1.0f, 0.0f}, 100.0f));
  REQUIRE_FALSE(slide_active({0.0f, 0.0f}, 0.0f)); // idle pane keeps no repaint
}

TEST_CASE("Retention keeps the sweep covered and drops what cannot draw", "[jot]")
{
  // The window sweeps rows [lo, hi]; a frame matters only where it overlaps.
  REQUIRE(frame_retained(0, 20, -5.0f, 25.0f));
  REQUIRE(frame_retained(10, 20, -5.0f, 25.0f));
  REQUIRE_FALSE(frame_retained(30, 20, -5.0f, 25.0f)); // past the sweep
  REQUIRE_FALSE(frame_retained(-30, 20, -5.0f, 25.0f)); // fully behind it

  // Neighbours one pane apart or less already cover the rows between them.
  REQUIRE(frame_redundant(0, 70, 70));
  REQUIRE(frame_redundant(0, 71, 72));
  REQUIRE_FALSE(frame_redundant(0, 71, 70));
}

TEST_CASE("Sparse dropping keeps pane coverage however long the scroll", "[jot]")
{
  // The capture policy in miniature: a viewport per three rows visited, then
  // the same neighbour rule capture_pane_rows drops with. The kept set must
  // stay sparse while every gap stays within the pane (no uncovered row).
  const int body = 40;
  std::vector<int> tops;
  for (int t = 0; t <= 600; t += 3)
  {
    tops.push_back(t);
  }
  for (size_t i = 1; i + 1 < tops.size();)
  {
    if (frame_redundant(tops[i - 1], tops[i + 1], body))
    {
      tops.erase(tops.begin() + (long)i);
    }
    else
    {
      i++;
    }
  }
  REQUIRE(tops.size() <= 18); // 600 rows / pane, not one per visited viewport
  for (size_t i = 1; i < tops.size(); i++)
  {
    REQUIRE(tops[i] - tops[i - 1] <= body);
  }
  REQUIRE(tops.front() == 0);
  REQUIRE(tops.back() == 600);
}
