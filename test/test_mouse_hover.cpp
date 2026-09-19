// Mouse motion must never scroll the viewport.
//
// Editor::handle_mouse ends with a caret reveal (ensure_cursor_visible) that is
// meant for presses/drags/releases, where the pointer actually places the caret.
// It ran for every event that reached the tail, so plain mouse motion scrolled
// the viewport back to the caret whenever the user had scrolled away from it.
// Ordinary motion only escaped that by accident: the LSP-hover block above
// returns early on motion, and both of its branches are gated on "Ctrl not held"
// -- so with Ctrl held (the Ctrl+hover goto-definition affordance) execution fell
// through and the view "teleported" to the cursor on every motion cell, in both
// the terminal and the GUI.
#include "editor.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_mouse_hover_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A buffer the probe can place arbitrary pointer positions on: the caller
  // names the lines and the columns are indexed from the code pane's first
  // cell (pane.x + 1 + the 7-cell line-number band). Each case gets its own
  // path -- load_file focuses an already-open buffer instead of re-reading it,
  // so a shared name would keep the previous case's lines.
  void load_lines(Editor &e, const std::vector<std::string> &lines, const std::string &name)
  {
    const std::string path = "/tmp/jot_mouse_probe_" + name + ".cpp";
    std::ofstream out(path);
    for (const auto &line : lines)
    {
      out << line << "\n";
    }
    out.close();
    e.load_file(path);
  }

  int code_col(Editor &e, int col)
  {
    return e.pane_for_test().x + 8 + col;
  }

  // The screen row that renders buffer line 0: found by clicking, since the
  // pane's chrome height (tab strip, border) is the renderer's business. The
  // tab strip is skipped -- a click there activates the tab and can re-home the
  // caret on line 0, which would look like a hit.
  int first_code_row(Editor &e)
  {
    // Park the caret elsewhere first: the editor starts at 0,0, so "the click
    // left the caret on line 0" proves nothing until it has moved.
    e.scroll_cursor_to_for_test(5, 0);
    for (int row = e.pane_for_test().y + 1; row < e.pane_for_test().y + 8; row++)
    {
      e.reset_mouse_clicks_for_test();
      e.mouse_event_for_test(code_col(e, 0), row, /*bstate=*/1);
      e.mouse_event_for_test(code_col(e, 0), row, /*bstate=*/2);
      if (e.buffer_for_test().cursor.y == 0)
      {
        return row;
      }
      e.scroll_cursor_to_for_test(5, 0);
    }
    return e.pane_for_test().y + 1;
  }

  // A file long enough that the caret can sit far outside the viewport.
  std::string load_long_file(Editor &e)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_mouse_hover_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    for (int i = 0; i < 400; i++)
    {
      out << "int value_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
    return path;
  }

  // Caret at `caret_line` but the viewport parked `rows_away` lines above it, so
  // the caret is off-screen and any caret reveal would visibly move the view.
  void scroll_away_from_caret(Editor &e, int caret_line, int top_line)
  {
    e.scroll_cursor_to_for_test(caret_line, 0);
    e.buffer_for_test().scroll_offset = top_line;
    e.render_for_test();
  }

  // A point inside the pane's code area (past the 9-cell gutter, below the tab
  // strip), on the pane the editor is actually drawing.
  int code_x(Editor &e)
  {
    return e.pane_for_test().x + 20;
  }
  int code_y_upper(Editor &e)
  {
    return e.pane_for_test().y + 4;
  }
} // namespace

TEST_CASE("Ctrl + mouse motion never scrolls the viewport", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  // Caret at line 200, viewport showing lines 150..176: the caret is off-screen.
  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int scroll_before = e.buffer_for_test().scroll_offset;
  const int caret_line_before = e.buffer_for_test().cursor.y;
  REQUIRE(scroll_before == 150);
  REQUIRE(caret_line_before == 200);

  // Ctrl+motion: the goto-definition underline follows the pointer, and the view
  // stays exactly where the user left it.
  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/32, /*ctrl=*/true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
  REQUIRE(e.buffer_for_test().cursor.y == caret_line_before);
  // The affordance itself still works (this is what Ctrl+motion is for).
  REQUIRE(e.ctrl_hover_active_for_test());

  // A second motion cell must not drift either (the bug re-armed on every cell).
  e.mouse_event_for_test(code_x(e), code_y_upper(e) + 1, /*bstate=*/32, /*ctrl=*/true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
}

TEST_CASE("Plain mouse motion never scrolls the viewport", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int scroll_before = e.buffer_for_test().scroll_offset;
  REQUIRE(scroll_before == 150);

  // No Ctrl: this path is the one that used to escape the reveal by accident
  // (the LSP-hover early returns). Pin it so it stays true on purpose.
  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/32, /*ctrl=*/false);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset == scroll_before);
  REQUIRE_FALSE(e.ctrl_hover_active_for_test());
}

