#include "features/color_space.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
  double clamp01(double v)
  {
    return std::clamp(v, 0.0, 1.0);
  }

  double clamp255(double v)
  {
    return std::clamp(v, 0.0, 255.0);
  }

  // --- Shared matrices -------------------------------------------------------
  // XYZ (D65) <-> linear sRGB, from the CSS Color 4 conversion matrices.
  constexpr double kXyzToRgb[3][3] = {
      {3.2404541621, -1.5371385940, -0.4985314096},
      {-0.9692660305, 1.8760108454, 0.0415560175},
      {0.0556434310, -0.2040259135, 1.0572251882},
  };

  // Bradford chromatic adaptation between D50 (Lab/LCH) and D65 (sRGB).
  constexpr double kD50ToD65[3][3] = {
      {0.9555766, -0.0230393, 0.0631636},
      {-0.0282895, 1.0099416, 0.0210077},
      {0.0122982, -0.0204830, 1.3299098},
  };

  // Linear sRGB -> 0..255 through the sRGB transfer function, clamped to gamut.
  void linear_rgb_to_srgb(double r_lin, double g_lin, double b_lin, double &r, double &g, double &b)
  {
    r = clamp255(jot_color::linear_to_srgb(clamp01(r_lin)) * 255.0);
    g = clamp255(jot_color::linear_to_srgb(clamp01(g_lin)) * 255.0);
    b = clamp255(jot_color::linear_to_srgb(clamp01(b_lin)) * 255.0);
  }

  // XYZ (D65) -> sRGB, the tail every space conversion shares.
  std::uint32_t xyz_d65_to_rgb(double x, double y, double z)
  {
    const double r_lin = kXyzToRgb[0][0] * x + kXyzToRgb[0][1] * y + kXyzToRgb[0][2] * z;
    const double g_lin = kXyzToRgb[1][0] * x + kXyzToRgb[1][1] * y + kXyzToRgb[1][2] * z;
    const double b_lin = kXyzToRgb[2][0] * x + kXyzToRgb[2][1] * y + kXyzToRgb[2][2] * z;
    double r = 0.0, g = 0.0, b = 0.0;
    linear_rgb_to_srgb(r_lin, g_lin, b_lin, r, g, b);
    return jot_color::pack_rgb_f(r, g, b);
  }

  // --- HSLuv -----------------------------------------------------------------
  // Ported from hsluv-lua (MIT), the reference implementation of http://hsluv.org.
  // HSLuv is a human-friendly form of CIE LUV: same L, but saturation is expressed
  // relative to the most saturated colour at that lightness and hue, so S=100 is
  // always on the gamut boundary.
  constexpr double kRefU = 0.19783000664283;
  constexpr double kRefV = 0.46831999493879;
  constexpr double kKappa = 903.2962962;
  constexpr double kEpsilon = 0.0088564516;

  struct Bounds
  {
    double slope;
    double intercept;
  };

  // The six gamut-boundary lines at lightness `l`, in linear RGB terms.
  void luv_bounds(double l, Bounds out[6])
  {
    double sub1 = std::pow(l + 16.0, 3) / 1560896.0;
    double sub2 = sub1 > kEpsilon ? sub1 : l / kKappa;
    int n = 0;
    for (int i = 0; i < 3; i++)
    {
      const double m1 = kXyzToRgb[i][0];
      const double m2 = kXyzToRgb[i][1];
      const double m3 = kXyzToRgb[i][2];
      for (int t = 0; t < 2; t++)
      {
        const double top1 = (284517.0 * m1 - 94839.0 * m3) * sub2;
        const double top2 =
            (838422.0 * m3 + 769860.0 * m2 + 731718.0 * m1) * l * sub2 - 769860.0 * t * l;
        const double bottom = (632260.0 * m3 - 126452.0 * m2) * sub2 + 126452.0 * t;
        out[n].slope = top1 / bottom;
        out[n].intercept = top2 / bottom;
        n++;
      }
    }
  }

  // Longest chroma that stays inside the gamut for this lightness and hue.
  double max_safe_chroma(double l, double h_deg)
  {
    const double hrad = h_deg / 360.0 * 2.0 * M_PI;
    Bounds bounds[6];
    luv_bounds(l, bounds);
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < 6; i++)
    {
      const double denom = std::sin(hrad) - bounds[i].slope * std::cos(hrad);
      if (std::abs(denom) < 1e-12)
      {
        continue;
      }
      const double length = bounds[i].intercept / denom;
      if (length >= 0)
      {
        best = std::min(best, length);
      }
    }
    return best == std::numeric_limits<double>::max() ? 0.0 : best;
  }

  double luv_y_for_l(double l)
  {
    if (l <= 8.0)
    {
      return l / kKappa;
    }
    return std::pow((l + 16.0) / 116.0, 3);
  }

} // namespace

