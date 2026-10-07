#ifndef SMOOTH_SCROLL_H
#define SMOOTH_SCROLL_H

#include <cstdint>
#include <string>

// neoscroll.nvim's animation model: line k of n lands at duration * easing(k /
// n), and jot samples the same curve every frame instead of on a timer. The
// easing functions and the mid-animation merge are upstream's, spelled alike.
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

  // Upstream's in-flight branch: a scroll arriving mid-animation extends the
  // running one instead of restarting it. Both distances are signed, positive
  // downwards, and the lag clamp shows up as travel stopping two notches out.
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
