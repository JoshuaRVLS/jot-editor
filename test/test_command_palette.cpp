// The command palette (Ctrl+P / `:`) as a Helix-style command menu: results in
// a multi-column grid filled column by column, an explanation box describing
// the selected command (its sentence, aliases and argument shape) above the
// grid, and the prompt on the statusline row.
//
// The geometry is built once per frame by Editor::build_command_palette_layout()
// and read by the renderer, the cursor placement, the Lua UI kit and the mouse
// hit-test, so a click and a painted cell cannot disagree
// (src/render/palette.cpp, src/input/palette/input.cpp,
// src/input/palette/suggestions.cpp).
#include "editor.h"
#include "ui/ui.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_command_palette_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  std::string write_file(const std::string &name, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_command_palette_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + name;
    std::ofstream out(path);
    out << text;
    out.close();
    return path;
  }

  // A 140x40 editor with a file open and the palette toggled open with Ctrl+P.
  // With `native`, the bundled UI kit's leases are dropped before the first
  // paint (no Lua float -- the kit's status line would cover the prompt row),
  // so the native renderers own the frame.
  std::unique_ptr<Editor> open_palette(int width = 140, int height = 40, bool native = false)
  {
    seed_config_home();
    auto editor = std::make_unique<Editor>();
    editor->set_home_menu_visible(false);
    editor->load_file(write_file("a.txt", "alpha\nbeta\ngamma\n"));
    editor->apply_resize_for_test(width, height);
    if (native)
    {
      editor->clear_lua_ui_handler_for_test("command_palette");
      editor->clear_lua_ui_handler_for_test("status_line");
    }
    editor->render_for_test();
    editor->editor_input_for_test('p', true);
    REQUIRE(editor->command_palette_open_for_test());
    return editor;
  }

  int index_of_label(Editor &e, const std::string &label)
  {
    for (int i = 0; i < e.command_palette_result_count_for_test(); i++)
    {
      if (e.command_palette_label_for_test(i) == label)
      {
        return i;
      }
    }
    return -1;
  }

  void type(Editor &e, const std::string &text)
  {
    for (char c : text)
    {
      e.editor_input_for_test(c);
    }
  }

  // One painted screen row as text (the cell grid, not the terminal stream).
  std::string row_text(UI *ui, int y)
  {
    std::string out;
    for (int x = 0; x < ui->get_render_width(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      out += cell ? cell->ch : std::string(" ");
    }
    return out;
  }
} // namespace

TEST_CASE("The palette paints a column-major grid with an explanation box", "[jot][palette]")
{
  const auto owned = open_palette();
  Editor &e = *owned;
  const auto layout = e.command_palette_layout_for_test();

  REQUIRE(layout.result_count == e.command_palette_result_count_for_test());
  REQUIRE(layout.result_count > 20);
  REQUIRE(layout.prompt_y == 39); // the statusline slot
  REQUIRE(layout.grid_col_w >= 12);
  REQUIRE(layout.grid_cols >= 2);
  REQUIRE(layout.grid_cols * layout.grid_col_w <= layout.screen_w);
  // The grid is as tall as a full column needs, windowed to the screen.
  REQUIRE(layout.rows_per_col == (layout.result_count + layout.grid_cols - 1) / layout.grid_cols);
  REQUIRE(layout.grid_rows >= 1);
  REQUIRE(layout.grid_rows <= layout.rows_per_col);
  // It sits directly on the prompt, spanning the width.
  REQUIRE(layout.grid_x == 0);
  REQUIRE(layout.w == layout.screen_w);
  REQUIRE(layout.grid_y + layout.grid_rows == layout.prompt_y);

  // Every row carries an explanation -- the curated sentence or the one-line
  // detail -- so the box always answers "what does this do".
  for (int i = 0; i < layout.result_count; i++)
  {
    REQUIRE_FALSE(e.command_palette_help_for_test(i).empty());
  }

  // The box for the selected row stands directly on the grid's top edge, sized
  // to its content (border rows excluded from `help_lines`).
  REQUIRE(layout.help_for == 0);
  REQUIRE(layout.help_x == 0);
  REQUIRE(layout.help_w >= 12);
  REQUIRE(layout.help_w <= layout.screen_w);
  REQUIRE(layout.help_y + layout.help_h == layout.grid_y);
  REQUIRE((int)layout.help_lines.size() == layout.help_h - 2);
  REQUIRE_FALSE(layout.help_lines.empty());
  REQUIRE(layout.help_lines[0].rfind(e.command_palette_help_for_test(0).substr(0, 8), 0) == 0);

  // The curated help for a top command carries its sentence, its aliases and
  // its argument shape; the box prints each on its own line.
  const int w_index = index_of_label(e, "w");
  REQUIRE(w_index >= 0);
  REQUIRE(e.command_palette_help_for_test(w_index).rfind("Write the current buffer", 0) == 0);
  REQUIRE(e.command_palette_aliases_for_test(w_index) == "write, save");
  REQUIRE(e.command_palette_args_for_test(w_index) == "[path]");

  // An alias resolves to the entry that owns it: `write` explains itself like
  // `w` instead of falling back to the bare detail line.
  const int write_index = index_of_label(e, "write");
  REQUIRE(write_index >= 0);
  REQUIRE(e.command_palette_help_for_test(write_index) == e.command_palette_help_for_test(w_index));
  REQUIRE(e.command_palette_aliases_for_test(write_index) == "write, save");
}

