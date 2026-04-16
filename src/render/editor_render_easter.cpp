#include "editor.h"
#include <cstdio>
#include <sstream>

void Editor::render_easter_egg() {
  if (easter_egg_timer <= 0)
    return;

  int h = ui->get_height();
  int w = ui->get_width();

  // Color cycles every 6 frames → gives a smooth rainbow sweep
  int phase = (easter_egg_timer / 4) % 6;

  // ASCII art lines for the popup
  static const std::vector<std::string> art = {
    "  ____  __.                         .__ ",
    " |    |/ _|___  ____   ____   _____ |__|",
    " |      < /  _ \\/    \\ /  _ \\_/ __ \\|  |",
    " |    |  (  <_> )   |  (  <_> )  ___/|  |",
    " |____|__ \\____/|___|  /\\____/ \\___  >__|",
    "         \\/          \\/            \\/   ",
  };

  static const std::vector<std::string> msg = {
    "",
    "   \xe2\x86\x91\xe2\x86\x91\xe2\x86\x93\xe2\x86\x93\xe2\x86\x90\xe2\x86\x92\xe2\x86\x90\xe2\x86\x92   KONAMI CODE ACTIVATED!   ",
    "",
    "    +30 Lives  \xc2\xb7  God Mode  \xc2\xb7  No Clip    ",
    "       Welcome to jcode, player one.      ",
    "",
  };

  // Compute box size: wide enough for the longest art line
  int box_w = 50;
  int box_h = (int)(art.size() + msg.size()) + 4;
  int bx = std::max(0, (w - box_w) / 2);
  int by = std::max(0, (h - box_h) / 2);

  // Clamp to screen
  if (bx + box_w > w) box_w = w - bx;
  if (by + box_h > h) box_h = h - by;

  // Shadow
  UIRect shadow = {bx + 1, by + 1, box_w, box_h};
  ui->draw_rect(shadow, 0, 0);

  // Background fill (dark)
  UIRect rect = {bx, by, box_w, box_h};
  ui->fill_rect(rect, " ", 7, 0);
  // Border in current phase color
  ui->draw_border(rect, (phase % 6) + 1, 0);

  // Draw ASCII art with cycling colors
  int row = by + 2;
  for (int i = 0; i < (int)art.size() && row < by + box_h - 1; i++, row++) {
    int color = ((phase + i) % 6) + 1;
    std::string line = art[i];
    if ((int)line.length() > box_w - 2)
      line = line.substr(0, box_w - 2);
    ui->draw_text(bx + 1, row, line, color, 0);
  }

  // Draw message lines
  for (int i = 0; i < (int)msg.size() && row < by + box_h - 1; i++, row++) {
    int color = ((phase + i + 2) % 6) + 1;
    std::string line = msg[i];
    if ((int)line.length() > box_w - 2)
      line = line.substr(0, box_w - 2);
    ui->draw_text(bx + 1, row, line, color, 0);
  }

  // Countdown bar at the bottom of the box
  int bar_y = by + box_h - 2;
  if (bar_y > by && bar_y < by + box_h) {
    int bar_len = (int)((float)easter_egg_timer / 180.0f * (box_w - 2));
    std::string bar(bar_len, '#');
    int bar_color = ((phase + 3) % 6) + 1;
    ui->draw_text(bx + 1, bar_y, bar, bar_color, 0);
  }
}
