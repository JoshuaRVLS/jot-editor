// Truecolour plumbing for the cell model and the colour-value domain.
//
// jot's cells were xterm-256 palette indices only, so a colour preview could
// never show the colour a literal actually named. UICell now carries an
// optional 24-bit value that wins over the index; these cases pin both ends of
// that: the GUI resolves it exactly, and the terminal emits 38;2/48;2 when it
// is supported and quantises to the nearest palette entry when it is not.
//
// The same value domain is what a theme file's hex colours land in: values from
// kExactColorBase up are interned exact colours, which is why a hex theme needs
// no special case in the renderer -- but it does need the resolution at the
// point a cell is built, which the last cases cover.
#include "ui/terminal.h"
#include "ui/ui.h"
#include "ui/xterm_palette.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace jot_ui;

namespace
{
  // UI's colour emitter is an implementation detail (protected); a case that
  // wants to read the SGR it produces opens that one door without widening the
  // production interface.
  class SgrProbeUI : public UI
  {
  public:
    explicit SgrProbeUI(Terminal *t) : UI(t) {}
    using UI::emit_cell_colors;
    using UI::emit_full_row;
  };
} // namespace

TEST_CASE("Hex colours intern into exact values, indices stay indices", "[jot][palette]")
{
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;

  // The documented forms: #rgb, #rrggbb, #rrggbbaa (alpha dropped), any case,
  // surrounding whitespace tolerated.
  REQUIRE(parse_hex_color("#ff8800", r, g, b));
  REQUIRE(r == 0xFF);
  REQUIRE(g == 0x88);
  REQUIRE(b == 0x00);
  REQUIRE(parse_hex_color("#f80", r, g, b));
  REQUIRE((r == 0xFF && g == 0x88 && b == 0x00));
  REQUIRE(parse_hex_color("#F80", r, g, b));
  REQUIRE((r == 0xFF && g == 0x88 && b == 0x00));
  REQUIRE(parse_hex_color("#ff880080", r, g, b));
  REQUIRE((r == 0xFF && g == 0x88 && b == 0x00));
  REQUIRE(parse_hex_color("  #0f0  ", r, g, b));
  REQUIRE((r == 0x00 && g == 0xFF && b == 0x00));

  // Not colours: a bare palette index, a truncated value, a non-hex digit, an
  // empty string. A theme slot that carries one of these is left at the value
  // it inherited rather than painted black.
  REQUIRE_FALSE(parse_hex_color("215", r, g, b));
  REQUIRE_FALSE(parse_hex_color("#12345", r, g, b));
  REQUIRE_FALSE(parse_hex_color("#gggggg", r, g, b));
  REQUIRE_FALSE(parse_hex_color("#", r, g, b));
  REQUIRE_FALSE(parse_hex_color("", r, g, b));
  REQUIRE(exact_color_from_hex("chartreuse") == -1);

  // An exact colour is interned once and resolves back to itself, so the same
  // rgb named by two themes (or by a Lua set_hl and a theme file) shares an id.
  const int amber = exact_color_from_hex("#5a5b5c");
  REQUIRE(amber >= kExactColorBase);
  REQUIRE(is_exact_color(amber));
  const std::size_t after_first = exact_color_count();
  REQUIRE(exact_color_from_hex("#5A5B5C") == amber);
  REQUIRE(exact_color_count() == after_first); // no second entry
  REQUIRE(exact_color_rgb(amber, r, g, b));
  REQUIRE((r == 0x5A && g == 0x5B && b == 0x5C));

  // Every consumer reads colours through palette_rgb, so an exact colour is
  // resolved by the GUI, the caret's contrast maths and the SGR emitter alike.
  palette_rgb(amber, r, g, b);
  REQUIRE((r == 0x5A && g == 0x5B && b == 0x5C));
  REQUIRE(contrast_ratio(amber, 0) > 1.5f);
  // A palette index still resolves through the palette.
  REQUIRE_FALSE(is_exact_color(255));
  REQUIRE_FALSE(is_exact_color(-1));
  // A value that was never handed out is not an exact colour, and an
  // out-of-range one still blacks out instead of reading past the table.
  REQUIRE_FALSE(is_exact_color(kExactColorBase + 100000));
  palette_rgb(kExactColorBase + 100000, r, g, b);
  REQUIRE((r == 0 && g == 0 && b == 0));
}

