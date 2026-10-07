// The builtin tag list in a markup buffer: the rows
// src/editor/integrations/lsp/common.h appends, and the two paths that decide when
// they are shown and what accepting one writes (completion.cpp).
//
// Every row is a whole opening element (`<div>|</div>`), so the list completes a
// tag name -- the word right after the `<` that opens the tag -- and nothing
// else. The cases pin the two ways that went wrong. The list was offered for
// words that are not tag names, so its rows landed in the middle of what the
// author was writing (`div>` + Tab wrote `div><div></div>`, `</d` + Tab wrote
// `</d<div></div>`) and the Tab went to a row instead of to the Emmet refusal the
// docs promise falls through to indentation. And accepting a row never covered
// the `<` the author had already typed, because the replacement site is the word
// after it: `<` + Tab wrote `<<a href=""></a>`.
#include "editor.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  bool has(const std::vector<std::string> &list, const std::string &value)
  {
    return std::find(list.begin(), list.end(), value) != list.end();
  }

  void seed_config_home()
  {
    char home[] = "/tmp/jot_tag_completion_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  Editor &probe_editor()
  {
    static Editor e;
    return e;
  }

  // A file whose content is `text`, opened for real: the suffix decides the file
  // type, and the file type is half of what the cases are about.
  void open_file(Editor &e, const std::string &suffix, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_tag_completion_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + suffix;
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(0, (int)text.size());
  }
} // namespace

TEST_CASE("Tag completion: the list is offered for a tag name only", "[jot][tag-completion]")
{
  seed_config_home();
  Editor &e = probe_editor();
  e.set_home_menu_visible(false);

  // The shorthand probe's scene: `div>` over plain text. The word is not a tag
  // name, so its rows would land in the middle of the text.
  open_file(e, ".html", "div>");
  e.request_lsp_completion_for_test('>');
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_labels_for_test().empty());

  // A closing tag's name is not one either: no row is a `</div>`.
  open_file(e, ".html", "</d");
  e.request_lsp_completion_for_test('d');
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_labels_for_test().empty());
  open_file(e, ".html", "</");
  e.request_lsp_completion_for_test('/');
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_labels_for_test().empty());

  // An attribute value is a class name's place, which the workspace index (and
  // a server) answer for -- the tag list does not belong there.
  open_file(e, ".html", "<div class=\"he");
  e.request_lsp_completion_for_test('e');
  REQUIRE_FALSE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_labels_for_test().empty());

  // A tag name, bare and with a letter typed: where the list belongs, so none of
  // the cases above is the seeding being off in markup.
  open_file(e, ".html", "<d");
  e.request_lsp_completion_for_test('d');
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(has(e.lsp_completion_labels_for_test(), "div"));

  open_file(e, ".html", "<");
  e.request_lsp_completion_for_test('<');
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(has(e.lsp_completion_labels_for_test(), "div"));
  REQUIRE(has(e.lsp_completion_labels_for_test(), "a"));
}

TEST_CASE("Tag completion: accepting a row keeps the `<` the author typed once",
          "[jot][tag-completion]")
{
  seed_config_home();
  Editor &e = probe_editor();
  e.set_home_menu_visible(false);

  // `<` then `d`: the two keystrokes that arm the popup and filter it. The list
  // used to be written over the word alone, leaving the `<` in front of it.
  open_file(e, ".html", "<");
  e.request_lsp_completion_for_test('<');
  e.host().core.insert_char_at_carets('d');
  e.refresh_lsp_completion_for_test();
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(has(e.lsp_completion_labels_for_test(), "div"));
  REQUIRE(e.apply_selected_lsp_completion_for_test());
  REQUIRE(e.host().core.buffer_content() == "<div></div>");
}

TEST_CASE("Tag completion: a server's own range is left alone", "[jot][tag-completion]")
{
  seed_config_home();
  Editor &e = probe_editor();
  e.set_home_menu_visible(false);

  // A server names the range it replaces, so the take-back above is not applied
  // to it: the `x` outside the range has to survive.
  open_file(e, ".html", "x<d");
  LSPCompletionItem item;
  item.label = "div";
  item.filter_text = "div"; // what the typed word is matched against
  item.insert_text = "<div>|</div>";
  item.insert_text_format = 1;
  item.has_text_edit_range = true;
  item.edit_start_line = 0;
  item.edit_start_char = 1;
  item.edit_end_line = 0;
  item.edit_end_char = 3;
  REQUIRE(e.seed_lsp_completion_for_test({item}));
  REQUIRE(e.apply_selected_lsp_completion_for_test());
  REQUIRE(e.host().core.buffer_content() == "x<div></div>");
}
