// The save / rename / quit prompts (src/render/prompts.cpp,
// Editor::render_prompt_modal).
//
// These three are modal panels, and they used to be rendered as if they were
// screens of their own: the frame painted the panel and nothing else, so the
// buffer the prompt was asking about vanished behind it (only the cleared grid
// was left) while whatever the previous frame had painted stayed on screen.
// What is pinned here is the modal shape every other panel in this editor
// already uses: the frame behind the prompt is painted normally, the whole of
// it is dimmed, and the panel is the one thing that is not. Plus the panel's own
// sentence, which the Lua kit renders -- the native painter and the Lua handler
// are two renderings of the same prompt and have to ask the same question.
#include "core/editor.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_prompt_modal_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A buffer with recognisable text in it, so a case can tell the pane's
  // content from the cleared grid the prompt used to leave behind. Longer than
  // the pane on purpose: the wheel cases need a viewport with somewhere to go.
  void load_buffer(Editor &e, const char *suffix)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_prompt_modal_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + "_" + suffix;
    std::ofstream out(path);
    for (int i = 0; i < 80; i++)
    {
      out << "int buffer_line_" << i << " = " << i << ";\n";
    }
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(120, 30);
  }

  std::string row_text(Editor &e, int y)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  }

  std::string whole_screen(Editor &e)
  {
    std::string out;
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      out += row_text(e, y);
      out += "\n";
    }
    return out;
  }

  // The row a prompt's text is on, or -1. Found by text rather than by
  // arithmetic, so a case does not re-implement the panel's centring.
  int text_row(Editor &e, const std::string &needle)
  {
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      if (row_text(e, y).find(needle) != std::string::npos)
      {
        return y;
      }
    }
    return -1;
  }

  // Every painted cell of one row, as (painted, undimmed): a case can assert
  // both that the row had something to dim and that all of it was dimmed.
  void row_tally(Editor &e, int y, int &painted, int &undimmed)
  {
    UI *ui = e.ui_for_test();
    painted = 0;
    undimmed = 0;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (!cell || cell->ch == " ")
      {
        continue;
      }
      painted++;
      if (!cell->dim)
      {
        undimmed++;
      }
    }
  }

  // The chrome rows every prompt has to dim, whatever layout its panel uses: the
  // tab strip, the first row of the buffer, and the status line.
  void require_chrome_dimmed(Editor &e, bool &buffer_row_found)
  {
    int painted = 0;
    int undimmed = 0;
    buffer_row_found = false;
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      if (row_text(e, y).find("int buffer_line_0") != std::string::npos)
      {
        buffer_row_found = true;
        row_tally(e, y, painted, undimmed);
        REQUIRE(painted > 0);
        REQUIRE(undimmed == 0);
      }
    }
    row_tally(e, 0, painted, undimmed);
    REQUIRE(painted > 0);
    REQUIRE(undimmed == 0);
    row_tally(e, e.ui_height_for_test() - 1, painted, undimmed);
    REQUIRE(painted > 0);
    REQUIRE(undimmed == 0);
  }

  // The panel's text row carries the buffer behind it on both sides, so "the
  // panel is the live part" has to be read inside the panel's own borders: the
  // cells outside them belong to the scrimmed frame.
  void require_panel_text_undimmed(Editor &e, int y)
  {
    UI *ui = e.ui_for_test();
    int left = -1;
    int right = -1;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (cell && cell->ch == "\u2502")
      {
        if (left < 0)
        {
          left = x;
        }
        right = x;
      }
    }
    REQUIRE(left >= 0);
    REQUIRE(right > left);
    bool saw_text = false;
    for (int x = left + 1; x < right; x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (cell && cell->ch != " ")
      {
        saw_text = true;
        REQUIRE_FALSE(cell->dim);
      }
    }
    REQUIRE(saw_text);
  }

  void require_nothing_dimmed(Editor &e)
  {
    UI *ui = e.ui_for_test();
    for (int y = 0; y < e.ui_height_for_test(); y++)
    {
      for (int x = 0; x < e.ui_width_for_test(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        const bool dimmed = cell != nullptr && cell->dim;
        REQUIRE_FALSE(dimmed);
      }
    }
  }
} // namespace

