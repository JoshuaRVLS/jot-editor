#include "smooth_scroll.h"

#include <algorithm>
#include <cmath>

namespace SmoothScroll
{
  namespace
  {
    // Upstream's easing table (neoscroll/scroll.lua):
    //   quadratic = 1 - (1 - x)^(1/2)   ... quintic = 1 - (1 - x)^(1/5)
    //   circular  = 1 - (1 - x^2)^(1/2)
    //   sine      = 2 * asin(x) / pi
    // Those map a *distance* fraction onto a *time* fraction, so the position
    // curve is their inverse: 1 - (1 - t)^n, sqrt(2t - t^2), sin(pi t / 2).
    double inverse_easing(Easing easing, double t)
    {
      switch (easing)
      {
      case Easing::Quadratic:
        return 1.0 - (1.0 - t) * (1.0 - t);
      case Easing::Cubic:
        return 1.0 - std::pow(1.0 - t, 3.0);
      case Easing::Quartic:
        return 1.0 - std::pow(1.0 - t, 4.0);
      case Easing::Quintic:
        return 1.0 - std::pow(1.0 - t, 5.0);
      case Easing::Circular:
        return std::sqrt(std::max(0.0, 2.0 * t - t * t));
      case Easing::Sine:
        return std::sin(3.14159265358979323846 * t / 2.0);
      case Easing::Linear:
      default:
        return t;
      }
    }
  } // namespace

  bool easing_from_name(const std::string &name, Easing &out)
  {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
    {
      lower.push_back((char)std::tolower((unsigned char)c));
    }
    if (lower == "linear")
    {
      out = Easing::Linear;
    }
    else if (lower == "quadratic")
    {
      out = Easing::Quadratic;
    }
    else if (lower == "cubic")
    {
      out = Easing::Cubic;
    }
    else if (lower == "quartic")
    {
      out = Easing::Quartic;
    }
    else if (lower == "quintic")
    {
      out = Easing::Quintic;
    }
    else if (lower == "circular")
    {
      out = Easing::Circular;
    }
    else if (lower == "sine")
    {
      out = Easing::Sine;
    }
    else
    {
      return false;
    }
    return true;
  }

  const char *easing_name(Easing easing)
  {
    switch (easing)
    {
    case Easing::Quadratic:
      return "quadratic";
    case Easing::Cubic:
      return "cubic";
    case Easing::Quartic:
      return "quartic";
    case Easing::Quintic:
      return "quintic";
    case Easing::Circular:
      return "circular";
    case Easing::Sine:
      return "sine";
    case Easing::Linear:
    default:
      return "linear";
    }
  }

  double position_fraction(Easing easing, double progress)
  {
    // The endpoints are pinned: a frame that lands exactly on the end of the
    // animation must place the viewport on the target, not next to it.
    if (!(progress > 0.0))
    {
      return 0.0;
    }
    if (progress >= 1.0)
    {
      return 1.0;
    }
    return std::clamp(inverse_easing(easing, progress), 0.0, 1.0);
  }

  int merge_target(InFlight &in_flight, int relative, int lines)
  {
    if (lines == 0)
    {
      return in_flight.target;
    }
    const int pending = in_flight.target - relative;
    const bool opposite_direction = pending * lines < 0;
    const bool long_scroll = std::abs(pending) - std::abs(lines) > 0;
    if (opposite_direction && long_scroll)
    {
      // Reversing: the running animation decelerates to a stop one request
      // short of where it is, instead of restarting in the new direction.
      in_flight.target = relative - lines;
    }
    else if (in_flight.continuous)
    {
      in_flight.target = relative + 2 * lines;
    }
    else if (std::abs(pending) > std::abs(5 * lines))
    {
      // Far enough behind that upstream stops chasing: it declares the scroll
      // continuous and drops the backlog, which caps the lag at two requests
      // so a held wheel cannot run away from the viewport.
      in_flight.continuous = true;
      in_flight.target = relative + 2 * lines;
    }
    else
    {
      in_flight.target += lines;
    }
    return in_flight.target;
  }

  int64_t duration_ms(int base_ms, double multiplier, int distance, int reference_distance)
  {
    if (!std::isfinite(multiplier) || multiplier <= 0.0)
    {
      multiplier = 1.0;
    }
    const int reference = std::max(1, std::abs(reference_distance));
    const int span = std::max(1, std::abs(distance));
    const double ms =
        (double)std::max(0, base_ms) * multiplier * (double)span / (double)reference;
    if (!(ms >= 1.0))
    {
      // Never zero: a sub-millisecond duration would depend on the frame timer
      // happening to land after the deadline.
      return 1;
    }
    return (int64_t)std::llround(std::min(ms, 1.0e9));
  }
} // namespace SmoothScroll
