// Emmet abbreviations (src/features/emmet.cpp) and the editor entry point that
// feeds them to the snippet engine (src/editor/app/emmet.cpp).
//
// The expander cases are pure text in, snippet text out, so they can pin the
// shape of an expansion exactly -- the tree an operator builds, the numbering a
// repeat hands out, the unit a value picks up. The refusals matter as much as
// the expansions: this runs from Tab, so anything that does not parse has to
// return false and leave the keystroke to indentation, and a case that only
// checked the happy path would not notice a parser that accepted everything.
//
// The editor cases drive the real entry point (the same one the snippet keymap
// calls) against a loaded buffer, which is where the splice and the caret
// placement are decided.
#include "editor.h"
#include "features/emmet.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{
  std::string markup(const std::string &abbr, bool jsx = false)
  {
    Emmet::Expansion expansion;
    REQUIRE(Emmet::expand_markup(abbr, "  ", jsx, expansion));
    return expansion.snippet;
  }

  std::string css(const std::string &abbr)
  {
    Emmet::Expansion expansion;
    REQUIRE(Emmet::expand_css(abbr, "", expansion));
    return expansion.snippet;
  }

  bool markup_fails(const std::string &abbr)
  {
    Emmet::Expansion expansion;
    return !Emmet::expand_markup(abbr, "  ", false, expansion);
  }

  bool css_fails(const std::string &abbr)
  {
    Emmet::Expansion expansion;
    return !Emmet::expand_css(abbr, "", expansion);
  }

  void seed_config_home()
  {
    char home[] = "/tmp/jot_emmet_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  void open_with_caret(Editor &e, const std::string &suffix, const std::string &text, int line,
                       int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_emmet_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + suffix;
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
  }
} // namespace

TEST_CASE("Emmet markup: elements, ids and classes", "[jot][emmet]")
{
  REQUIRE(markup("div") == "<div>$0</div>");
  REQUIRE(markup("span") == "<span>$0</span>");
  REQUIRE(markup(".box") == "<div class=\"box\">$0</div>");
  REQUIRE(markup("#main") == "<div id=\"main\">$0</div>");
  REQUIRE(markup("div#main.box.big") == "<div id=\"main\" class=\"box big\">$0</div>");
  REQUIRE(markup("MyComponent") == "<MyComponent>$0</MyComponent>");
}

TEST_CASE("Emmet markup: nesting, siblings and climbing out", "[jot][emmet]")
{
  REQUIRE(markup("div>p") == "<div>\n  <p>$0</p>\n</div>");
  REQUIRE(markup("div>p+span") == "<div>\n  <p></p>\n  <span>$0</span>\n</div>");
  REQUIRE(markup("div>p^span") == "<div>\n  <p></p>\n</div>\n<span>$0</span>");
  REQUIRE(markup("ul>li>a") == "<ul>\n  <li>\n    <a href=\"\">$0</a>\n  </li>\n</ul>");
}

TEST_CASE("Emmet markup: multiplication multiplies the subtree", "[jot][emmet]")
{
  // `ul>li*3` is three items, and the caret lands in the last of them.
  REQUIRE(markup("ul>li*3")
          == "<ul>\n  <li></li>\n  <li></li>\n  <li>$0</li>\n</ul>");
  // `$` numbers the copies, and a child of a repeated element is copied with it.
  REQUIRE(markup("ul>li.item$*3")
          == "<ul>\n  <li class=\"item1\"></li>\n  <li class=\"item2\"></li>\n"
             "  <li class=\"item3\">$0</li>\n</ul>");
  REQUIRE(markup("ul>li*2>a")
          == "<ul>\n  <li>\n    <a href=\"\"></a>\n  </li>\n  <li>\n    <a href=\"\">$0</a>\n"
             "  </li>\n</ul>");
}

TEST_CASE("Emmet markup: attributes, text and the doctype", "[jot][emmet]")
{
  REQUIRE(markup("span[data-x=1]") == "<span data-x=\"1\">$0</span>");
  REQUIRE(markup("a[href=#]") == "<a href=\"#\">$0</a>");
  REQUIRE(markup("p{Hello}") == "<p>Hello</p>$0");
  REQUIRE(markup("div{Item $}*2") == "<div>Item 1</div>\n<div>Item 2</div>$0");
  REQUIRE(markup("!") == "<!DOCTYPE html>$0");
}

