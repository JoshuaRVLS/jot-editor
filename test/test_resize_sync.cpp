// Grid resize plumbing: the UI's one-shot full repaint, pane re-fitting, and
// the sidebar's width-driven auto-hide.
//
// The terminal and GUI resize handlers both funnel through Editor::apply_resize
// (jot/app/resize.cpp), so exercising it here covers what a live window resize
// does without needing a real terminal or display.
#include "editor.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_resize_cfg_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  std::string scratch_workspace()
  {
    static std::string root;
    if (root.empty())
    {
      char tmpl[] = "/tmp/jot_resize_ws_XXXXXX";
      REQUIRE(mkdtemp(tmpl) != nullptr);
      root = tmpl;
      fs::create_directory(root + "/src");
      std::ofstream(root + "/src/main.cpp") << "int main() { return 0; }\n";
      std::ofstream(root + "/README.md") << "# hi\n";
    }
    return root;
  }

  // Writes `content` to a fresh .cpp file (so the highlighter tokenizes strings
  // and comments) and loads it, then returns its path.
  std::string load_source(Editor &e, const std::string &content)
  {
    static int counter = 0;
    const std::string path =
        "/tmp/jot_bracket_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".cpp";
    std::ofstream(path) << content;
    e.load_file(path);
    // Earlier cases in this file leave a viewport behind (a scrolled workspace);
    // the bracket tests measure geometry, so start from a clean one.
    e.buffer_for_test().scroll_x = 0;
    e.buffer_for_test().scroll_offset = 0;
    return path;
  }

  // Rainbow palette indices for the active theme, cheapest to compare.
  std::vector<int> bracket_palette(Editor &e)
  {
    const Theme &theme = e.theme_for_test();
    return {theme.fg_bracket1,
            theme.fg_bracket2,
            theme.fg_bracket3,
            theme.fg_bracket4,
            theme.fg_bracket5,
            theme.fg_bracket6};
  }

  bool is_bracket_glyph(const std::string &ch)
  {
    return ch == "{" || ch == "}" || ch == "(" || ch == ")" || ch == "[" || ch == "]";
  }

  // Rainbow palette index of a cell, or -1 when its color is not in the palette.
  // Matching on color alone is not enough: the palette entries are ordinary ANSI
  // slots, so a keyword or a status chip can legitimately share one. Every
  // helper below therefore filters by the bracket glyph as well.
  int bracket_color_index(Editor &e, const UICell *cell)
  {
    if (!cell || !is_bracket_glyph(cell->ch))
    {
      return -1;
    }
    const std::vector<int> palette = bracket_palette(e);
    for (size_t i = 0; i < palette.size(); i++)
    {
      if (cell->fg == palette[i])
      {
        return (int)i;
      }
    }
    return -1;
  }

  // Fingerprint of the bracket cells from row `first_row` on, in row/column
  // order: "row:col:palette". Rainbow bracket colors are meant to be a pure
  // function of file position, so this must not change when only the window
  // does. Rows are skipped when a test needs to ignore brackets whose
  // *visibility* depends on the width (a line wider than the pane reveals more
  // of its braces as the pane grows).
  std::vector<std::string> bracket_color_fingerprint(Editor &e, int first_row)
  {
    std::vector<std::string> out;
    UI *ui = e.ui_for_test();
    if (!ui)
    {
      return out;
    }
    // Pane rows only: the status line shares palette slots with its chips.
    const int last_row = std::max(first_row, ui->get_height() - 2);
    for (int y = first_row; y < last_row; y++)
    {
      for (int x = 0; x < ui->get_width(); x++)
      {
        const int index = bracket_color_index(e, ui->cell_at(x, y));
        if (index >= 0)
        {
          out.push_back(std::to_string(y) + ":" + std::to_string(x) + ":" + std::to_string(index));
        }
      }
    }
    return out;
  }

  // Palette indices of the bracket cells of one grid row, left to right.
  std::vector<int> bracket_colors_in_row(Editor &e, int row)
  {
    std::vector<int> out;
    UI *ui = e.ui_for_test();
    if (!ui)
    {
      return out;
    }
    for (int x = 0; x < ui->get_width(); x++)
    {
      const int index = bracket_color_index(e, ui->cell_at(x, row));
      if (index >= 0)
      {
        out.push_back(index);
      }
    }
    return out;
  }

  // A pristine editor for the cases that assert on exact screen content: the
  // shared probe editor carries whatever panes and buffers earlier cases opened,
  // and those cells would show up in a whole-grid scan.
  std::unique_ptr<Editor> fresh_editor()
  {
    probe_editor(); // seeds the scratch config home on first use
    auto editor = std::make_unique<Editor>();
    editor->apply_resize_for_test(120, 30);
    return editor;
  }

  // The bracket tests compare screen geometry, so the explorer must be out of
  // the way: earlier cases in this file open workspaces, which show it.
  void hide_sidebar(Editor &e)
  {
    if (e.sidebar_visible_for_test())
    {
      e.toggle_sidebar_for_test();
    }
  }

  std::string join(const std::vector<std::string> &parts)
  {
    std::string out;
    for (const std::string &part : parts)
    {
      out += part;
      out += " ";
    }
    return out;
  }
} // namespace

