// Keeping a markup tag and its partner spelled the same while one of them is
// edited (src/features/html.cpp, HtmlFeatures::find_tag_pair, driven from
// Editor::sync_markup_tag_name in src/text/edit.cpp).
//
// The pair is found structurally: the cursor has to sit inside a tag *name*, and
// the scan then walks the document counting tags until the nest closes. That is
// what lets the pair survive the name changing into a different one (`div` ->
// `span`) rather than only growing and shrinking at the end, and it is also the
// part worth pinning: a scan that matched on the name would pass the typing case
// and silently do nothing for a rename, and one that ignored nesting would
// rename the inner tag's partner instead of the outer one.
//
// The model cases are pure. The editor cases drive real keystrokes through the
// frontend's own decode path (raw_key_for_test), so what is asserted is the
// gesture: type into an opening tag and the closing tag follows; backspace and it
// shrinks; do it in a file that is not markup, or anywhere that is not a tag
// name, and nothing moves.
#include "editor.h"
#include "html.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  using HtmlFeatures::find_tag_pair;
  using HtmlFeatures::MarkupTagPair;

  const MarkupTagPair &
  pair_at(const std::vector<std::string> &lines, int line, int col, MarkupTagPair &out)
  {
    REQUIRE(find_tag_pair(lines, line, col, out));
    return out;
  }

  void seed_config_home()
  {
    char home[] = "/tmp/jot_markup_tags_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // A file whose content is `text`, opened with the caret at `line`:`col`.
  void
  open_with_caret(Editor &e, const std::string &suffix, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_markup_tags_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + suffix;
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
  }

  // Types `text` one keystroke at a time, exactly as a terminal would deliver
  // it. jot is modeless -- a printable key is an insert -- so there is no mode
  // to enter first.
  void type(Editor &e, const std::string &text)
  {
    for (char c : text)
    {
      e.raw_key_for_test((unsigned char)c);
    }
  }

  void press_backspace(Editor &e, int times)
  {
    for (int i = 0; i < times; i++)
    {
      e.raw_key_for_test(127);
    }
  }

  const std::string &line_of(Editor &e, int index)
  {
    return e.buffer_for_test().line(index);
  }
} // namespace

TEST_CASE("Tag pair: the same line, both directions", "[jot][html]")
{
  MarkupTagPair pair;
  const std::vector<std::string> lines = {"<div></div>"};

  // Cursor inside the opening name: the partner is the closing name.
  pair_at(lines, 0, 2, pair);
  REQUIRE_FALSE(pair.closing);
  REQUIRE(pair.name == "div");
  REQUIRE(pair.edit_line == 0);
  REQUIRE(pair.edit_col == 1);
  REQUIRE(pair.partner == "div");
  REQUIRE(pair.partner_line == 0);
  REQUIRE(pair.partner_col == 7);
}

TEST_CASE("Tag pair: closing tag resolves the opening one", "[jot][html]")
{
  MarkupTagPair pair;
  const std::vector<std::string> lines = {"<div></div>"};
  pair_at(lines, 0, 8, pair);
  REQUIRE(pair.closing);
  REQUIRE(pair.name == "div");
  REQUIRE(pair.edit_col == 7);
  REQUIRE(pair.partner == "div");
  REQUIRE(pair.partner_line == 0);
  REQUIRE(pair.partner_col == 1);
}

TEST_CASE("Tag pair: nesting picks the outer pair", "[jot][html]")
{
  MarkupTagPair pair;

  SECTION("editing the outer opening tag")
  {
    const std::vector<std::string> lines = {"<div>", "  <span></span>", "</div>"};
    pair_at(lines, 0, 2, pair);
    REQUIRE(pair.partner_line == 2);
    REQUIRE(pair.partner_col == 2);
  }

  SECTION("editing the inner opening tag")
  {
    const std::vector<std::string> lines = {"<div>", "  <span></span>", "</div>"};
    pair_at(lines, 1, 4, pair);
    REQUIRE(pair.partner_line == 1);
    REQUIRE(pair.partner_col == 10);
  }

  SECTION("editing the outer closing tag")
  {
    const std::vector<std::string> lines = {"<div>", "  <span></span>", "</div>"};
    pair_at(lines, 2, 3, pair);
    REQUIRE(pair.closing);
    REQUIRE(pair.partner_line == 0);
    REQUIRE(pair.partner_col == 1);
  }
}

