// GUI grid geometry. See gui_fit.h for why the unit split matters.
#include "gui_fit.h"

#include <algorithm>
#include <cmath>

namespace jot_gui
{
  namespace
  {
    // Above this the display is misreporting (or a compositor is mid-resize
    // with a stale drawable), and trusting it would shrink the grid to a
    // handful of cells.
    constexpr float kMaxScale = 8.0f;
  } // namespace

  float display_scale(int window_w, int window_h, int drawable_w, int drawable_h)
  {
    if (window_w <= 0 || window_h <= 0 || drawable_w <= 0 || drawable_h <= 0)
    {
      return 1.0f;
    }
    // The two axes agree on every platform jot targets; averaging keeps a
    // rounding difference between them from picking the wrong scale.
    const float sx = (float)drawable_w / (float)window_w;
    const float sy = (float)drawable_h / (float)window_h;
    const float scale = (sx + sy) * 0.5f;
    if (!(scale > 1.0f)) // also catches NaN
    {
      return 1.0f;
    }
    return std::min(scale, kMaxScale);
  }

  GridFit fit_grid(int drawable_w, int drawable_h, float cell_w_px, float cell_h_px)
  {
    GridFit fit;
    if (drawable_w <= 0 || drawable_h <= 0 || cell_w_px <= 0.0f || cell_h_px <= 0.0f)
    {
      return fit;
    }
    fit.cols = std::max(1, (int)std::floor((float)drawable_w / cell_w_px));
    fit.rows = std::max(1, (int)std::floor((float)drawable_h / cell_h_px));
    return fit;
  }

  float point_cell_size(float cell_px, float scale)
  {
    if (!(scale > 0.0f))
    {
      return cell_px;
    }
    // A sub-half-point cell would make every downstream division meaningless.
    return std::max(0.5f, cell_px / scale);
  }
} // namespace jot_gui
