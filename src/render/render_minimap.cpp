#include "bracket.h"
#include "editor.h"
#include <cctype>
#include <cstdio>
#include <sstream>

void Editor::render_minimap(int x, int y, int w, int h, int buffer_id) {
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
    return;
  if (w <= 0 || h <= 0)
    return;
  auto &buf = buffers[buffer_id];

  // Draw background
  UIRect rect = {x, y, w, h};
  ui->fill_rect(rect, " ", theme.fg_minimap, theme.bg_minimap);

  // Simple compressed view
  int total_lines = buf.lines.size();
  if (total_lines == 0)
    return;

  // Viewport indicator
  auto &pane = get_pane();
  float ratio = (float)h / total_lines;
  if (ratio > 1.0f)
    ratio = 1.0f;

  int viewport_y = (int)(buf.scroll_offset * ratio);
  int viewport_h = (int)(pane.h * ratio);
  if (viewport_h < 1)
    viewport_h = 1;

  // Highlight viewport background
  UIRect viewport = {x, y + viewport_y, w, viewport_h};
  ui->fill_rect(viewport, " ", theme.fg_minimap, theme.bg_selection);

  // Draw content (blocks)
  for (int i = 0; i < h; i++) {
    int line_idx = (int)(i / ratio);
    if (line_idx < total_lines) {
      const std::string &line = buf.lines[line_idx];
      const auto &colors = get_line_syntax_colors(buf, line_idx);

      int draw_x = x;
      int max_x = x + w;
      // Draw blocks: 1 block per 4 chars approx, or just sample colors
      // Let's iterate through colors.
      // We will draw a block for every ~4 characters.
      // But we need to pick the color of that chunk.
      // Simplest: Just iterate through the line,
      // if char is space, skip. If char is code, draw block with its color.

      // We need to map linear char index to color
      // Colors are pair<style, color_code> for ranges?
      // No, get_colors returns vector of pairs per character?
      // Let's check syntax.cpp...
      // get_colors(line) returns vector<pair<int, int>> which is one pair per
      // character. Pair is {bold, color}.

      for (size_t k = 0; k < line.length(); k += 4) {
        if (draw_x >= max_x)
          break;

        // Check if chunk has non-space
        bool has_code = false;
        int chunk_color = theme.fg_minimap;

        for (size_t j = 0; j < 4 && k + j < line.length(); j++) {
          if (!std::isspace(line[k + j])) {
            has_code = true;
            // Get color of this char
            if (k + j < colors.size()) {
              int c = colors[k + j].second;
              if (c != 0)
                chunk_color = c;
            }
            break; // Found code, use this color
          }
        }

        if (has_code) {
          // Block character: UTF-8 for full block is \xE2\x96\x88
          // ui->draw_text expects std::string.
          // Windows/Curses might need special handling but we are on Linux zsh.
          // We can use a simple pipe | or # or just unicode block.
          // Let's try unicode block.
          ui->draw_text(draw_x, y + i, "\u2588", chunk_color, theme.bg_minimap);
        }
        draw_x++;
      }
    }
  }
}
