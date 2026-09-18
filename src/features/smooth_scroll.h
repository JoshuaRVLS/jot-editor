#ifndef SMOOTH_SCROLL_H
#define SMOOTH_SCROLL_H

#include <cstdint>
#include <string>

// neoscroll.nvim's animation model, ported for jot's frame-driven renderer.
//
// Upstream schedules the k-th of `n` lines at `duration * easing(k / n)`, i.e.
// it spreads the requested distance over the duration with the easing curve,
// and the timer only exists because Vim offers no per-frame callback. jot does
// have one, so the port samples the same curve every frame instead
// (position_fraction below): identical motion profile, no timer drift.
//
// The pieces that are *not* about the clock -- the easing functions, the
// duration and the way a scroll arriving mid-animation merges into the one in
// flight -- are upstream's, spelled the same way, and tested as such.
namespace SmoothScroll
{
  // Upstream's easing functions, in its own spelling.
  enum class Easing
  {
    Linear,
    Quadratic,
    Cubic,
    Quartic,
    Quintic,
    Circular,
    Sine,
  };

  // Parses upstream's option names ("linear", "quadratic", "cubic", "quartic",
  // "quintic", "circular", "sine"), case-insensitively. False for anything else,
  // leaving `out` untouched.
  bool easing_from_name(const std::string &name, Easing &out);

  // The name this easing is configured by.
  const char *easing_name(Easing easing);

  // Fraction of the requested distance covered at `progress` of the animation's
  // duration (clamped to 0..1; 0 exactly at 0 and 1 exactly at 1), monotone.
  //
  // This is the inverse of upstream's time mapping: it solves
  // `progress = easing(k / n)` for `k / n`, so the curve is upstream's
  // ease-out -- most of the distance early, a soft landing -- not ease-in.
  double position_fraction(Easing easing, double progress);

  // The scroll a running animation is committed to, in the units upstream
  // tracks it: scroll.relative_line / scroll.target_line and the sticky
  // continuous_scroll flag.
  struct InFlight
  {
    int target = 0;          // distance the animation will have covered when it settles
    bool continuous = false; // a burst long enough for upstream's lag clamp
  };

  // Upstream's new_scroll() in-flight branch: a scroll that arrives while
  // another is still animating extends it rather than restarting it.
  // `relative` is the distance the running animation has covered so far and
  // `lines` the newly requested distance (both signed, positive downwards).
  // Returns the updated target and mutates `in_flight` (including the sticky
  // continuous flag) exactly like scroll:new_scroll.
  //
  // Upstream records the lag clamp by advancing `relative_line` instead of
  // moving the target; the visible effect of that branch -- travel stops two
  // notches from where the viewport is now -- is what this returns.
  int merge_target(InFlight &in_flight, int relative, int lines);

  // Upstream's duration for the line-scroll mappings (<C-e>/<C-y>), i.e. what
  // one wheel notch costs before the global duration_multiplier scales it.
  // The callers that animate other distances (a page, a centring jump) pass
  // their own base, like upstream's per-mapping durations do.
  inline constexpr int kWheelDurationMs = 100;

  // Duration of one animation: upstream multiplies each mapping's own duration
  // by the global `duration_multiplier`, and its distance-scaled mappings
  // (zt/zz/zb, G, gg) scale it by the distance left relative to a reference.
  // `base_ms` is what a `reference_distance`-line scroll costs, which for the
  // wheel is upstream's <C-e>/<C-y> mapping (100ms).
  int64_t duration_ms(int base_ms, double multiplier, int distance, int reference_distance);
} // namespace SmoothScroll

#endif
