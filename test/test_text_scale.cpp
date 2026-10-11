// The terminal frontend's text scale.
//
// A terminal program cannot change its own font: the emulator owns the glyphs
// and jot only places characters in cells. What it can do is ask the terminal to
// draw a character in a block of cells -- kitty's text sizing protocol, OSC 66 --
// and then lay the whole UI out on a grid that many times smaller. These cases
// pin the three halves of that: the bytes a scaled cell is written as, the
// coordinate translation it needs (cursor moves and mouse reports both speak
// terminal cells at the edge and grid cells everywhere else), and the refusal
// when the terminal has no such protocol -- a grid drawn at scaled coordinates a
// terminal does not scale would come out in pieces, so it must not be drawn.
#include "core/editor.h"
#include "ui/terminal.h"
#include "ui/text.h"
#include "ui/ui.h"
#include "ui/xterm_palette.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jot_ui;

namespace
{
  // One editor for the file, on a config home of its own: the zoom reads the
  // terminal it was started in, and the case below pins what a refused zoom
  // leaves behind in that config.
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_text_scale_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // emit_full_row is the renderer's full-row writer; a case reads the bytes it
  // produces instead of flushing a whole frame into the test's own output.
  class RowProbeUI : public UI
  {
  public:
    explicit RowProbeUI(Terminal *t) : UI(t) {}
    using UI::emit_full_row;
  };

  // The texts of the OSC 66 frames a scaled write queued.
  std::vector<std::string> frames_of(const std::string &out, int scale)
  {
    const std::string head = "\x1b]66;s=" + std::to_string(scale) + ";";
    std::vector<std::string> texts;
    std::size_t at = 0;
    while ((at = out.find(head, at)) != std::string::npos)
    {
      const std::size_t start = at + head.size();
      const std::size_t end = out.find('\a', start);
      REQUIRE(end != std::string::npos);
      texts.push_back(out.substr(start, end - start));
      at = end + 1;
    }
    return texts;
  }

  std::string join(const std::vector<std::string> &parts)
  {
    std::string out;
    for (const std::string &part : parts)
    {
      out += part;
    }
    return out;
  }
} // namespace

TEST_CASE("A scaled cell is written as a block the terminal draws", "[jot][scale]")
{
  Terminal term;
  term.set_cell_scale(2);
  term.clear_pending_output_for_test();
  term.write_scaled_text("ab");
  // One frame: the protocol's header, the text, and its BEL terminator. The
  // terminal draws the two characters across two cells each.
  REQUIRE(term.pending_output_for_test() == "\x1b]66;s=2;ab\a");

  // At 1:1 there is no protocol and no framing: the text is just text.
  term.set_cell_scale(1);
  term.clear_pending_output_for_test();
  term.write_scaled_text("ab");
  REQUIRE(term.pending_output_for_test() == "ab");
}

TEST_CASE("A long run is split at the protocol's frame limit", "[jot][scale]")
{
  Terminal term;
  term.set_cell_scale(3);
  // The cap is 4096 bytes of frame text, and a multi-byte character that lands
  // on the boundary may not be cut in half: the terminal would draw a
  // replacement character for the broken half of it.
  std::string text(4094, 'a');
  text += "\xc3\xa9"; // é, two bytes, straddling the cap
  text += std::string(10, 'b');
  term.write_scaled_text(text);

  const std::vector<std::string> frames = frames_of(term.pending_output_for_test(), 3);
  REQUIRE(frames.size() == 2);
  for (const std::string &frame : frames)
  {
    REQUIRE(frame.size() <= 4096);
    REQUIRE(ui_is_valid_utf8_sequence(frame));
  }
  REQUIRE(join(frames) == text);
}

TEST_CASE("Framed text folds the bytes that would end the frame", "[jot][scale]")
{
  // BEL ends an OSC string, so a cell holding one (a file with a literal bell in
  // it) would cut its own frame short and shift the rest of the row. The byte is
  // folded to a space, which keeps the frame's cell count exact.
  Terminal term;
  term.set_cell_scale(2);
  term.write_scaled_text(std::string("a") + '\a' + "b" + '\x1b' + "\x7f");
  REQUIRE(term.pending_output_for_test() == "\x1b]66;s=2;a b  \a");
}

TEST_CASE("Cursor moves address the block a scaled cell starts in", "[jot][scale]")
{
  Terminal term;
  term.move_cursor(3, 2);
  REQUIRE(term.pending_output_for_test() == "\x1b[3;4H");

  term.clear_pending_output_for_test();
  term.set_cell_scale(2);
  term.move_cursor(3, 2);
  REQUIRE(term.pending_output_for_test() == "\x1b[5;7H");
}