TEST_CASE("Tag pair: only a tag name counts", "[jot][html]")
{
  MarkupTagPair pair;

  // Attributes and the body are not the name.
  REQUIRE_FALSE(find_tag_pair({"<div class=\"box\"></div>"}, 0, 10, pair));
  REQUIRE_FALSE(find_tag_pair({"<div>body</div>"}, 0, 7, pair));
  // A comparison is not a tag either, and neither is a name that never closes.
  REQUIRE_FALSE(find_tag_pair({"if (a < b)"}, 0, 7, pair));
  REQUIRE_FALSE(find_tag_pair({"<div"}, 0, 2, pair));
  // An element with no partner has no pair; one the author wrote a close tag for
  // does, void or not, because what is being kept in step is the text.
  REQUIRE_FALSE(find_tag_pair({"<br>"}, 0, 2, pair));
  pair_at({"<br></br>"}, 0, 2, pair);
  REQUIRE(pair.partner == "br");
  REQUIRE_FALSE(find_tag_pair({"<img src=\"a\" />"}, 0, 2, pair));
}

TEST_CASE("Tag pair: a rename keeps the same partner", "[jot][html]")
{
  // The point of matching structurally: the names are not related at all here,
  // and the pair still has to be found.
  MarkupTagPair pair;
  const std::vector<std::string> lines = {"<span>x</span>"};
  pair_at(lines, 0, 2, pair);
  REQUIRE(pair.name == "span");
  REQUIRE(pair.partner == "span");

  // A closing tag a name has been deleted out of is still the partner, and comes
  // back with a zero length: the editor writes the name into it.
  const std::vector<std::string> emptied = {"<s></>"};
  pair_at(emptied, 0, 2, pair);
  REQUIRE(pair.name == "s");
  REQUIRE(pair.partner.empty());
  REQUIRE(pair.partner_col == 5);
}

TEST_CASE("Tag rename: typing into an opening tag carries the closing tag", "[jot][html]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // The end of the opening name, so the typed char extends it.
  open_with_caret(e, ".html", "<div></div>\n", 0, 4);

  type(e, "x");
  REQUIRE(line_of(e, 0) == "<divx></divx>");
  REQUIRE(e.buffer_for_test().cursor.x == 5);
}

TEST_CASE("Tag rename: backspacing a name shrinks the partner", "[jot][html]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // A char from the *middle* of the name: the pair has to be found
  // structurally, because `dv` and `div` no longer agree by prefix.
  open_with_caret(e, ".html", "<div></div>\n", 0, 3);

  press_backspace(e, 1);
  REQUIRE(line_of(e, 0) == "<dv></dv>");
  REQUIRE(e.buffer_for_test().cursor.x == 2);
}

TEST_CASE("Tag rename: retyping a name from empty keeps the pair", "[jot][html]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, ".html", "<div></div>\n", 0, 4);

  // Delete the name, then type a different one: the closing tag follows all the
  // way through the empty state rather than being left as `</>`.
  press_backspace(e, 3);
  type(e, "span");
  REQUIRE(line_of(e, 0) == "<span></span>");
}

TEST_CASE("Tag rename: the closing tag renames the opening one", "[jot][html]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // The end of the closing name, so the typed char extends it.
  open_with_caret(e, ".html", "<div></div>\n", 0, 10);

  type(e, "x");
  REQUIRE(line_of(e, 0) == "<divx></divx>");
  // The partner is behind the caret, so the caret follows the text that moved
  // under it rather than landing a column short.
  REQUIRE(e.buffer_for_test().cursor.x == 12);
}

TEST_CASE("Tag rename: nothing moves outside a tag name or outside markup", "[jot][html]")
{
  seed_config_home();

  SECTION("typing in the body")
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, ".html", "<div></div>\n", 0, 5);
    type(e, "zz");
    REQUIRE(line_of(e, 0) == "<div>zz</div>");
  }

  SECTION("typing in an attribute name")
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, ".html", "<div id></div>\n", 0, 6);
    type(e, "x");
    REQUIRE(line_of(e, 0) == "<div ixd></div>");
  }

  SECTION("a file that is not markup")
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, ".txt", "<div></div>\n", 0, 4);
    type(e, "x");
    REQUIRE(line_of(e, 0) == "<divx></div>");
  }
}
