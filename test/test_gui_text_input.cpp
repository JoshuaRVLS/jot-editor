// GUI text input: the composing (IME) text of src/ui/gui/gui_input.cpp and
// gui_cursor.cpp, and the window title of gui_setup.cpp.
//
// A composition never reaches the buffer -- the input method holds it until it
// commits -- so the frontend has to carry it itself and paint it at the caret.
// These pin the two halves of that: the state SDL's SDL_TEXTEDITING updates, and
// the cells that state turns into. Both are SDL-free, so they are testable
// without a window; the event plumbing around them is what the probes on a real
// display cover.
#include "ui/gui/gui.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A composition is replaced wholesale on every input method update", "[jot]")
{
  std::string preedit;
  // SDL re-sends the whole composition on every step, never the delta: the
  // second update replaces the first rather than appending to it.
  REQUIRE(apply_text_editing(preedit, "k"));
  REQUIRE(preedit == "k");
  REQUIRE(apply_text_editing(preedit, "き"));
  REQUIRE(preedit == "き");
  REQUIRE(apply_text_editing(preedit, "きょ"));
  REQUIRE(preedit == "きょ");
  // The same composition again is not a change: an input method can re-send it
  // while the user does nothing, and it must not ask for a frame every time.
  REQUIRE_FALSE(apply_text_editing(preedit, "きょ"));
  // Committing ends it: SDL sends the empty composition around the commit.
  REQUIRE(apply_text_editing(preedit, ""));
  REQUIRE(preedit.empty());
  // ...and ending an empty composition changes nothing.
  REQUIRE_FALSE(apply_text_editing(preedit, ""));
  // A null payload is the empty composition, not a crash.
  REQUIRE_FALSE(apply_text_editing(preedit, nullptr));
}

TEST_CASE("Composing text becomes one cell per codepoint, underlined", "[jot]")
{
  std::vector<UICell> cells;
  // Multi-byte codepoints are one cell each, in their own glyphs: a cell holds
  // the encoded character, not a byte of it.
  preedit_cells("きょう", 3, 4, cells);
  REQUIRE(cells.size() == 3);
  REQUIRE(cells[0].ch == "き");
  REQUIRE(cells[1].ch == "ょ");
  REQUIRE(cells[2].ch == "う");
  // Composing text is underlined so it reads as not committed yet, and carries
  // the colors the painter was given.
  for (const UICell &cell : cells)
  {
    REQUIRE(cell.underline == 1);
    REQUIRE(cell.fg == 3);
    REQUIRE(cell.bg == 4);
  }

  // ASCII compositions (romaji, dead keys) are cells the same way.
  preedit_cells("ni", 7, 0, cells);
  REQUIRE(cells.size() == 2);
  REQUIRE(cells[0].ch == "n");

  // An empty composition paints nothing, and clears whatever was there.
  preedit_cells("", 7, 0, cells);
  REQUIRE(cells.empty());

  // Invalid UTF-8 ends the run at the last character that decoded: a cell holds
  // a whole character, so a half-written sequence has no glyph to paint.
  preedit_cells("a\xff\xfe", 7, 0, cells);
  REQUIRE(cells.size() == 1);
  REQUIRE(cells[0].ch == "a");
}

TEST_CASE("The window title names the active file, then the workspace", "[jot]")
{
  // A file: its own name, with the unsaved marker a title has room for.
  REQUIRE(window_title_for("/home/me/proj/src/main.cpp", false, "/home/me/proj")
          == "main.cpp - jot");
  REQUIRE(window_title_for("/home/me/proj/src/main.cpp", true, "/home/me/proj")
          == "main.cpp * - jot");
  // Only the leaf: the directories above it are not what the title bar shows.
  REQUIRE(window_title_for("main.cpp", false, "") == "main.cpp - jot");
  REQUIRE(window_title_for("C:\\proj\\src\\main.cpp", false, "") == "main.cpp - jot");

  // No file open: the workspace folder is what the window is on.
  REQUIRE(window_title_for("", false, "/home/me/proj") == "proj - jot");
  // A workspace written with a trailing separator still names itself.
  REQUIRE(window_title_for("", false, "/home/me/proj/") == "proj - jot");
  // No file and no workspace: no label to invent.
  REQUIRE(window_title_for("", false, "") == "jot");
  REQUIRE(window_title_for("", true, ".") == "jot");
  // A file wins over the workspace it lives in.
  REQUIRE(window_title_for("notes.md", false, "/home/me/proj") == "notes.md - jot");
}