TEST_CASE("Quit prompt: the panel asks with its keys, over a dimmed editor", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "a.cpp");
  e.open_quit_prompt_for_test();
  e.render_for_test();

  const int panel_y = text_row(e, "Quit anyway?");
  REQUIRE(panel_y >= 0);
  // The question, and the answer keys with it: the panel carries no title, so
  // "quit anyway?" with nothing after it leaves the user guessing. The Lua kit
  // paints this row, and it used to drop the hint the native painter writes.
  REQUIRE(row_text(e, panel_y).find("(y/n)") != std::string::npos);

  // The buffer the prompt is about is still on the grid behind it. This is the
  // half of the bug the scrim alone would not have fixed: the pane used to be
  // blank, so the prompt asked about a file you could no longer see.
  REQUIRE(whole_screen(e).find("int buffer_line_0") != std::string::npos);

  // The panel's own cells are the live ones.
  require_panel_text_undimmed(e, panel_y);

  // And the chrome around it -- the buffer row itself, the tab strip, the
  // status line -- is under the scrim.
  bool buffer_row_found = false;
  require_chrome_dimmed(e, buffer_row_found);
  REQUIRE(buffer_row_found);
}

TEST_CASE("Quit prompt: Y confirms after Ctrl+Q from the explorer", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "focused_y.cpp");
  e.insert_string_for_test("dirty");
  e.toggle_sidebar_for_test();
  REQUIRE(e.focus_state_for_test() == FOCUS_SIDEBAR);

  // This is the real Ctrl+Q route: it opens the modal through the editor
  // dispatcher, after which Y must not become the explorer's yank key.
  e.terminal_key_for_test('q', true);
  REQUIRE(e.quit_prompt_visible_for_test());
  e.terminal_key_for_test('y');

  REQUIRE_FALSE(e.quit_prompt_visible_for_test());
}

TEST_CASE("Quit prompt: N cancels after Ctrl+Q from the explorer", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "focused_n.cpp");
  e.insert_string_for_test("dirty");
  e.toggle_sidebar_for_test();
  REQUIRE(e.focus_state_for_test() == FOCUS_SIDEBAR);

  e.terminal_key_for_test('q', true);
  REQUIRE(e.quit_prompt_visible_for_test());
  e.terminal_key_for_test('n');

  REQUIRE_FALSE(e.quit_prompt_visible_for_test());
  REQUIRE(e.focus_state_for_test() == FOCUS_SIDEBAR);
}

TEST_CASE("Quit prompt: dismissing it takes the scrim with it", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "b.cpp");
  e.open_quit_prompt_for_test();
  REQUIRE(e.quit_prompt_visible_for_test());
  e.render_for_test();
  REQUIRE(text_row(e, "Quit anyway?") >= 0);

  // Back to the editor. A scrim left behind on dismiss would keep dimming every
  // future frame, so this is asserted over the whole grid rather than at one
  // sample cell.
  e.dismiss_quit_prompt_for_test();
  REQUIRE_FALSE(e.quit_prompt_visible_for_test());
  e.render_for_test();
  REQUIRE(text_row(e, "Quit anyway?") < 0);
  require_nothing_dimmed(e);
}

TEST_CASE("Quit prompt: the panel owns the pointer while it is up", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "d.cpp");
  e.render_for_test();

  // First without the prompt: a click in the pane moves the caret and a notch of
  // wheel scrolls the view. That is what makes the assertions below about
  // *swallowing* the pointer rather than about events that never landed.
  e.mouse_event_for_test(60, 12, /*bstate=*/1);
  e.render_for_test();
  const int clicked_line = e.buffer_for_test().cursor.y;
  REQUIRE(clicked_line != 0);
  e.wheel_event_for_test(60, 12, false, true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().scroll_offset != 0);

  // With the panel up the same two events change nothing: the dimmed frame
  // behind it is not clickable, and a wheel over the panel has nothing to
  // scroll (it used to scroll the buffer from under it).
  e.open_quit_prompt_for_test();
  e.render_for_test();
  const int held_line = e.buffer_for_test().cursor.y;
  const int held_scroll = e.buffer_for_test().scroll_offset;
  e.mouse_event_for_test(60, 20, /*bstate=*/1);
  e.wheel_event_for_test(60, 20, false, true);
  e.render_for_test();
  REQUIRE(e.buffer_for_test().cursor.y == held_line);
  REQUIRE(e.buffer_for_test().scroll_offset == held_scroll);
}

TEST_CASE("Save prompt: the same modal shape as the quit prompt", "[jot][prompt]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  load_buffer(e, "c.cpp");
  e.open_save_prompt_for_test();
  e.render_for_test();

  // The save box is the single-field one: "Save As" is the panel title (Lua) or
  // its own row (native), and the field row carries the name.
  REQUIRE(text_row(e, "Save As") >= 0);
  REQUIRE(whole_screen(e).find("Filename:") != std::string::npos);
  REQUIRE(whole_screen(e).find("int buffer_line_0") != std::string::npos);

  bool buffer_row_found = false;
  require_chrome_dimmed(e, buffer_row_found);
  REQUIRE(buffer_row_found);

  // The field's own cells are live, not dimmed -- the prompt is the one thing
  // you can type into.
  const int field_y = text_row(e, "Filename:");
  REQUIRE(field_y >= 0);
  require_panel_text_undimmed(e, field_y);
}