TEST_CASE("Typing narrows the palette and the box explains the selection", "[jot][palette]")
{
  {
    const auto owned = open_palette();
    Editor &e = *owned;
    type(e, "wq");
    REQUIRE(e.command_palette_label_for_test(0) == "wq");

    // The curated help for `wq`: the sentence wraps to one line, the aliases
    // get their own, and it takes no arguments (no Args line).
    const auto layout = e.command_palette_layout_for_test();
    REQUIRE(layout.help_for == 0);
    REQUIRE(layout.help_lines.size() == 2);
    REQUIRE(layout.help_lines[0] == "Write the current buffer and quit.");
    REQUIRE(layout.help_lines[1] == "Aliases: x, xit");
    REQUIRE(layout.help_h == 4); // two lines + the border rows
  }

  // A command with no curated entry still explains itself: the box falls back
  // to the one-line detail rather than going blank.
  const auto owned = open_palette();
  Editor &e = *owned;
  type(e, "upper");
  REQUIRE(e.command_palette_label_for_test(0) == "upper");
  REQUIRE(e.command_palette_aliases_for_test(0).empty());
  REQUIRE(e.command_palette_help_for_test(0) == "Uppercase selection or word");
  const auto layout = e.command_palette_layout_for_test();
  REQUIRE(layout.help_lines.size() == 1);
  REQUIRE(layout.help_lines[0] == "Uppercase selection or word");
  REQUIRE(layout.help_h == 3); // one line + the border rows
}