TEST_CASE("Emmet markup: the attributes an element implies", "[jot][emmet]")
{
  REQUIRE(markup("a") == "<a href=\"\">$0</a>");
  REQUIRE(markup("img") == "<img src=\"\" alt=\"\">$0");
  REQUIRE(markup("link") == "<link rel=\"stylesheet\" href=\"\">$0");
  REQUIRE(markup("input") == "<input type=\"text\">$0");
  // An author's own spelling wins over the implied one.
  REQUIRE(markup("a[href=/home]") == "<a href=\"/home\">$0</a>");
}

TEST_CASE("Emmet markup: JSX closes void elements itself", "[jot][emmet]")
{
  REQUIRE(markup("img", true) == "<img src=\"\" alt=\"\" />$0");
  REQUIRE(markup("br", true) == "<br />$0");
  REQUIRE(markup("br") == "<br>$0");
  REQUIRE(markup("div>img", true) == "<div>\n  <img src=\"\" alt=\"\" />\n</div>$0");
}

TEST_CASE("Emmet markup: what does not parse is refused", "[jot][emmet]")
{
  // A refusal is what hands the Tab back to indentation, so the shapes that are
  // not abbreviations have to be refused rather than guessed at.
  REQUIRE(markup_fails(""));
  REQUIRE(markup_fails(">"));
  REQUIRE(markup_fails("div>"));
  REQUIRE(markup_fails("div+"));
  REQUIRE(markup_fails("^div"));
  REQUIRE(markup_fails("div..x"));
  REQUIRE(markup_fails("div{unclosed"));
  REQUIRE(markup_fails("div[x"));
  REQUIRE(markup_fails("div!"));
  REQUIRE(markup_fails("3div"));
}

TEST_CASE("Emmet CSS: shorthand properties and values", "[jot][emmet]")
{
  REQUIRE(css("m10") == "margin: 10px;$0");
  REQUIRE(css("m10-20") == "margin: 10px 20px;$0");
  REQUIRE(css("m-10") == "margin: -10px;$0");
  REQUIRE(css("m0") == "margin: 0;$0");
  REQUIRE(css("p5p") == "padding: 5%;$0");
  REQUIRE(css("w100p") == "width: 100%;$0");
  REQUIRE(css("fz14") == "font-size: 14px;$0");
  REQUIRE(css("lh1.5") == "line-height: 1.5;$0");
  REQUIRE(css("z10") == "z-index: 10;$0");
  REQUIRE(css("op.5") == "opacity: .5;$0");
  REQUIRE(css("c#fff") == "color: #fff;$0");
  REQUIRE(css("bgc#1a1a1a") == "background-color: #1a1a1a;$0");
  REQUIRE(css("m10!") == "margin: 10px !important;$0");
  REQUIRE(css("m1e") == "margin: 1em;$0");
}

TEST_CASE("Emmet CSS: keyword values resolve, full names read", "[jot][emmet]")
{
  REQUIRE(css("d:f") == "display: flex;$0");
  REQUIRE(css("d:n") == "display: none;$0");
  REQUIRE(css("pos:a") == "position: absolute;$0");
  REQUIRE(css("ta:c") == "text-align: center;$0");
  REQUIRE(css("jc:sb") == "justify-content: space-between;$0");
  REQUIRE(css("cur:p") == "cursor: pointer;$0");
  REQUIRE(css("bxz:bb") == "box-sizing: border-box;$0");
  // A keyword the table does not know is still a keyword.
  REQUIRE(css("d:inline-table") == "display: inline-table;$0");
  // A bare property leaves the caret in the value.
  REQUIRE(css("margin") == "margin: $0;");
  REQUIRE(css("display") == "display: $0;");
}

TEST_CASE("Emmet CSS: a chain becomes one declaration per line", "[jot][emmet]")
{
  Emmet::Expansion expansion;
  REQUIRE(Emmet::expand_css("p5+m10+ta:c", "  ", expansion));
  REQUIRE(expansion.snippet == "padding: 5px;\n  margin: 10px;\n  text-align: center;$0");
}

