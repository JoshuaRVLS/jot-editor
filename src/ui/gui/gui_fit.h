#ifndef UI_GUI_FIT_H
#define UI_GUI_FIT_H

// Grid geometry for the GUI frontend, kept free of SDL/GL so it is unit
// testable on its own.
//
// The GUI renders in *device pixels*: glyphs are rasterized at a pixel size
// (FreeType), the cell metrics come out of those bitmaps, and the GL viewport
// is the drawable size. SDL, on the other hand, reports window sizes and mouse
// coordinates in *logical points*. Dividing a point-sized window by a
// device-pixel cell -- which is what the resize paths used to do -- leaves the
// grid covering only 1/scale of a scaled window, with the editor stuck in the
// top-left corner, and it puts pointer input on the wrong cell. These helpers
// keep the two unit systems explicit at every boundary.

namespace jot_gui
{
  // Device pixels per logical point: 1.0 on an unscaled display, 2.0 on a 2x
  // one, 1.25/1.5 under fractional compositor scaling. Clamped to >= 1 so text
  // is never shrunk to fit a smaller drawable, and to a sane upper bound.
  float display_scale(int window_w, int window_h, int drawable_w, int drawable_h);

  struct GridFit
  {
    int cols = 1;
    int rows = 1;
  };

  // Largest grid of `cell_w_px` x `cell_h_px` device-pixel cells that fits in a
  // `drawable_w` x `drawable_h` device-pixel drawable. Always at least 1x1 so a
  // momentarily zero-sized window cannot produce an empty grid.
  GridFit fit_grid(int drawable_w, int drawable_h, float cell_w_px, float cell_h_px);

  // Logical (point) size of one cell, for translating SDL point coordinates
  // onto grid columns and rows.
  float point_cell_size(float cell_px, float scale);

  // One axis of the caret's glide, in device pixels. The caret eases toward
  // `target` so short moves read as motion rather than teleports -- except when
  // the pane under it is scrolling (`snap`), where the content is already
  // sliding and easing the caret separately would detach it from the line it
  // belongs to. Also snaps when the remaining distance is sub-pixel.
  float glide_axis(float current, float target, float dt, bool snap);
} // namespace jot_gui

#endif // UI_GUI_FIT_H