TEST_CASE("A resize schedules exactly one full repaint", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  // A real size change must force a full paint: newly exposed cells are
  // default-constructed in both the live and the retained grid, so the cell
  // diff would skip them and leave whatever the terminal restored there.
  ui->resize(ui->get_width() + 7, ui->get_height() + 3);
  REQUIRE(ui->full_repaint_pending());
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE_FALSE(ui->full_repaint_pending()); // consumed by the frame

  // Re-applying the same size (the editor does this at startup) needs no full
  // paint.
  ui->resize(ui->get_width(), ui->get_height());
  REQUIRE_FALSE(ui->full_repaint_pending());
}

TEST_CASE("A resize re-fits the panes to the new grid", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  e.apply_resize_for_test(120, 40);
  const SplitPane wide = e.pane_for_test();
  REQUIRE(wide.w > 0);
  REQUIRE(wide.x + wide.w <= ui->get_render_width());

  // Growing the window must grow the pane area, not leave it at the old size.
  e.apply_resize_for_test(160, 50);
  const SplitPane wider = e.pane_for_test();
  REQUIRE(wider.w > wide.w);
  REQUIRE(wider.x + wider.w <= ui->get_render_width());

  // And shrinking must bring it back in bounds rather than overflow.
  e.apply_resize_for_test(60, 20);
  const SplitPane narrow = e.pane_for_test();
  REQUIRE(narrow.w > 0);
  REQUIRE(narrow.x + narrow.w <= ui->get_render_width());
}

TEST_CASE("Explorer clicks work below the terminal's row count", "[jot]")
{
  Editor &e = probe_editor();

  // A workspace tall enough to fill a 40-row explorer, with names that sort in
  // creation order so a screen row maps predictably onto a file.
  char tmpl[] = "/tmp/jot_click_ws_XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string root = tmpl;
  for (int i = 0; i < 40; i++)
  {
    char name[32];
    std::snprintf(name, sizeof(name), "/f%02d.txt", i);
    std::ofstream(root + name) << "file " << i << "\n";
  }
  e.host().io.open_workspace(root);

  // The condition that made this a GUI-only bug: the grid is 40 rows while the
  // terminal still reports its 24-row constructor default (under --gui the
  // terminal is never initialised). A hit test reading the terminal's height
  // therefore stops at row 22 in a window of any size.
  e.apply_resize_for_test(120, 40);
  e.render_for_test();
  REQUIRE(e.ui_height_for_test() == 40);
  REQUIRE(e.terminal_height_for_test() == 24);
  REQUIRE(e.sidebar_visible_for_test());

  // Explorer rows start at y = 1 (row 0 is the panel's top border), so the
  // filename index is y - 1.
  const auto click_row = [&](int y)
  {
    e.mouse_event_for_test(5, y, /*bstate=*/1); // press
    e.render_for_test();
    return fs::path(e.buffer_for_test().filepath).filename().string();
  };

  // Control: a row above the old cutoff must keep working.
  REQUIRE(click_row(1) == "f00.txt");

  // The regression: row 22 is the first row the old terminal-derived gate
  // rejected, so this click used to be dropped entirely.
  REQUIRE(click_row(22) == "f21.txt");

  // The last row of the explorer that is actually painted (the panel's bottom
  // border sits one row lower). Its index in the flat list is 34.
  REQUIRE(click_row(35) == "f34.txt");

  // The row below that is the panel's bottom border / the status line: it must
  // not open anything.
  const std::string before = e.buffer_for_test().filepath;
  e.mouse_event_for_test(5, 38, /*bstate=*/1);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().filepath == before);
}