TEST_CASE("Emmet CSS: what does not parse is refused", "[jot][emmet]")
{
  REQUIRE(css_fails(""));
  REQUIRE(css_fails("xyzzy"));
  REQUIRE(css_fails("foo10"));
  REQUIRE(css_fails("m>10"));
  REQUIRE(css_fails("m10+"));
  REQUIRE(css_fails("10"));
}

TEST_CASE("Emmet: the token stops at a boundary a Tab can mean", "[jot][emmet]")
{
  REQUIRE(Emmet::abbreviation_before("div.box", 7, false) == "div.box");
  REQUIRE(Emmet::abbreviation_before("  div.box", 9, false) == "div.box");
  REQUIRE(Emmet::abbreviation_before(">div", 4, false) == "div");
  REQUIRE(Emmet::abbreviation_before("m10", 3, true) == "m10");
  // Nothing to expand: a tag already started, a word after a name, or an empty
  // prefix.
  REQUIRE(Emmet::abbreviation_before("<div", 4, false).empty());
  REQUIRE(Emmet::abbreviation_before("</div>", 5, false).empty());
  REQUIRE(Emmet::abbreviation_before("foo.bar", 7, false) == "foo.bar");
  // A tag already on the line is not a boundary: the run back through `>` would
  // swallow the tag's own name, so the token is refused at the `<` and the Tab
  // stays with indentation.
  REQUIRE(Emmet::abbreviation_before("<section>div", 12, false).empty());
  // On the next line it is.
  REQUIRE(Emmet::abbreviation_before("<section>\n  div", 15, false) == "div");
  REQUIRE(Emmet::abbreviation_before("", 0, false).empty());
  // A number is not the start of an abbreviation.
  REQUIRE(Emmet::abbreviation_before("2m", 2, false).empty());
}

TEST_CASE("Emmet: the file types it works in", "[jot][emmet]")
{
  REQUIRE(Emmet::is_markup_file("a.html"));
  REQUIRE(Emmet::is_markup_file("a.HTM"));
  REQUIRE(Emmet::is_markup_file("a.tsx"));
  REQUIRE(Emmet::is_css_file("a.css"));
  REQUIRE(Emmet::is_css_file("a.scss"));
  REQUIRE(Emmet::is_supported_file("a.jsx"));
  REQUIRE_FALSE(Emmet::is_supported_file("a.cpp"));
  REQUIRE_FALSE(Emmet::is_supported_file("a.md"));
}

TEST_CASE("Emmet in the editor: Tab expands the abbreviation at the cursor", "[jot][emmet]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, ".html", "div.box\n", 0, 7);

  REQUIRE(e.expand_emmet_for_test());
  REQUIRE(e.buffer_for_test().line(0) == "<div class=\"box\"></div>");
  // The caret sits at the `$0` the expansion carried: inside the element, ready
  // for its content.
  REQUIRE(e.buffer_for_test().cursor.x == 17);
}

TEST_CASE("Emmet in the editor: nothing to expand leaves the buffer alone", "[jot][emmet]")
{
  seed_config_home();

  SECTION("the token does not parse")
  {
    // `div>` is not an abbreviation: an operator with nothing after it. A Tab
    // over one has to fall through to indentation rather than guess.
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, ".html", "div>\n", 0, 4);
    REQUIRE_FALSE(e.expand_emmet_for_test());
    REQUIRE(e.buffer_for_test().line(0) == "div>");
  }

  SECTION("the file is not markup or a stylesheet")
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, ".txt", "div.box\n", 0, 7);
    REQUIRE_FALSE(e.expand_emmet_for_test());
    REQUIRE(e.buffer_for_test().line(0) == "div.box");
  }
}

TEST_CASE("Emmet in the editor: a stylesheet expands declarations", "[jot][emmet]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, ".css", "  m10-20\n", 0, 8);

  REQUIRE(e.expand_emmet_for_test());
  REQUIRE(e.buffer_for_test().line(0) == "  margin: 10px 20px;");
}