// Ctrl+click on a member/qualified chain has to answer about the element the
// pointer is on, not about whatever cell the pointer's column happens to name:
// `counter.stored`, `p->field` and `outer::thing` are several symbols in one run
// of characters, and clangd resolves exactly one of them per position. A
// separator selects the member on its right (`.`, `->`) or the qualifier on its
// left (`::`), which is what clangd answers for the separator itself, so the
// caret lands on the first character of the symbol that was asked about.
TEST_CASE("Ctrl+click resolves the element of a chain under the pointer", "[jot]")
{
  Editor &e = probe_editor();
  struct Scene
  {
    const char *line;
    int x;      // the pointer's column
    int caret;  // the column the click must leave the caret on
    const char *why;
  };
  const Scene scenes[] = {
      {"  return counter.stored;", 9, 9, "the receiver's first character"},
      {"  return counter.stored;", 15, 9, "the receiver's last character"},
      {"  return counter.stored;", 16, 17, "the dot asks about the member"},
      {"  return counter.stored;", 22, 17, "the member's last character"},
      {"  return s.size();", 9, 9, "a one-character receiver"},
      {"  return s.size();", 10, 11, "its dot"},
      {"  return s.size();", 14, 11, "its member"},
      {"  return a.b.c;", 9, 9, "the head of a three-element chain"},
      {"  return a.b.c;", 10, 11, "the first dot picks the middle name"},
      {"  return a.b.c;", 11, 11, "the middle name"},
      {"  return a.b.c;", 12, 13, "the second dot picks the tail"},
      {"  return a.b.c;", 13, 13, "the tail"},
      {"  return p->field;", 9, 9, "the pointer variable, not the arrow"},
      {"  return p->field;", 10, 12, "the `-` of `->` asks about the member"},
      {"  return p->field;", 11, 12, "and so does the `>`"},
      {"  return p->field;", 12, 12, "the member behind the arrow"},
      {"  return this->field;", 12, 9, "the last character of `this` itself"},
      {"  return this->field;", 13, 15, "`this->` resolves to the member too"},
      {"  return this->field;", 14, 15, "the `>` of the arrow"},
      {"  return outer::thing();", 9, 9, "the qualifier"},
      {"  return outer::thing();", 13, 9, "its last character"},
      {"  return outer::thing();", 14, 9, "a `::` colon names the qualifier"},
      {"  return outer::thing();", 15, 9, "the second colon as well"},
      {"  return outer::thing();", 16, 16, "the qualified name itself"},
      {"  return ns::sub::deep;", 9, 9, "a two-step qualifier"},
      {"  return ns::sub::deep;", 12, 9, "the first `::`"},
      {"  return ns::sub::deep;", 13, 13, "the middle name"},
      {"  return ns::sub::deep;", 16, 13, "the second `::`"},
      {"  return ns::sub::deep;", 21, 18, "the innermost name"},
  };

  std::vector<std::string> lines;
  for (const auto &scene : scenes)
  {
    lines.emplace_back(scene.line);
  }
  load_lines(e, lines, "chain");
  e.apply_resize_for_test(120, 40);
  e.buffer_for_test().scroll_offset = 0;
  e.render_for_test();
  const int row0 = first_code_row(e);

  for (int i = 0; i < (int)(sizeof(scenes) / sizeof(scenes[0])); i++)
  {
    const int row = row0 + i;
    e.reset_mouse_clicks_for_test();
    e.mouse_event_for_test(code_col(e, scenes[i].x), row, /*bstate=*/1, /*ctrl=*/true);
    e.mouse_event_for_test(code_col(e, scenes[i].x), row, /*bstate=*/2, /*ctrl=*/true);
    INFO(scenes[i].line << " column " << scenes[i].x << " (" << scenes[i].why << ")");
    REQUIRE(e.buffer_for_test().cursor.y == i);
    REQUIRE(e.buffer_for_test().cursor.x == scenes[i].caret);
  }
}