namespace jot_color
{
  std::uint32_t pack_rgb(int r, int g, int b)
  {
    return ((std::uint32_t)(r & 0xFF) << 16) | ((std::uint32_t)(g & 0xFF) << 8)
           | (std::uint32_t)(b & 0xFF);
  }

  std::uint32_t pack_rgb_f(double r, double g, double b)
  {
    return pack_rgb((int)std::lround(clamp255(r)),
                    (int)std::lround(clamp255(g)),
                    (int)std::lround(clamp255(b)));
  }

  double srgb_to_linear(double c)
  {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
  }

  double linear_to_srgb(double c)
  {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
  }

  std::uint32_t hwb_to_rgb(double h_deg, double w, double b)
  {
    const double sum = w + b;
    if (sum >= 1.0)
    {
      // Fully desaturated: the ratio of whiteness to blackness picks the grey.
      const double grey = std::floor(w / sum * 255.0);
      return pack_rgb_f(grey, grey, grey);
    }
    // The pure hue colour is HSL(h, 100%, 50%); whiteness lifts it toward white
    // and blackness lowers it toward black.
    const std::uint32_t base = hsl_to_rgb(h_deg, 1.0, 0.5);
    const double scale = 1.0 - w - b;
    const double r = (((base >> 16) & 0xFF) / 255.0 * scale + w) * 255.0;
    const double g = (((base >> 8) & 0xFF) / 255.0 * scale + w) * 255.0;
    const double bl = ((base & 0xFF) / 255.0 * scale + w) * 255.0;
    return pack_rgb_f(r, g, bl);
  }

  std::uint32_t hsl_to_rgb(double h_deg, double s, double l)
  {
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    if (s == 0.0)
    {
      const double grey = l * 255.0;
      return pack_rgb_f(grey, grey, grey);
    }
    const double c = (1.0 - std::abs(2.0 * l - 1.0)) * s;
    const double hue_turns = (h_deg / 360.0) - std::floor(h_deg / 360.0);
    const double hp = hue_turns * 6.0;
    const double x = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
    double r = 0.0, g = 0.0, b = 0.0;
    if (hp < 1.0)
    {
      r = c;
      g = x;
    }
    else if (hp < 2.0)
    {
      r = x;
      g = c;
    }
    else if (hp < 3.0)
    {
      g = c;
      b = x;
    }
    else if (hp < 4.0)
    {
      g = x;
      b = c;
    }
    else if (hp < 5.0)
    {
      r = x;
      b = c;
    }
    else
    {
      r = c;
      b = x;
    }
    const double m = l - c * 0.5;
    return pack_rgb_f((r + m) * 255.0, (g + m) * 255.0, (b + m) * 255.0);
  }