TEST_CASE("A scaled grid is the terminal's own size divided by the scale", "[jot][scale]")
{
  Terminal term;
  RowProbeUI ui(&term);
  ui.resize(80, 24);
  REQUIRE(ui.get_width() == 80);
  REQUIRE(ui.get_height() == 24);

  ui.set_cell_scale(2);
  REQUIRE(ui.get_cell_scale() == 2);
  REQUIRE(ui.get_width() == 40);
  REQUIRE(ui.get_height() == 12);

  // The row the renderer writes says both things: the cursor lands on the block
  // the cell starts in, and the text is framed at the scale.
  ui.set_default_colors(7, 0);
  ui.clear();
  ui.draw_text(0, 0, "hi", 7, 0);
  term.clear_pending_output_for_test();
  ui.emit_full_row(0, ui.get_render_width());
  const std::string &scaled = term.pending_output_for_test();
  REQUIRE(scaled.rfind("\x1b[1;1H", 0) == 0);
  const std::vector<std::string> frames = frames_of(scaled, 2);
  REQUIRE_FALSE(frames.empty());
  REQUIRE(join(frames).find("hi") == 0);

  // Row 1 is the third terminal row: one grid row is two terminal rows tall.
  term.clear_pending_output_for_test();
  ui.emit_full_row(1, ui.get_render_width());
  REQUIRE(term.pending_output_for_test().rfind("\x1b[3;1H", 0) == 0);

  // Back to 1:1, the same row is written at its own coordinates and without the
  // protocol, which is the state a terminal without it has to keep working in.
  ui.set_cell_scale(1);
  REQUIRE(ui.get_width() == 80);
  term.clear_pending_output_for_test();
  ui.emit_full_row(1, ui.get_render_width());
  const std::string &plain = term.pending_output_for_test();
  REQUIRE(plain.rfind("\x1b[2;1H", 0) == 0);
  REQUIRE(plain.find("\x1b]66;") == std::string::npos);
}

#ifndef _WIN32
TEST_CASE("A mouse report names the grid cell under the pointer", "[jot][scale]")
{
  // The terminal reports the cell the pointer is in; at a scale above 1 that is
  // one cell of a block, so the grid cell the UI has to act on is further in.
  Terminal term;
  MouseEvent ev{};
  term.set_cell_scale(2);
  REQUIRE(term.parse_mouse_report_for_test("0;10;4M", ev));
  REQUIRE(ev.x == 4);
  REQUIRE(ev.y == 1);

  term.set_cell_scale(1);
  REQUIRE(term.parse_mouse_report_for_test("0;10;4M", ev));
  REQUIRE(ev.x == 9);
  REQUIRE(ev.y == 3);
}
#endif

TEST_CASE("A terminal without the protocol keeps the grid at 1:1", "[jot][scale]")
{
  // A test run has no terminal to ask -- and a piped or CI stdout is exactly the
  // case the probe has to answer no to -- so the zoom is refused and the grid
  // stays where the screen can actually be drawn: one character per cell.
  Editor &e = probe_editor();
  REQUIRE(e.tui_text_scale_for_test() == 1);
  e.set_tui_text_scale_for_test(3);
  REQUIRE(e.tui_text_scale_for_test() == 1);
}

TEST_CASE("A refused zoom leaves the setting alone and says why", "[jot][scale]")
{
  // The zoom keys persist through the same setter, so a refused request must not
  // reach the config: the next start would otherwise ask for a scale this
  // terminal still cannot draw. The message names what the terminal is missing,
  // because "nothing happened" is not something to debug from.
  Editor &e = probe_editor();
  e.set_tui_text_scale_for_test(2);
  REQUIRE(e.tui_text_scale_for_test() == 1);
  REQUIRE(e.config_int_for_test("tui_text_scale") == 1);
  REQUIRE(e.message_for_test().find("cannot scale") != std::string::npos);
}

TEST_CASE("Ctrl+wheel zooms while a plain wheel still scrolls", "[jot][scale]")
{
  // The chords reach the zoom through the real mouse dispatch -- button 64 is a
  // notch up, and the Ctrl bit is what makes it a zoom rather than a scroll.
  Editor &e = probe_editor();
  e.set_message_for_test("nothing yet");
  e.terminal_mouse_for_test(40, 12, 64, true, false);
  REQUIRE(e.message_for_test() == "nothing yet");

  e.terminal_mouse_for_test(40, 12, 64, true, true);
  REQUIRE(e.message_for_test().find("cannot scale") != std::string::npos);
  REQUIRE(e.tui_text_scale_for_test() == 1);
}
