// The colour preview end to end: does a colour literal actually reach the
// screen as that colour, in both frontends' shared cell model?
//
// Everything here goes through a real Editor and a real render, so it covers
// the parts that a scan-only test cannot: which cells get painted, what the
// text colour becomes on a filled swatch, how the display modes differ, and
// that the switches and the exclusion list take effect without a restart.
#include "editor.h"
#include "features/color_codes.h"
#include "ui/ui.h"
#include "ui/xterm_palette.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_colorizer_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void load_text(Editor &e, const std::string &text, const std::string &ext = ".css")
  {
    static int counter = 0;
    const std::string path =
        "/tmp/jot_colorizer_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ext;
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(120, 40);
  }

  // Scans the rendered grid for a cell painted with the exact colour, and
  // reports the cell so the caller can check how its text was drawn.
  const UICell *find_cell_with_bg(UI *ui, std::uint32_t rgb)
  {
    for (int y = 0; y < ui->get_height(); y++)
    {
      for (int x = 0; x < ui->get_width(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        if (cell && cell->bg_rgb == rgb)
        {
          return cell;
        }
      }
    }
    return nullptr;
  }

  const UICell *find_cell_with_fg(UI *ui, std::uint32_t rgb)
  {
    for (int y = 0; y < ui->get_height(); y++)
    {
      for (int x = 0; x < ui->get_width(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        if (cell && cell->fg_rgb == rgb)
        {
          return cell;
        }
      }
    }
    return nullptr;
  }

  void render(Editor &e)
  {
    e.request_redraw_for_test();
    e.render_for_test();
  }

  // A theme colour as 0xRRGGBB: the bundled themes name exact colours, a
  // built-in default is a palette index.
  std::uint32_t theme_rgb(int value)
  {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    if (!jot_ui::exact_color_rgb(value, r, g, b))
    {
      jot_ui::palette_rgb(value, r, g, b);
    }
    return ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
  }
} // namespace

TEST_CASE("A hex literal is painted in its own colour", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "background");
  load_text(e, "a { color: #ff8800; }\nb { color: #003366; }\n");
  render(e);

  UI *ui = e.ui_for_test();
  const UICell *orange = find_cell_with_bg(ui, 0xFF8800u);
  REQUIRE(orange != nullptr);
  // Background mode flips the text for contrast: this fill is bright, so the
  // text must be black rather than the syntax colour it would otherwise have.
  REQUIRE(orange->fg_rgb == 0x000000u);

  const UICell *navy = find_cell_with_bg(ui, 0x003366u);
  REQUIRE(navy != nullptr);
  REQUIRE(navy->fg_rgb == 0xFFFFFFu);
}

TEST_CASE("Foreground mode colours the text and leaves the background", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "foreground");
  load_text(e, "a { color: #ff8800; }\n");
  render(e);

  UI *ui = e.ui_for_test();
  const UICell *orange_text = find_cell_with_fg(ui, 0xFF8800u);
  REQUIRE(orange_text != nullptr);
  // The background stays the theme's own, i.e. no fill was painted: a themed
  // cell carries the theme's colour as a 24-bit value now that the bundled
  // palettes are hex, so the check is against that rather than "nothing".
  REQUIRE(orange_text->bg_rgb == theme_rgb(e.theme_for_test().bg_default));
  REQUIRE(orange_text->bg_rgb != 0xFF8800u);

  // Switching back to background mode must take effect on the next frame.
  e.config_set_for_test("colorizer_mode", "background");
  render(e);
  REQUIRE(find_cell_with_bg(ui, 0xFF8800u) != nullptr);
  REQUIRE(find_cell_with_fg(ui, 0xFF8800u) == nullptr);
}

TEST_CASE("Named colours and functions are painted too", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "background");
  load_text(e, "a { color: LightBlue; }\nb { color: rgb(255, 0, 0); }\n");
  render(e);

  UI *ui = e.ui_for_test();
  REQUIRE(find_cell_with_bg(ui, 0xADD8E6u) != nullptr);
  REQUIRE(find_cell_with_bg(ui, 0xFF0000u) != nullptr);
}