TEST_CASE("A theme's exact colour reaches the cell as 24-bit", "[jot][ui]")
{
  // The cell builders are the choke point: a painter hands over the int it got
  // from the theme, and the cell comes out with the 24-bit companion both
  // backends read. Nothing in the painting code has to know about hex colours.
  Terminal term;
  SgrProbeUI ui(&term);
  ui.resize(20, 4);

  const int ink = exact_color_from_hex("#e8ddcc");
  const int paper = exact_color_from_hex("#1e1b18");
  const int slate = exact_color_from_hex("#4a4440");
  REQUIRE(ink >= kExactColorBase);
  ui.set_default_colors(ink, paper);

  // The clear path (blank_cell) resolves the theme's default pair.
  ui.clear();
  const UICell *blank = ui.cell_at(2, 2);
  REQUIRE(blank != nullptr);
  REQUIRE(blank->fg == ink);
  REQUIRE(blank->fg_rgb == 0xE8DDCCu);
  REQUIRE(blank->bg_rgb == 0x1E1B18u);

  // draw_text carries the exact colour through to the cell, including the
  // background of a selection the painter only had as an int.
  ui.draw_text(0, 0, "hi", ink, slate);
  const UICell *cell = ui.cell_at(0, 0);
  REQUIRE(cell != nullptr);
  REQUIRE(cell->fg_rgb == 0xE8DDCCu);
  REQUIRE(cell->bg_rgb == 0x4A4440u);

  // An explicit 24-bit value still wins over the int (the inline colour
  // preview), and a palette index is left without a 24-bit companion.
  ui.draw_text(0, 1, "x", 4, 5, false, false, 0, -1, 0x123456u, kNoRgb);
  const UICell *explicit_rgb = ui.cell_at(0, 1);
  REQUIRE(explicit_rgb->fg_rgb == 0x123456u);
  REQUIRE(explicit_rgb->bg_rgb == kNoRgb);
  ui.draw_text(0, 2, "y", 4, 5);
  const UICell *index_cell = ui.cell_at(0, 2);
  REQUIRE(index_cell->fg == 4);
  REQUIRE(index_cell->fg_rgb == kNoRgb);

  // And the terminal emits it as truecolour, which is the other end of the
  // promise: a hex theme on a truecolor terminal is 38;2 / 48;2, not the
  // nearest of 256 entries.
  term.set_truecolor_supported(true);
  term.clear_pending_output_for_test();
  ui.emit_cell_colors(*cell);
  const std::string emitted = term.pending_output_for_test();
  REQUIRE(emitted.find("38;2;232;221;204") != std::string::npos);
  REQUIRE(emitted.find("48;2;74;68;64") != std::string::npos);
  REQUIRE(emitted.find("38;5;") == std::string::npos);

  // A cell that only knows a palette index is untouched: no invented 24-bit
  // colour, and the index goes out as 38;5.
  term.clear_pending_output_for_test();
  ui.emit_cell_colors(*index_cell);
  REQUIRE(term.pending_output_for_test().find("38;5;4m") != std::string::npos);
  REQUIRE(term.pending_output_for_test().find("38;2;") == std::string::npos);

  // Dimming an exact colour stays exact (a scrim over a hex theme dims the 24-bit
  // value rather than quantising it to the nearest of 256 first), and the
  // "reads dark" probe that decides whether SGR 2 is needed resolves the exact
  // background too instead of decoding an id as an index.
  ui.dim_rect(UIRect{0, 0, 20, 1});
  term.clear_pending_output_for_test();
  ui.emit_cell_colors(*cell);
  REQUIRE(term.pending_output_for_test().find("38;2;127;121;112") != std::string::npos);
}

