// The picture an image tab shows, drawn inside the pane that holds it.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace overlay_internal;

void Editor::render_image_viewer(const SplitPane &pane)
{
  if (pane.h <= 0)
    return;

  // Live inside the pane the image tab belongs to, the way any other file's
  // contents do: a window-wide panel reads as an overlay sitting over the
  // explorer. The cell grid starts under the pane's tab strip and stops above
  // its bottom edge, matching render_buffer_content.
  int area_x = pane.x;
  int area_y = pane.y + tab_height;
  int area_w = std::max(1, pane.w);
  int area_h = std::max(0, pane.h - tab_height);
  if (area_h <= 0)
    return;

  if (show_minimap && area_w > 20)
  {
    area_w = std::max(1, area_w - minimap_width);
  }

  UIRect clear_area = {area_x, area_y, area_w, area_h};
  ui->fill_rect(clear_area, " ", theme.fg_default, theme.bg_default);

  int img_w = std::clamp(area_w * 3 / 4, std::min(area_w, 40), area_w);
  int img_h = std::clamp(area_h * 3 / 4, std::min(area_h, 12), area_h);
  int img_x = area_x + std::max(0, (area_w - img_w) / 2);
  int img_y = area_y + std::max(0, (area_h - img_h) / 2);

  image_viewer.render(img_x, img_y, img_w, img_h, theme.fg_image_border, theme.bg_image_border);

  int x = image_viewer.get_view_x();
  int y = image_viewer.get_view_y();
  int vw = image_viewer.get_view_w();
  int vh = image_viewer.get_view_h();
  if (vw <= 2 || vh <= 2)
  {
    return;
  }

  UIRect panel = {x, y, vw, vh};
  ui->fill_rect(panel, " ", theme.fg_default, theme.bg_image_border);
  ui->draw_border(panel,
                  image_viewer.get_border_fg(),
                  image_viewer.get_border_bg(),
                  UIBorderEdges{false, false, false, false});

  std::string status = image_viewer.get_status_text();
  if (!status.empty())
  {
    ui->draw_text(x + 2, y, " " + status + " ", theme.fg_comment, theme.bg_default);
  }

  if (image_viewer.real_graphics_shown())
  {
    // The real picture is a terminal-side placement (kitty / sixel), emitted
    // from the frame's graphics pass once every pane has been laid out, so
    // there is nothing to draw into the cell grid here. A backend that produced
    // nothing (no conversion helper, no sixel support) falls through to the
    // cell preview instead of leaving the pane blank.
    return;
  }

  const auto &lines = image_viewer.get_preview_lines();
  const auto &pixels = image_viewer.get_color_preview_bg();
  const int text_x = x + 1;
  const int text_y = y + 1;
  const int text_w = std::max(1, vw - 2);
  const int text_h = std::max(1, vh - 2);
  const int ascii_rows = (int)lines.size();
  const bool has_color = image_viewer.has_color_preview_data();
  const int color_rows = has_color ? (int)pixels.size() : 0;
  // One scroll offset covers the whole preview -- caption and ASCII art first,
  // a blank row, then the color block -- so the wheel and the navigation keys
  // pan it the way they pan a file's text.
  const int first_row = image_viewer.get_preview_scroll();

  for (int row_on_screen = 0; row_on_screen < text_h; row_on_screen++)
  {
    const int row = first_row + row_on_screen;
    const int draw_y = text_y + row_on_screen;
    if (row < ascii_rows)
    {
      std::string line = lines[(size_t)row];
      if ((int)line.size() > text_w)
      {
        line = line.substr(0, text_w);
      }
      ui->draw_text(text_x, draw_y, line, theme.fg_default, theme.bg_default);
      continue;
    }
    if (!has_color)
    {
      continue;
    }
    const int rel = row - ascii_rows;
    if (rel <= 0)
    {
      continue; // the blank row between the art and the color block
    }
    const int py = rel - 1;
    if (py >= color_rows)
    {
      continue;
    }
    const int cols = std::min((int)pixels[(size_t)py].size(), text_w);
    for (int px = 0; px < cols; px++)
    {
      ui->draw_text(text_x + px, draw_y, " ", theme.fg_default, pixels[(size_t)py][(size_t)px]);
    }
  }
}