TEST_CASE("The format switches are live", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "background");
  load_text(e, "a { color: red; }\n");
  render(e);
  UI *ui = e.ui_for_test();
  REQUIRE(find_cell_with_bg(ui, 0xFF0000u) != nullptr);

  // Turning names off must remove the swatch without a restart.
  e.config_set_for_test("colorizer_names", "false");
  render(e);
  REQUIRE(find_cell_with_bg(ui, 0xFF0000u) == nullptr);

  // And the master switch overrides everything.
  e.config_set_for_test("colorizer_names", "true");
  e.config_set_for_test("colorizer", "false");
  render(e);
  REQUIRE(find_cell_with_bg(ui, 0xFF0000u) == nullptr);
}

TEST_CASE("Excluded extensions are left alone", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "background");
  e.config_set_for_test("colorizer_exclude_filetypes", ".min.css,.map");

  load_text(e, "a { color: #ff8800; }\n", ".min.css");
  render(e);
  REQUIRE(find_cell_with_bg(e.ui_for_test(), 0xFF8800u) == nullptr);

  // The same content in a normal file is highlighted.
  load_text(e, "a { color: #ff8800; }\n", ".css");
  render(e);
  REQUIRE(find_cell_with_bg(e.ui_for_test(), 0xFF8800u) != nullptr);
}

TEST_CASE("Virtualtext mode leaves the text alone and appends a swatch", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "virtualtext");
  load_text(e, "a { color: #ff8800; }\n");
  render(e);

  UI *ui = e.ui_for_test();
  const std::uint32_t literal = 0xFF8800u;
  // The literal's own cells must not be filled: that is the whole point of
  // virtualtext mode, the text keeps its syntax colours.
  int literal_row = -1;
  int literal_col = -1;
  for (int y = 0; y < ui->get_height() && literal_row < 0; y++)
  {
    for (int x = 0; x + 7 <= ui->get_width(); x++)
    {
      std::string run;
      for (int k = 0; k < 7; k++)
      {
        const UICell *c = ui->cell_at(x + k, y);
        run += c ? c->ch : " ";
      }
      if (run == "#ff8800")
      {
        literal_row = y;
        literal_col = x;
        break;
      }
    }
  }
  REQUIRE(literal_row >= 0);
  for (int k = 0; k < 7; k++)
  {
    const UICell *c = ui->cell_at(literal_col + k, literal_row);
    REQUIRE(c != nullptr);
    // No fill and no recoloured text: neither side of the cell is the literal's
    // colour. The background is the theme's own (a hex theme's cells carry it as
    // an exact value), the foreground is whatever the syntax colouring used.
    REQUIRE(c->bg_rgb != literal);
    REQUIRE(c->fg_rgb != literal);
    REQUIRE(c->bg_rgb == theme_rgb(e.theme_for_test().bg_default));
  }

  // A swatch carrying the colour is drawn after the literal's text ends.
  int swatch_col = -1;
  for (int x = literal_col + 7; x < ui->get_width(); x++)
  {
    const UICell *c = ui->cell_at(x, literal_row);
    if (c && c->bg_rgb == 0xFF8800u)
    {
      swatch_col = x;
      break;
    }
  }
  REQUIRE(swatch_col > literal_col + 7);

  e.config_set_for_test("colorizer_mode", "background");
  render(e);
  const UICell *filled = ui->cell_at(literal_col, literal_row);
  REQUIRE(filled != nullptr);
  REQUIRE(filled->bg_rgb == 0xFF8800u);
}

TEST_CASE("A text selection still wins over the preview", "[jot][colorizer]")
{
  Editor &e = probe_editor();
  e.config_set_for_test("colorizer", "true");
  e.config_set_for_test("colorizer_mode", "background");
  load_text(e, "a { color: #ff8800; }\n");
  render(e);
  UI *ui = e.ui_for_test();
  REQUIRE(find_cell_with_bg(ui, 0xFF8800u) != nullptr);

  // Select the whole first line: the preview must yield to the selection, so
  // the swatch colour disappears from the grid.
  e.buffer_for_test().selection.start = {0, 0};
  e.buffer_for_test().selection.end = {20, 0};
  e.buffer_for_test().selection.active = true;
  render(e);
  REQUIRE(find_cell_with_bg(ui, 0xFF8800u) == nullptr);
}
