// The completion popup's ghost text: the selected item's remaining insert text
// previewed inline at the caret while typing.
//
// Two rules decide whether a preview is worth showing, and both come from what
// the preview *is* -- an inline completion of the word being typed:
//
//   * only an item whose insert text really continues what is typed has
//     something to preview (a row that matched by fuzzy subsequence, or one left
//     over from the previous keystroke's response, would render a whole
//     identifier as if the user were typing it), and
//   * it is only painted where the caret owns the rest of the row (inside a call
//     the editor auto-closed, `printf(|)`, the text under the caret is the `)`
//     itself, and the preview would land after a character that accepting would
//     insert before).
//
// The cases drive a real Editor: the prefix comes out of the buffer's own token
// under the caret, through the same arm/filter path a real response runs, and
// the render cases read the cells the painter produced.
#include "core/editor.h"
#include "ui/ui.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_completion_ghost_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  LSPCompletionItem item(const std::string &label,
                         const std::string &insert_text,
                         int insert_text_format = 1)
  {
    LSPCompletionItem it;
    it.label = label;
    it.insert_text = insert_text;
    it.filter_text = label;
    it.insert_text_format = insert_text_format;
    it.kind = 3;
    return it;
  }

  // A file whose content is `lines`, opened and left with the caret at
  // `line`:`col` -- the typed prefix is whatever the token before the caret
  // holds, exactly as a real keystroke would leave it.
  std::string open_with_caret(Editor &e, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_completion_ghost_" + std::to_string(::getpid())
                             + "_" + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
    return path;
  }

  // Every italic cell on screen, per row: the ghost is drawn italic
  // (like the inlay hints), and nothing else in a test's screen is.
  std::vector<std::string> italic_rows(UI *ui)
  {
    std::vector<std::string> rows;
    for (int y = 0; y < ui->get_height(); y++)
    {
      std::string row;
      for (int x = 0; x < ui->get_width(); x++)
      {
        const UICell *cell = ui->cell_at(x, y);
        if (cell && cell->italic && !cell->ch.empty())
        {
          row += cell->ch;
        }
      }
      if (!row.empty())
      {
        rows.push_back(row);
      }
    }
    return rows;
  }
} // namespace

TEST_CASE("Completion ghost: the preview is the insert text minus what is typed", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  pri\n}\n", 1, 5);

  // The snippet's argument placeholder is dropped on accept, so the preview is
  // the empty call too -- an inline preview of text that would not be written is
  // the one thing it must not be.
  REQUIRE(e.seed_lsp_completion_for_test(
      {item("printf", "printf(${1:const char *format, ...})", 2)}));
  REQUIRE(e.lsp_completion_prefix_for_test() == "pri");
  REQUIRE(e.lsp_completion_ghost_for_test() == "ntf()");
}

TEST_CASE("Completion ghost: what accepting would insert is what is previewed", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  wid\n}\n", 1, 5);

  // The server asked us to filter by `Widget`, and the typed `wid` leads into
  // that -- so the row is listed -- but what accepting actually inserts is the
  // qualified name, which the caret is not in the middle of typing. The preview
  // asks its own question of the text it would type.
  REQUIRE(e.seed_lsp_completion_for_test({item("Widget", "ns::Widget")}));
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_ghost_for_test().empty());

  // The same item with nothing qualified about it previews its remainder.
  REQUIRE(e.seed_lsp_completion_for_test({item("Widget", "Widget")}));
  REQUIRE(e.lsp_completion_ghost_for_test() == "get");
}

TEST_CASE("Completion ghost: nothing typed and nothing left to type preview nothing", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);

  // Triggered on `.`: the member list is up before a letter is typed, and the
  // preview would be the selected member's whole name.
  open_with_caret(e, "int main() {\n  w.\n}\n", 1, 4);
  REQUIRE(e.seed_lsp_completion_for_test({item("width", "width"), item("area", "area()")}));
  REQUIRE(e.lsp_completion_prefix_for_test().empty());
  REQUIRE(e.lsp_completion_ghost_for_test().empty());

  // Typed in full: the item is the prefix, so there is no remainder to show.
  open_with_caret(e, "int main() {\n  width\n}\n", 1, 7);
  REQUIRE(e.seed_lsp_completion_for_test({item("width", "width")}));
  REQUIRE(e.lsp_completion_ghost_for_test().empty());
}