TEST_CASE("Arrow keys walk the grid by row and column", "[jot][palette]")
{
  const auto owned = open_palette();
  Editor &e = *owned;
  const auto first = e.command_palette_layout_for_test();
  const int rows = first.rows_per_col;
  REQUIRE(rows >= 2);
  REQUIRE(first.grid_cols >= 2);
  REQUIRE(e.command_palette_selected_for_test() == 0);

  e.editor_input_for_test(1009); // Down
  REQUIRE(e.command_palette_selected_for_test() == 1);
  e.editor_input_for_test(1010); // Right: the same row in the next column
  REQUIRE(e.command_palette_selected_for_test() == rows + 1);
  e.editor_input_for_test(1011); // Left
  REQUIRE(e.command_palette_selected_for_test() == 1);
  e.editor_input_for_test(1008); // Up
  REQUIRE(e.command_palette_selected_for_test() == 0);
  e.editor_input_for_test(1008); // Up at the top edge: nothing above to reach
  REQUIRE(e.command_palette_selected_for_test() == 0);

  // PageDown is a screenful of rows inside the same column; PageUp returns.
  const int page = first.grid_rows;
  e.editor_input_for_test(1016);
  REQUIRE(e.command_palette_selected_for_test() == std::min(page, rows - 1));
  e.editor_input_for_test(1015);
  REQUIRE(e.command_palette_selected_for_test() == 0);

  // End reaches the last command; the window follows the selection instead of
  // letting it leave the grid.
  e.editor_input_for_test(1013);
  const int count = e.command_palette_result_count_for_test();
  REQUIRE(e.command_palette_selected_for_test() == count - 1);
  const auto end_layout = e.command_palette_layout_for_test();
  const int end_row = (count - 1) % end_layout.rows_per_col;
  REQUIRE(end_layout.grid_first_row <= end_row);
  REQUIRE(end_row < end_layout.grid_first_row + end_layout.grid_rows);
  REQUIRE(end_layout.grid_y + end_layout.grid_rows == end_layout.prompt_y);

  e.editor_input_for_test(1012); // Home
  REQUIRE(e.command_palette_selected_for_test() == 0);
  REQUIRE(e.command_palette_layout_for_test().grid_first_row == 0);

  // A short screen windows the grid: End scrolls the follower so the last row
  // is the one on screen, and the prompt keeps its row.
  e.apply_resize_for_test(40, 14);
  e.render_for_test();
  e.editor_input_for_test(1013);
  const auto short_layout = e.command_palette_layout_for_test();
  REQUIRE(short_layout.grid_rows < short_layout.rows_per_col);
  REQUIRE(e.command_palette_selected_for_test() == count - 1);
  REQUIRE(short_layout.grid_first_row == short_layout.rows_per_col - short_layout.grid_rows);
  const int short_row = (count - 1) % short_layout.rows_per_col;
  REQUIRE(short_row >= short_layout.grid_first_row);
  REQUIRE(short_row < short_layout.grid_first_row + short_layout.grid_rows);
  REQUIRE(short_layout.grid_y + short_layout.grid_rows == short_layout.prompt_y);

  // Home brings the window back to the top of every column.
  e.editor_input_for_test(1012);
  REQUIRE(e.command_palette_selected_for_test() == 0);
  REQUIRE(e.command_palette_layout_for_test().grid_first_row == 0);
}

TEST_CASE("Hover selects the cell under the pointer and a click runs it", "[jot][palette]")
{
  const auto owned = open_palette();
  Editor &e = *owned;
  const auto layout = e.command_palette_layout_for_test();
  REQUIRE(layout.grid_cols >= 2);
  REQUIRE(layout.help_h > 0);
  REQUIRE(layout.help_y > 0);
  REQUIRE(layout.rows_per_col > 3);

  // The first cell of the second column is results[rows_per_col]: the grid is
  // filled column by column, and the hit-test reads the same mapping.
  const int col1_x = layout.grid_x + layout.grid_col_w + 1;
  REQUIRE(e.command_palette_mouse_for_test(col1_x, layout.grid_y, false));
  REQUIRE(e.command_palette_selected_for_test() == layout.rows_per_col + layout.grid_first_row);

  // The wheel walks the same column three rows at a time.
  REQUIRE(e.command_palette_mouse_for_test(col1_x, layout.grid_y, false, false, true));
  REQUIRE(e.command_palette_selected_for_test() == layout.rows_per_col + 3);
  REQUIRE(e.command_palette_mouse_for_test(col1_x, layout.grid_y, false, true, false));
  REQUIRE(e.command_palette_selected_for_test() == layout.rows_per_col);

  // The prompt row and the explanation box are consumed without moving the
  // selection; above the box the pointer belongs to the editor again. The box
  // moves with the selected row, so re-read the geometry after the hover.
  const auto hovered = e.command_palette_layout_for_test();
  REQUIRE(hovered.help_h > 0);
  REQUIRE(hovered.help_y > 0);
  const int before = e.command_palette_selected_for_test();
  REQUIRE(e.command_palette_mouse_for_test(1, hovered.prompt_y, false));
  REQUIRE(e.command_palette_mouse_for_test(1, hovered.help_y, false));
  REQUIRE(e.command_palette_selected_for_test() == before);
  REQUIRE_FALSE(e.command_palette_mouse_for_test(1, hovered.help_y - 1, false));

  // A click runs the clicked command (the same path as Tab + Enter): narrow to
  // `datetime` first so the cell is unambiguous, then click it.
  type(e, "datetime");
  const auto narrow = e.command_palette_layout_for_test();
  REQUIRE(e.command_palette_label_for_test(0) == "datetime");
  const std::string line_before = e.buffer_for_test().line(0);
  REQUIRE(e.command_palette_mouse_for_test(narrow.grid_x + 1, narrow.grid_y, true));
  REQUIRE_FALSE(e.command_palette_open_for_test());
  REQUIRE(e.buffer_for_test().line(0) != line_before);
  REQUIRE(e.buffer_for_test().line(0).size() > line_before.size());
}