TEST_CASE("The terminal emits a 24-bit underline colour when it can", "[jot][palette]")
{
  Terminal term;
  term.clear_pending_output_for_test();

  // The indexed form is unchanged, and -1 still resets to the text colour.
  term.set_underline_color(3, kNoRgb);
  REQUIRE(term.pending_output_for_test() == "\x1b[58;5;3m");
  term.clear_pending_output_for_test();
  term.set_underline_color(-1, kNoRgb);
  REQUIRE(term.pending_output_for_test() == "\x1b[59m");

  // A 24-bit value on a terminal that does not understand it folds to the
  // nearest palette entry, the way set_color does -- never emitted raw.
  term.clear_pending_output_for_test();
  REQUIRE_FALSE(term.supports_truecolor());
  term.set_underline_color(3, 0xFF8800u);
  const std::string folded = term.pending_output_for_test();
  REQUIRE(folded.find("58;5;") != std::string::npos);
  REQUIRE(folded.find("58:2") == std::string::npos);

  // And on a terminal that does, the exact value goes out in SGR 58's colon form.
  term.clear_pending_output_for_test();
  term.set_truecolor_supported(true);
  term.set_underline_color(3, 0xFF8800u);
  REQUIRE(term.pending_output_for_test() == "\x1b[58:2::255:136:0m");
}

TEST_CASE("An exact underline colour reaches the cell and the row", "[jot][ui]")
{
  // Decorations and themes carry underline colours the same way they carry text
  // colours: one int that may be an exact 24-bit value. The cell has to keep it
  // (and its 24-bit companion), and the row painter emits SGR 58 in its 24-bit
  // form -- the underline is no longer folded to the nearest palette entry.
  Terminal term;
  SgrProbeUI ui(&term);
  ui.resize(20, 4);
  const int underline = exact_color_from_hex("#44cc99");
  REQUIRE(is_exact_color(underline));

  ui.draw_text(0, 0, "wavy", 4, 5, false, false, 2, underline);
  const UICell *cell = ui.cell_at(0, 0);
  REQUIRE(cell != nullptr);
  REQUIRE(cell->underline == 2);
  REQUIRE(cell->underline_fg == underline); // the exact value stays the cell's colour
  REQUIRE(cell->underline_rgb == 0x44CC99u);

  term.set_truecolor_supported(true);
  term.clear_pending_output_for_test();
  ui.emit_full_row(0, 20);
  const std::string emitted = term.pending_output_for_test();
  REQUIRE(emitted.find("4:3") != std::string::npos); // the wavy underline
  REQUIRE(emitted.find("58:2::68:204:153") != std::string::npos);

  // A palette-index underline keeps the indexed form: only exact colours are
  // promoted, and a cell with no underline colour emits nothing for SGR 58.
  ui.draw_text(0, 1, "plain", 4, 5, false, false, 1, 6);
  const UICell *indexed = ui.cell_at(0, 1);
  REQUIRE(indexed->underline_fg == 6);
  REQUIRE(indexed->underline_rgb == kNoRgb);
  term.clear_pending_output_for_test();
  ui.emit_full_row(1, 20);
  const std::string indexed_row = term.pending_output_for_test();
  REQUIRE(indexed_row.find("58;5;6m") != std::string::npos);
  REQUIRE(indexed_row.find("58:2") == std::string::npos);
}

TEST_CASE("Nearest palette index round-trips the palette", "[jot][colorizer]")
{
  // Every palette entry must map to a colour that is *identical* to it, or
  // quantising would shift a colour even when the palette already has it. The
  // index itself is not asserted because the palette repeats some colours
  // (index 0 and 16 are both black, 15 and 231 both white), so either is a
  // correct answer for those.
  for (int i = 0; i < 256; i++)
  {
    unsigned char r = 0, g = 0, b = 0;
    palette_rgb(i, r, g, b);
    const int hit = palette_nearest_index(r, g, b);
    unsigned char hr = 0, hg = 0, hb = 0;
    palette_rgb(hit, hr, hg, hb);
    REQUIRE(hr == r);
    REQUIRE(hg == g);
    REQUIRE(hb == b);
  }

  // Pure primaries land on the palette's own primaries.
  REQUIRE(palette_nearest_index(0, 0, 0) == 0);
  REQUIRE(palette_nearest_index(255, 255, 255) == 15);
  REQUIRE(palette_nearest_index(255, 0, 0) == 9);
  // A colour between two palette entries picks the closer one.
  const int dark = palette_nearest_index(8, 8, 8);
  unsigned char dr = 0, dg = 0, db = 0;
  palette_rgb(dark, dr, dg, db);
  REQUIRE(dr <= 10);
}