TEST_CASE("Completion ghost: a snippet previews its first line", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  i\n}\n", 1, 3);

  // A body spanning lines cannot be shown inline, and the newline's control byte
  // would paint as a fallback glyph on the row: only the first line is previewed.
  REQUIRE(e.seed_lsp_completion_for_test({item("if", "if (${1:cond})\n{\n\t$0\n}", 2)}));
  REQUIRE(e.lsp_completion_prefix_for_test() == "i");
  REQUIRE(e.lsp_completion_ghost_for_test() == "f (cond)");
}

TEST_CASE("Completion ghost: the prefix match ignores case", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  PRI\n}\n", 1, 5);

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(format)")}));
  REQUIRE(e.lsp_completion_ghost_for_test() == "ntf()");
}

TEST_CASE("Completion ghost: nothing is painted inside an auto-closed call", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // The caret sits between the typed `p` and the `)` the editor auto-closed the
  // call with: the bracket owns the rest of the row.
  open_with_caret(e, "int main() {\n  printf(p)\n}\n", 1, 10);

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(${1:format})", 2)}));
  // There is a preview to show -- the suppression is the paint rule, not the
  // match rule -- so this case fails if either half regresses.
  REQUIRE(e.lsp_completion_prefix_for_test() == "p");
  REQUIRE(e.lsp_completion_ghost_for_test() == "rintf()");

  // Past the typing pause, so the clock is not what is being asserted here.
  e.age_lsp_completion_typing_for_test(1000);
  e.request_redraw_for_test();
  e.render_for_test();
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.empty());

  // The row itself is untouched: the bracket is still the bracket, and nothing
  // previews between them.
  std::string screen;
  UI *ui = e.ui_for_test();
  for (int y = 0; y < ui->get_height(); y++)
  {
    for (int x = 0; x < ui->get_width(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      if (cell)
      {
        screen += cell->ch;
      }
    }
    screen += "\n";
  }
  REQUIRE(screen.find("printf(p)") != std::string::npos);
  REQUIRE(screen.find("printf(p)rintf") == std::string::npos);
}

TEST_CASE("Completion ghost: it is painted where the caret owns the row", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  pri\n}\n", 1, 5);

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(format)")}));
  REQUIRE(e.lsp_completion_ghost_for_test() == "ntf()");

  e.age_lsp_completion_typing_for_test(1000);
  e.request_redraw_for_test();
  e.render_for_test();
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.size() == 1);
  REQUIRE(painted.front() == "ntf()");
}

TEST_CASE("Completion ghost: only the pane that owns the popup previews", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  pri\n}\n", 1, 5);

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(format)")}));
  REQUIRE(e.lsp_completion_ghost_for_test() == "ntf()");

  // The split shows the same file with its own view, and its caret sits on the
  // same row. The popup -- and so the preview -- belongs to the focused pane
  // (render_lsp_completion anchors there), so the second pane must not draw the
  // same word at its own caret: two carets, one preview.
  e.age_lsp_completion_typing_for_test(1000);
  e.split_pane_for_test(true);
  e.request_redraw_for_test();
  e.render_for_test();
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.size() == 1);
  REQUIRE(painted.front() == "ntf()");
}