  std::uint32_t lab_to_rgb(double l, double a, double b)
  {
    // Lab -> D50 XYZ, per the spec's piecewise function.
    const double fy = (l + 16.0) / 116.0;
    const double fx = a / 500.0 + fy;
    const double fz = fy - b / 200.0;
    const double delta = 6.0 / 29.0;
    const double delta_sq = delta * delta;
    const double delta_cb = delta_sq * delta;
    const double xr = fx > delta ? fx * fx * fx : 3.0 * delta_sq * (fx - 4.0 / 29.0);
    const double yr = fy > delta ? fy * fy * fy : 3.0 * delta_sq * (fy - 4.0 / 29.0);
    const double zr = fz > delta ? fz * fz * fz : 3.0 * delta_sq * (fz - 4.0 / 29.0);

    // Scale by the D50 white point, then adapt D50 -> D65.
    const double x50 = xr * 0.3457 / 0.3585;
    const double y50 = yr;
    const double z50 = zr * (1.0 - 0.3457 - 0.3585) / 0.3585;

    const double x = kD50ToD65[0][0] * x50 + kD50ToD65[0][1] * y50 + kD50ToD65[0][2] * z50;
    const double y = kD50ToD65[1][0] * x50 + kD50ToD65[1][1] * y50 + kD50ToD65[1][2] * z50;
    const double z = kD50ToD65[2][0] * x50 + kD50ToD65[2][1] * y50 + kD50ToD65[2][2] * z50;
    return xyz_d65_to_rgb(x, y, z);
  }

  std::uint32_t lch_to_rgb(double l, double c, double h_deg)
  {
    const double h_rad = h_deg * M_PI / 180.0;
    return lab_to_rgb(l, c * std::cos(h_rad), c * std::sin(h_rad));
  }

  std::uint32_t oklch_to_rgb(double l, double c, double h_deg)
  {
    // OKLCH -> OKLab: cylindrical to cartesian.
    const double h_rad = h_deg * M_PI / 180.0;
    const double a = c * std::cos(h_rad);
    const double b = c * std::sin(h_rad);

    // OKLab -> LMS': the inverse of the cone-response matrix.
    const double l_ = l + 0.3963377774 * a + 0.2158037573 * b;
    const double m_ = l - 0.1055613458 * a - 0.0638541728 * b;
    const double s_ = l - 0.0894841775 * a - 1.2914855480 * b;

    // Undo the cube root, then LMS -> linear sRGB.
    const double lp = l_ * l_ * l_;
    const double mp = m_ * m_ * m_;
    const double sp = s_ * s_ * s_;

    const double r_lin = 4.0767416621 * lp - 3.3077115913 * mp + 0.2309699292 * sp;
    const double g_lin = -1.2684380046 * lp + 2.6097574011 * mp - 0.3413193965 * sp;
    const double b_lin = -0.0041960863 * lp - 0.7034186147 * mp + 1.7076147010 * sp;
    double r = 0.0, g = 0.0, bb = 0.0;
    linear_rgb_to_srgb(r_lin, g_lin, b_lin, r, g, bb);
    return pack_rgb_f(r, g, bb);
  }

  std::uint32_t css_color_to_rgb(ColorFnSpace space, double r, double g, double b)
  {
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);

