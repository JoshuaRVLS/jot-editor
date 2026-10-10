// Motion model behind the GUI pane slide, split out of gui_anim.cpp so the
// curve can be asserted on numbers instead of pixels.
#ifndef UI_GUI_SCROLL_MATH_H
#define UI_GUI_SCROLL_MATH_H

#include <cmath>

namespace jot_gui
{

  // Where the sliding strip currently sits on screen (pixels from the chain
  // start) and how fast it is moving.
  struct SlideState
  {
    float pos_px = 0.0f;
    float vel_px = 0.0f;
  };

  // Critically damped spring rate in rad/s. A three-row wheel notch settles in
  // about 200 ms: quick enough to keep up with the wheel, soft enough that a
  // notch burst or a direction flip carries the velocity it already had instead
  // of kicking the content around.
  constexpr float kSlideOmega = 26.0f;

  // One exact step of the critically damped spring toward `target_px`. Closed
  // form, so any dt is stable, and retargeting mid-glide keeps the velocity the
  // motion already had (only the target moves).
  inline SlideState slide_step(const SlideState &s, float target_px, float dt, float omega)
  {
    const float u = s.pos_px - target_px;
    const float b = s.vel_px + omega * u;
    const float e = std::exp(-omega * dt);
    SlideState out;
    out.pos_px = target_px + (u + b * dt) * e;
    out.vel_px = (b * (1.0f - omega * dt) - omega * u) * e;
    return out;
  }

  // Settled means stopped on the target: sub-pixel distance and effectively no
  // velocity, so a fast pass through the target keeps flying.
  inline bool slide_settled(const SlideState &s, float target_px)
  {
    return std::abs(s.pos_px - target_px) < 0.25f && std::abs(s.vel_px) < 1.0f;
  }

  inline bool slide_active(const SlideState &s, float target_px)
  {
    return !slide_settled(s, target_px);
  }

  // A retained viewport matters while its rows can still land on screen: the
  // display window sweeps between `s_lo` and `s_hi` rows (already widened by
  // the caller for momentum and reversals), a pane tall at each stop.
  inline bool frame_retained(int top_row, int body_rows, float s_lo, float s_hi)
  {
    return (float)(top_row + body_rows) > s_lo && (float)top_row < s_hi;
  }

  // A viewport is redundant once its neighbours already cover every row it
  // holds, which is exactly when their tops are one pane apart or less.
  inline bool frame_redundant(int prev_top, int next_top, int body_rows)
  {
    return next_top - prev_top <= body_rows;
  }

} // namespace jot_gui

#endif
