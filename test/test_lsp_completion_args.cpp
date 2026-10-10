// Accepting a function completion: the call goes in without the server's
// argument placeholders (src/editor/integrations/lsp/completion.cpp).
//
// clangd answers a function completion with a snippet that carries the
// parameter list as placeholder defaults -- `add(${1:int left}, ${2:int
// right})` -- and expanding that used to type those parameters into the buffer,
// leaving the user to gut a call they never wrote. The call is now inserted with
// an empty argument list and the caret between the parens, so the parameters are
// the user's to type. A declaration-shaped snippet (a body after the parens) is
// not a call and keeps its text.
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
    char home[] = "/tmp/jot_lsp_completion_args_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  void open_with_caret(Editor &e, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_lsp_completion_args_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
  }

  // Tab, the key the popup teaches: the accept key goes through the frontend's
  // own decode path, so this is the user's gesture and not a private hook.
  void accept(Editor &e)
  {
    e.raw_key_for_test(9);
  }

  LSPCompletionItem call_item(const std::string &label, const std::string &insert_text, int kind)
  {
    LSPCompletionItem it;
    it.label = label;
    it.insert_text = insert_text;
    it.filter_text = label;
    it.kind = kind;
    return it;
  }
} // namespace

TEST_CASE("Accepting a function completion leaves the parameters for the user", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);

  // The clangd shape: a snippet with the parameter list as placeholder defaults.
  LSPCompletionItem add =
      call_item("add", "add(${1:int left}, ${2:int right})", 3); // Function
  add.insert_text_format = 2;
  REQUIRE(e.seed_lsp_completion_for_test({add}));
  REQUIRE(e.lsp_completion_visible_for_test());

  accept(e);

  REQUIRE(e.buffer_for_test().line(1) == "  add()");
  // The caret sits between the parentheses, where the arguments go.
  REQUIRE(e.buffer_for_test().cursor.y == 1);
  REQUIRE(e.buffer_for_test().cursor.x == 6);
}

TEST_CASE("A plain-text function completion drops its argument text too", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);

  // No snippet format: the server sent the call as literal text, and the
  // arguments are still the user's to type.
  REQUIRE(e.seed_lsp_completion_for_test({call_item("add", "add(int left, int right)", 3)}));
  accept(e);

  REQUIRE(e.buffer_for_test().line(1) == "  add()");
  REQUIRE(e.buffer_for_test().cursor.x == 6);
}

TEST_CASE("A declaration-shaped snippet is not mistaken for a call", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);

  // The body after the closing paren means this is a definition, not a call.
  LSPCompletionItem add = call_item("add", "add(${1:int left}, ${2:int right}) { return left + right; }",
                                    3);
  add.insert_text_format = 2;
  REQUIRE(e.seed_lsp_completion_for_test({add}));

  accept(e);

  REQUIRE(e.buffer_for_test().line(1).find("add()") == std::string::npos);
  REQUIRE(e.buffer_for_test().line(1).find("add(int left, int right)") != std::string::npos);
}

TEST_CASE("A call and a non-function item are left alone", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);

  // An already-empty call has nothing to strip.
  REQUIRE(e.seed_lsp_completion_for_test({call_item("add", "add()", 3)}));
  accept(e);
  REQUIRE(e.buffer_for_test().line(1) == "  add()");

  // A snippet that is not a function completion keeps its placeholders (a plain
  // snippet's job is to write text the user would otherwise type in full).
  open_with_caret(e, "int main() {\n  ma\n}\n", 1, 4);
  LSPCompletionItem pair = call_item("make_pair", "make_pair(${1:int a}, ${2:int b})", 15); // Snippet
  pair.insert_text_format = 2;
  REQUIRE(e.seed_lsp_completion_for_test({pair}));
  accept(e);
  REQUIRE(e.buffer_for_test().line(1).find("make_pair(int a, int b)") != std::string::npos);
}