TEST_CASE("The terminal emits truecolour only when it is supported", "[jot][colorizer]")
{
  Terminal term;
  term.clear_pending_output_for_test();

  // Palette-only call: unchanged behaviour, the index goes out as 38;5/48;5.
  term.set_color(1, 2);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;5;1m\x1b[48;5;2m");

  // Without truecolor support a 24-bit value is quantised at emit time, so the
  // same cell still shows the closest available colour.
  term.clear_pending_output_for_test();
  REQUIRE_FALSE(term.supports_truecolor());
  term.set_color(1, 2, 0xFF8800u, 0x001122u);
  const std::string quantised = term.pending_output_for_test();
  REQUIRE(quantised.find("38;5;") != std::string::npos);
  REQUIRE(quantised.find("48;5;") != std::string::npos);
  REQUIRE(quantised.find("38;2;") == std::string::npos);

  // With support the exact value goes out as 38;2/48;2.
  term.clear_pending_output_for_test();
  term.set_truecolor_supported(true);
  REQUIRE(term.supports_truecolor());
  term.set_color(1, 2, 0xFF8800u, 0x001122u);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;2;255;136;0m\x1b[48;2;0;17;34m");

  // A cell with only one of the two sides in 24-bit colour mixes the forms.
  term.clear_pending_output_for_test();
  term.set_color(4, 5, kNoRgb, 0x0A0B0Cu);
  REQUIRE(term.pending_output_for_test() == "\x1b[38;5;4m\x1b[48;2;10;11;12m");
}

TEST_CASE("The environment decides the auto truecolor answer", "[jot][colorizer]")
{
  // terminal_env_supports_truecolor() reads the real environment; set and
  // restore it so the assertion is deterministic either way.
  const char *saved_colorterm = std::getenv("COLORTERM");
  const char *saved_term = std::getenv("TERM");
  const std::string colorterm = saved_colorterm ? saved_colorterm : "";
  const std::string term_value = saved_term ? saved_term : "";

  setenv("COLORTERM", "truecolor", 1);
  setenv("TERM", "xterm-256color", 1);
  REQUIRE(terminal_env_supports_truecolor());

  setenv("COLORTERM", "24bit", 1);
  REQUIRE(terminal_env_supports_truecolor());

  setenv("COLORTERM", "", 1);
  setenv("TERM", "xterm-direct", 1);
  REQUIRE(terminal_env_supports_truecolor());

  // An ordinary 256-colour terminal must not be claimed as truecolor: guessing
  // wrong would paint every preview cell wrong.
  setenv("COLORTERM", "", 1);
  setenv("TERM", "xterm-256color", 1);
  REQUIRE_FALSE(terminal_env_supports_truecolor());

  if (saved_colorterm)
  {
    setenv("COLORTERM", colorterm.c_str(), 1);
  }
  else
  {
    unsetenv("COLORTERM");
  }
  if (saved_term)
  {
    setenv("TERM", term_value.c_str(), 1);
  }
  else
  {
    unsetenv("TERM");
  }
}

TEST_CASE("A cell's 24-bit colour survives the diff renderer", "[jot][colorizer]")
{
  // The row-diff renderer compares cells to decide what to re-emit. Two cells
  // that differ only in their 24-bit colour must compare unequal, or the
  // second colour would never be painted.
  UICell a;
  a.ch = "x";
  a.fg = 1;
  a.bg = 2;
  UICell b = a;
  REQUIRE(a == b);

  b.bg_rgb = 0x123456u;
  REQUIRE(a != b);
  b.bg_rgb = kNoRgb;
  b.fg_rgb = 0x654321u;
  REQUIRE(a != b);
  REQUIRE(a == UICell(a));
}

// The trap that produced black cells: UICell's optional 24-bit colours sit
// between `bg` and `bold`, so a positional brace initializer written before they
// existed ({" ", fg, bg, false, false, false}) assigns `false` -- i.e. 0,
// truecolor *black* -- to them instead of leaving them unset. The clear path
// used exactly that form, so every cell nothing painted afterwards rendered
// black-on-black: visibly a black cell at each end of the tab row.
//
// This pins the invariant the clear path has to satisfy, and the helper it now
// uses (UI::blank_cell) is what makes it hold.
TEST_CASE("A cleared grid carries no 24-bit colours", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(24, 6);

  for (int y = 0; y < 6; y++)
  {
    for (int x = 0; x < 24; x++)
    {
      const UICell *cell = ui.cell_at(x, y);
      REQUIRE(cell != nullptr);
      // kNoRgb, not 0: 0 means "paint truecolor black", which is a very
      // different thing from "no explicit colour".
      REQUIRE(cell->fg_rgb == kNoRgb);
      REQUIRE(cell->bg_rgb == kNoRgb);
    }
  }

  // clear() (which the frame loop calls every frame) must preserve that.
  ui.clear();
  const UICell *after = ui.cell_at(3, 3);
  REQUIRE(after != nullptr);
  REQUIRE(after->fg_rgb == kNoRgb);
  REQUIRE(after->bg_rgb == kNoRgb);
}

// fill_rect paints a whole region in one colour pair, and it resolves that pair
// once for the rect rather than once per cell (the rects it is called with
// cover the screen every frame). The cell it writes still has to carry the
// 24-bit companions, or a hex theme's background would quantise to a palette
// index wherever a panel was filled rather than drawn.
TEST_CASE("A filled rect carries its exact colours to every cell", "[jot][ui]")
{
  Terminal term;
  UI ui(&term);
  ui.resize(20, 8);

  const int fg = exact_color_from_hex("#ff8800");
  const int bg = exact_color_from_hex("#102030");
  REQUIRE(fg >= kExactColorBase);
  REQUIRE(bg >= kExactColorBase);

  ui.fill_rect({2, 1, 5, 3}, " ", fg, bg);

  for (int y = 1; y < 4; y++)
  {
    for (int x = 2; x < 7; x++)
    {
      const UICell *cell = ui.cell_at(x, y);
      REQUIRE(cell != nullptr);
      REQUIRE(cell->fg == fg);
      REQUIRE(cell->bg == bg);
      REQUIRE(cell->fg_rgb == 0xff8800u);
      REQUIRE(cell->bg_rgb == 0x102030u);
    }
  }

  // Outside the rect nothing is touched -- including the cell just past each
  // edge, which a clamp that used <= would have painted.
  const UICell *left_of = ui.cell_at(1, 2);
  const UICell *below = ui.cell_at(3, 4);
  REQUIRE(left_of != nullptr);
  REQUIRE(below != nullptr);
  REQUIRE(left_of->fg_rgb == kNoRgb);
  REQUIRE(below->fg_rgb == kNoRgb);

  // A rect that starts off-screen is clipped, not indexed out of bounds: this
  // one covers x 0..1 and y 0..1, so the origin is painted and its far corner
  // is the cell just past it.
  ui.fill_rect({-4, -3, 6, 5}, " ", fg, bg);
  const UICell *origin = ui.cell_at(0, 0);
  REQUIRE(origin != nullptr);
  REQUIRE(origin->fg_rgb == 0xff8800u);
  const UICell *past_clipped = ui.cell_at(8, 5);
  REQUIRE(past_clipped != nullptr);
  REQUIRE(past_clipped->fg_rgb == kNoRgb);
}