// The hover underline is the affordance for the click, so it covers exactly the
// element the click would ask about: hovering `counter` underlines `counter`,
// hovering the dot or the member underlines `stored`. One underline across the
// whole run would promise a jump that half of it does not make.
TEST_CASE("Ctrl+hover underlines the element, not the whole chain", "[jot]")
{
  Editor &e = probe_editor();
  struct Scene
  {
    const char *line;
    int x;
    int start;
    int end;
    const char *why;
  };
  const Scene scenes[] = {
      {"  return counter.stored;", 9, 9, 16, "the receiver's first character"},
      {"  return counter.stored;", 12, 9, 16, "inside the receiver"},
      {"  return counter.stored;", 15, 9, 16, "the receiver's last character"},
      {"  return counter.stored;", 16, 17, 23, "the dot underlines the member"},
      {"  return counter.stored;", 20, 17, 23, "inside the member"},
      {"  return s.size();", 9, 9, 10, "a one-character receiver"},
      {"  return s.size();", 10, 11, 15, "its dot underlines `size`"},
      {"  return a.b.c;", 10, 11, 12, "the first dot underlines the middle name"},
      {"  return a.b.c;", 12, 13, 14, "the second dot underlines the tail"},
      {"  return p->field;", 9, 9, 10, "the pointer's own name"},
      {"  return p->field;", 10, 12, 17, "the `-` of `->` underlines the member"},
      {"  return p->field;", 11, 12, 17, "and so does the `>`"},
      {"  return outer::thing();", 13, 9, 14, "the qualifier's last character"},
      {"  return outer::thing();", 14, 9, 14, "a `::` colon underlines the qualifier"},
      {"  return outer::thing();", 16, 16, 21, "the qualified name itself"},
      {"  return ns::sub::deep;", 12, 9, 11, "the first `::`"},
      {"  return ns::sub::deep;", 13, 13, 16, "the middle name"},
      {"  return ns::sub::deep;", 16, 13, 16, "the second `::`"},
  };

  std::vector<std::string> lines;
  for (const auto &scene : scenes)
  {
    lines.emplace_back(scene.line);
  }
  load_lines(e, lines, "underline");
  e.apply_resize_for_test(120, 40);
  e.buffer_for_test().scroll_offset = 0;
  e.render_for_test();
  const int row0 = first_code_row(e);

  for (int i = 0; i < (int)(sizeof(scenes) / sizeof(scenes[0])); i++)
  {
    e.mouse_event_for_test(code_col(e, scenes[i].x), row0 + i, /*bstate=*/32,
                           /*ctrl=*/true);
    int start = -1;
    int end = -1;
    INFO(scenes[i].line << " column " << scenes[i].x << " (" << scenes[i].why << ")");
    REQUIRE(e.ctrl_hover_span_for_test(start, end));
    REQUIRE(start == scenes[i].start);
    REQUIRE(end == scenes[i].end);
  }
}

TEST_CASE("A press still reveals the caret", "[jot]")
{
  Editor &e = probe_editor();
  load_long_file(e);
  e.apply_resize_for_test(100, 30);

  scroll_away_from_caret(e, /*caret_line=*/200, /*top_line=*/150);
  const int mouse_line =
      e.buffer_for_test().scroll_offset + (code_y_upper(e) - e.pane_for_test().y - 1);
  REQUIRE(mouse_line < 200); // the click lands well above the old caret

  e.mouse_event_for_test(code_x(e), code_y_upper(e), /*bstate=*/1); // press
  e.render_for_test();

  // The pointer placed the caret on the clicked row, and that is the behaviour
  // the caret reveal exists for -- the fix must not disable it.
  REQUIRE(e.buffer_for_test().cursor.y == mouse_line);
}