TEST_CASE("An empty result set keeps the prompt and clears the box", "[jot][palette]")
{
  const auto owned = open_palette();
  Editor &e = *owned;
  type(e, "zzzzzz");
  REQUIRE(e.command_palette_result_count_for_test() == 0);
  REQUIRE(e.command_palette_query_for_test() == "zzzzzz");

  const auto layout = e.command_palette_layout_for_test();
  REQUIRE(layout.result_count == 0);
  REQUIRE(layout.grid_rows == 0);
  // One row of headroom (the "No matches" line) plus the prompt on the last
  // row: exactly the rows the caret is placed against.
  REQUIRE(layout.grid_y == layout.prompt_y - 1);
  REQUIRE(layout.y == layout.grid_y);
  REQUIRE(layout.h == 2);
  REQUIRE(layout.help_for == -1);
  REQUIRE(layout.help_h == 0);
  REQUIRE(layout.help_lines.empty());
}

TEST_CASE("The Lua kit owns the palette and the native painter matches it", "[jot][palette]")
{
  // The shipped path: the bundled Lua kit renders the surface as two floats,
  // the grid and the explanation box. A module error would fall back to the
  // native painter and say so on the status line.
  {
    const auto owned = open_palette();
    Editor &e = *owned;
    e.render_for_test();
    REQUIRE(e.message_for_test().find("Lua UI error") == std::string::npos);
    REQUIRE(e.lua_float_count_for_test("command_palette") == 2);

    // Closing the palette tears both floats down.
    e.editor_input_for_test(27); // Esc
    REQUIRE_FALSE(e.command_palette_open_for_test());
    e.render_for_test();
    REQUIRE(e.lua_float_count_for_test("command_palette") == 0);
  }

  // The fallback: with no kit handler, render/palette.cpp paints the same
  // geometry itself, cell for cell, because both read the same layout.
  const auto owned = open_palette(140, 40, true);
  Editor &e = *owned;
  e.render_for_test();
  REQUIRE(e.lua_float_count_for_test("command_palette") == 0);

  UI *ui = e.ui_for_test();
  REQUIRE(ui != nullptr);
  const auto layout = e.command_palette_layout_for_test();
  REQUIRE(layout.result_count > 0);
  REQUIRE(layout.grid_cols >= 2);

  // The selected cell wears the theme's selection pair, and its label starts
  // one cell into the cell block.
  const UICell *selected = ui->cell_at(layout.grid_x + 1, layout.grid_y);
  REQUIRE(selected != nullptr);
  REQUIRE(selected->fg == e.theme_for_test().fg_selection);
  REQUIRE(selected->bg == e.theme_for_test().bg_selection);
  const int first = layout.grid_first_row;
  REQUIRE(row_text(ui, layout.grid_y).rfind(" " + e.command_palette_label_for_test(first), 0) == 0);

  // Column-major: the second column starts at its own cell offset, with the
  // result one full column further down the list.
  const std::string second =
      " " + e.command_palette_label_for_test(layout.rows_per_col + layout.grid_first_row);
  REQUIRE(row_text(ui, layout.grid_y).rfind(second, layout.grid_col_w) == layout.grid_col_w);

  // An unselected cell keeps the plain command ink on the buffer background.
  const UICell *plain = ui->cell_at(layout.grid_x + layout.grid_col_w + 1, layout.grid_y);
  REQUIRE(plain != nullptr);
  REQUIRE(plain->fg == e.theme_for_test().fg_command);
  REQUIRE(plain->bg == e.theme_for_test().bg_default);

  // The explanation box: a border row standing on the grid's top edge, with
  // the selected row's sentence inside it.
  REQUIRE(layout.help_y + layout.help_h == layout.grid_y);
  REQUIRE(row_text(ui, layout.help_y).rfind("\u250c", 0) == 0);
  REQUIRE(row_text(ui, layout.help_y + 1).find(layout.help_lines[0]) != std::string::npos);

  // The prompt is the last row, with the ":" one cell in.
  REQUIRE(row_text(ui, layout.prompt_y).find(':') == 1);
}
