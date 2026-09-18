// The inline image viewer overlay.
#include "bracket.h"
#include "column_utils.h"
#include "editor.h"
#include "folding.h"
#include "jot/lua/api.h"
#include "render/overlay_internal.h"
#include "ui/text.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>

using namespace overlay_internal;

void Editor::render_image_viewer()
{
  int w = ui->get_render_width();
  int h = ui->get_height();
  if (w < 4 || h <= status_height + tab_height)
    return;

  int area_x = show_sidebar ? effective_sidebar_width() : 0;
  int area_y = tab_height;
  int area_w = std::max(1, w - area_x);
  int area_h = std::max(1, h - status_height - tab_height);

  UIRect clear_area = {area_x, area_y, area_w, area_h};
  ui->fill_rect(clear_area, " ", theme.fg_default, theme.bg_default);

  int img_w = std::clamp(area_w * 3 / 4, std::min(area_w, 40), area_w);
  int img_h = std::clamp(area_h * 3 / 4, std::min(area_h, 12), area_h);
  int img_x = area_x + std::max(0, (area_w - img_w) / 2);
  int img_y = area_y + std::max(0, (area_h - img_h) / 2);

  image_viewer.render(img_x, img_y, img_w, img_h, theme.fg_image_border, theme.bg_image_border);

  // Taken here, not from the frame's own pass: this function is what runs for
  // the viewer, and the frame-side call was unreachable. render() above has
  // just set the geometry the command is built from.
  const std::string graphics = image_viewer.take_graphics_output();
  if (!graphics.empty())
  {
    ui->set_frame_graphics(graphics);
  }

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

  if (image_viewer.uses_real_graphics() && !graphics.empty())
  {
    return;
  }

  const auto &lines = image_viewer.get_preview_lines();
  int text_x = x + 1;
  int text_y = y + 1;
  int text_w = std::max(1, vw - 2);
  int text_h = std::max(1, vh - 2);

  int line_y = text_y;
  for (int i = 0; i < text_h && i < (int)lines.size(); i++)
  {
    std::string line = lines[i];
    if ((int)line.size() > text_w)
    {
      line = line.substr(0, text_w);
    }
    ui->draw_text(text_x, line_y++, line, theme.fg_default, theme.bg_default);
  }

  if (image_viewer.has_color_preview_data() && line_y < text_y + text_h)
  {
    line_y += 1;
    const auto &pixels = image_viewer.get_color_preview_bg();
    int max_rows = std::max(0, text_y + text_h - line_y);
    int rows = std::min((int)pixels.size(), max_rows);
    for (int py = 0; py < rows; py++)
    {
      int cols = std::min((int)pixels[py].size(), text_w);
      for (int px = 0; px < cols; px++)
      {
        int bg = pixels[py][px];
        ui->draw_text(text_x + px, line_y + py, " ", theme.fg_default, bg);
      }
    }
  }
}