    switch (space)
    {
    case ColorFnSpace::Srgb:
      // Already sRGB: only the scale to 0..255 is needed.
      return pack_rgb_f(r * 255.0, g * 255.0, b * 255.0);

    case ColorFnSpace::SrgbLinear:
      return pack_rgb_f(
          linear_to_srgb(r) * 255.0, linear_to_srgb(g) * 255.0, linear_to_srgb(b) * 255.0);

    case ColorFnSpace::DisplayP3:
    case ColorFnSpace::A98Rgb:
    case ColorFnSpace::ProPhotoRgb:
    case ColorFnSpace::Rec2020:
    {
      // Every wide-gamut space follows the same shape: undo its transfer
      // function, go to XYZ, adapt to D65 where needed, then into sRGB (which
      // clamps, so out-of-gamut colours land on the closest sRGB colour).
      double rl = 0.0, gl = 0.0, bl = 0.0;
      double x = 0.0, y = 0.0, z = 0.0;

      if (space == ColorFnSpace::DisplayP3)
      {
        // Display P3 shares sRGB's transfer function.
        rl = srgb_to_linear(r);
        gl = srgb_to_linear(g);
        bl = srgb_to_linear(b);
        x = 0.4865709486 * rl + 0.2656676932 * gl + 0.1982172852 * bl;
        y = 0.2289745641 * rl + 0.6917385218 * gl + 0.0792869141 * bl;
        z = 0.0451133819 * gl + 1.0439443689 * bl;
      }
      else if (space == ColorFnSpace::A98Rgb)
      {
        constexpr double kGamma = 563.0 / 256.0;
        rl = std::pow(r, kGamma);
        gl = std::pow(g, kGamma);
        bl = std::pow(b, kGamma);
        x = 0.5766690429 * rl + 0.1855582379 * gl + 0.1882286462 * bl;
        y = 0.2973449753 * rl + 0.6273635663 * gl + 0.0752914585 * bl;
        z = 0.0270313614 * rl + 0.0706888525 * gl + 0.9913375368 * bl;
      }
      else if (space == ColorFnSpace::ProPhotoRgb)
      {
        auto prophoto_linear = [](double c)
        {
          const double abs_c = std::abs(c);
          if (abs_c <= 16.0 / 512.0)
          {
            return c / 16.0;
          }
          return std::copysign(std::pow(abs_c, 1.8), c);
        };
        rl = prophoto_linear(r);
        gl = prophoto_linear(g);
        bl = prophoto_linear(b);
        const double x50 = 0.7977604896 * rl + 0.1351917082 * gl + 0.0313493495 * bl;
        const double y50 = 0.2880711282 * rl + 0.7118432178 * gl + 0.0000856540 * bl;
        const double z50 = 0.8251046026 * bl;
        x = kD50ToD65[0][0] * x50 + kD50ToD65[0][1] * y50 + kD50ToD65[0][2] * z50;
        y = kD50ToD65[1][0] * x50 + kD50ToD65[1][1] * y50 + kD50ToD65[1][2] * z50;
        z = kD50ToD65[2][0] * x50 + kD50ToD65[2][1] * y50 + kD50ToD65[2][2] * z50;
      }
      else // Rec2020
      {
        constexpr double kAlpha = 1.09929682680944;
        constexpr double kBeta = 0.018053968510807;
        auto rec2020_linear = [](double c)
        {
          const double abs_c = std::abs(c);
          if (abs_c < kBeta * 4.5)
          {
            return c / 4.5;
          }
          return std::copysign(std::pow((abs_c + kAlpha - 1.0) / kAlpha, 1.0 / 0.45), c);
        };
        rl = rec2020_linear(r);
        gl = rec2020_linear(g);
        bl = rec2020_linear(b);
        x = 0.6369580483 * rl + 0.1446169036 * gl + 0.1688809752 * bl;
        y = 0.2627002120 * rl + 0.6779980715 * gl + 0.0593017165 * bl;
        z = 0.0280726930 * gl + 1.0609850577 * bl;
      }
      return xyz_d65_to_rgb(x, y, z);
    }
    }
    return 0;
  }

  std::uint32_t hsluv_to_rgb(double h, double s, double l)
  {
    // HSLuv -> LCH: saturation is relative to the gamut boundary at (L, H).
    double c = 0.0;
    if (l > 99.9999999)
    {
      l = 100.0;
      c = 0.0;
    }
    else if (l < 0.00000001)
    {
      l = 0.0;
      c = 0.0;
    }
    else
    {
      c = max_safe_chroma(l, h) / 100.0 * s;
    }

    const double h_rad = h / 360.0 * 2.0 * M_PI;
    const double u = std::cos(h_rad) * c;
    const double v = std::sin(h_rad) * c;

    // LCH -> LUV -> XYZ (D65).
    if (l == 0.0)
    {
      return pack_rgb(0, 0, 0);
    }
    const double var_u = u / (13.0 * l) + kRefU;
    const double var_v = v / (13.0 * l) + kRefV;
    const double y = luv_y_for_l(l);
    const double denom = (var_u - 4.0) * var_v - var_u * var_v;
    if (std::abs(denom) < 1e-12)
    {
      // Degenerate: the hue sits on the LUV axis, so there is no chroma.
      return xyz_d65_to_rgb(0.0, y, 0.0);
    }
    const double x = -(9.0 * y * var_u) / denom;
    const double z = (9.0 * y - 15.0 * var_v * y - var_v * x) / (3.0 * var_v);
    return xyz_d65_to_rgb(x, y, z);
  }
} // namespace jot_color