TEST_CASE("Completion ghost: the preview waits for the typing to pause", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  pri\n}\n", 1, 5);
  // Long enough that no part of this case can outlast the wait on its own: the
  // clock is moved with the hook below, never by waiting.
  e.config_set_for_test("lsp_completion_ghost_delay_ms", "2000");

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(format)")}));
  REQUIRE(e.lsp_completion_ghost_for_test() == "ntf()");
  REQUIRE(e.lsp_completion_preview_withheld_for_test());

  // Typing: the word just changed, so the frame paints the row without it.
  e.request_redraw_for_test();
  e.render_for_test();
  const std::vector<std::string> early = italic_rows(e.ui_for_test());
  REQUIRE(early.empty());

  // Still waiting, a frame later: nothing to reveal yet, and the frame loop now
  // knows the wait is what is holding the preview back.
  e.age_lsp_completion_typing_for_test(1000);
  REQUIRE(e.lsp_completion_preview_withheld_for_test());
  e.render_frame_for_test();
  REQUIRE(italic_rows(e.ui_for_test()).empty());

  // The pause: the frame loop is what reveals it -- nothing is typed from here on
  // and nothing else requests a paint, so the preview can only appear if the
  // frame that ends the wait was asked for (main_loop's preview_due_soon), and
  // the one after that drew it.
  e.age_lsp_completion_typing_for_test(1100);
  REQUIRE_FALSE(e.lsp_completion_preview_withheld_for_test());
  e.render_frame_for_test();
  e.render_frame_for_test();
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.size() == 1);
  REQUIRE(painted.front() == "ntf()");
}

TEST_CASE("Completion ghost: the delay can be switched off", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  pri\n}\n", 1, 5);
  e.config_set_for_test("lsp_completion_ghost_delay_ms", "0");

  REQUIRE(e.seed_lsp_completion_for_test({item("printf", "printf(format)")}));
  REQUIRE_FALSE(e.lsp_completion_preview_withheld_for_test());

  e.request_redraw_for_test();
  e.render_for_test();
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.size() == 1);
  REQUIRE(painted.front() == "ntf()");
}

TEST_CASE("Completion list: only the words the typed word leads into are listed", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);

  // `wdt` is a subsequence of both, which is how a fuzzy matcher would find
  // them -- and is exactly why neither belongs in the list: accepting either
  // would replace the word with an identifier it has nothing to do with.
  open_with_caret(e, "int main() {\n  wdt\n}\n", 1, 5);
  REQUIRE_FALSE(e.seed_lsp_completion_for_test({item("Widget", "Widget"),
                                                item("WidgetFactory", "WidgetFactory(w)")}));
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_ghost_for_test().empty());

  e.age_lsp_completion_typing_for_test(1000);
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(italic_rows(e.ui_for_test()).empty());

  // The same item is listed -- and previewed -- the moment the word leads into
  // it. `Widget` is 6 chars, so the remainder is `get`.
  open_with_caret(e, "int main() {\n  Wid\n}\n", 1, 5);
  REQUIRE(e.seed_lsp_completion_for_test({item("Widget", "Widget")}));
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_ghost_for_test() == "get");
}

TEST_CASE("Completion list: rows the next keystroke stops leading into drop out", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // One word holding both prefixes: the caret at 4 has typed `wi` (which `widen`
  // leads into), the caret at 7 has typed `width` (which it does not).
  open_with_caret(e, "int main() {\n  width\n}\n", 1, 4);

  REQUIRE(e.seed_lsp_completion_for_test({item("widen", "widen")}));
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_ghost_for_test() == "den");

  // The next keystroke lands the caret past the word the response answered: the
  // rows are still in hand (no response has arrived for the new prefix), and
  // none of them leads into it, so the popup goes away instead of listing them
  // -- and with it the preview, which is the flicker this rule is about.
  e.scroll_cursor_to_for_test(1, 7);
  e.age_lsp_completion_typing_for_test(1000);
  // What a keystroke does before the frame: re-filter against the word the caret
  // now sits in (the insert path's refresh_lsp_completion_filter), so the frame
  // that follows paints the new answer rather than the previous one.
  e.refresh_lsp_completion_for_test();
  e.request_redraw_for_test();
  e.render_for_test();
  REQUIRE(e.lsp_completion_prefix_for_test() == "width");
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_ghost_for_test().empty());
  const std::vector<std::string> painted = italic_rows(e.ui_for_test());
  REQUIRE(painted.empty());
}