TEST_CASE("Bracket colors survive a resize when a line is wider than the pane", "[jot]")
{
  const std::unique_ptr<Editor> owned = fresh_editor();
  Editor &e = *owned;
  hide_sidebar(e);

  // A line wider than the pane with an unbalanced brace beyond the narrow
  // window, then normal nested code. The row walk stops at the visible edge, so
  // the depth it hands to the next row used to depend on the pane width: at 200
  // columns the brace at column ~115 was counted, at 100 it was not, and the
  // following lines were colored from different depths.
  std::string line0 = "void f() {";
  line0 += std::string(105, ' ');
  line0 += "{";
  line0 += std::string(105, ' ');
  line0 += "{";
  const std::string content = line0 + "\n{\n  {\n    { x(); }\n  }\n}\n";
  load_source(e, content);

  e.apply_resize_for_test(100, 30);
  e.render_for_test();
  const std::string narrow = join(bracket_color_fingerprint(e, /*first_row=*/2));
  REQUIRE_FALSE(narrow.empty());

  e.apply_resize_for_test(200, 30);
  e.render_for_test();
  const std::string wide = join(bracket_color_fingerprint(e, /*first_row=*/2));

  INFO("narrow: " << narrow);
  INFO("wide:   " << wide);
  REQUIRE(narrow == wide);
}

TEST_CASE("Bracket colors match the file-position depth", "[jot]")
{
  const std::unique_ptr<Editor> owned = fresh_editor();
  Editor &e = *owned;
  hide_sidebar(e);

  // A line longer than the tokenizer's full-highlight limit keeps only a window
  // of token info, so which of its brackets counted as code depended on that
  // window (and on which caller filled the cache): the carried depth disagreed
  // with the absolute per-line depth, so the same bracket was painted one color
  // while the line was on screen and another once it scrolled off. The painted
  // color must always be rainbow{prefix depth at the line}.
  std::string long_line = "const char *s = \"";
  long_line += std::string(584, 'a');
  long_line += "{{{{";
  long_line += std::string(6000, 'b');
  long_line += "\";";
  REQUIRE(long_line.size() > 4096);

  std::string content = long_line + "\n";
  for (int i = 0; i < 19; i++)
  {
    content += "int filler" + std::to_string(i) + ";\n";
  }
  content += "{\n}\n"; // line 20: the first line with brackets the pane shows
  load_source(e, content);

  e.apply_resize_for_test(100, 30);
  e.scroll_cursor_to_for_test(/*line=*/20, /*col=*/0);
  e.render_for_test();

  // The long line's own braces sit at byte ~600, far outside a 100-column pane,
  // so the brackets the pane shows belong to lines 20 ("{") and 21 ("}").
  // Line 20 is displayed at this grid row (row 0 is the tab strip, row 1 the
  // first visible line), derived from the real scroll state rather than assumed.
  // Row 1 is the first visible line, so line N sits at this grid row. The file
  // is short enough to fit, so nothing is scrolled: the long line is on screen,
  // but its braces (byte ~600) are outside a 100-column pane, which is precisely
  // the case where the carried depth used to be taken from the truncated walk
  // instead of the file position.
  const int scroll_offset = e.buffer_for_test().scroll_offset;
  const int line20_row = 1 + (20 - scroll_offset);
  const std::vector<int> opening = bracket_colors_in_row(e, line20_row);
  const std::vector<int> closing = bracket_colors_in_row(e, line20_row + 1);
  REQUIRE(opening.size() == 1);
  REQUIRE(closing.size() == 1);

  const int depth = e.bracket_depth_for_test(20);
  INFO("depth at line 20: " << depth);
  // A matched pair shares its level's color: the close is painted after the
  // depth steps back down, so both ends of the pair read the same.
  REQUIRE(opening[0] == depth % 6);
  REQUIRE(closing[0] == depth % 6);

  e.apply_resize_for_test(120, 30);
}

TEST_CASE("The sidebar comes back when the window fits again", "[jot]")
{
  Editor &e = probe_editor();
  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);

  // A workspace shows the explorer.
  e.host().io.open_workspace(scratch_workspace());
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE(e.sidebar_visible_for_test());

  // Too narrow: the explorer is dropped so the code keeps a usable width.
  e.apply_resize_for_test(20, 20);
  e.render_for_test();
  REQUIRE_FALSE(e.sidebar_visible_for_test());
  REQUIRE(e.sidebar_auto_hidden_for_test());

  // Room again: it returns. Shrinking once must not hide it for the session.
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE(e.sidebar_visible_for_test());
  REQUIRE_FALSE(e.sidebar_auto_hidden_for_test());

  // An explicit toggle still wins: hiding the sidebar by hand and then
  // resizing must not resurrect it.
  e.toggle_sidebar_for_test(); // hide
  REQUIRE_FALSE(e.sidebar_visible_for_test());
  e.apply_resize_for_test(20, 20);
  e.render_for_test();
  e.apply_resize_for_test(140, 40);
  e.render_for_test();
  REQUIRE_FALSE(e.sidebar_visible_for_test());

  // Restore for the rest of the suite.
  e.toggle_sidebar_for_test();
  e.render_for_test();
}
