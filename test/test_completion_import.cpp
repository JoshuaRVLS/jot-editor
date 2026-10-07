// Accepting a completion that carries additionalTextEdits: the auto-import
// path (src/editor/integrations/lsp/completion.cpp).
//
// A server pairs the item that names a symbol with the edits that make the name
// resolvable elsewhere in the same file -- an `import { Widget } from "./widget"`
// for typescript/vtsls, an `#include` for clangd. Those positions are the
// document the *request* saw, so the insert that lands first moves them; the
// cases below pin both halves: the import is written at all, and an edit below
// the insertion point follows the lines the insert added. An edit that cannot
// be placed honestly (it overlaps the text being completed) is dropped rather
// than written at a guessed column.
#include "editor.h"
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
    char home[] = "/tmp/jot_completion_import_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  LSPCompletionItem item(const std::string &label, const std::string &insert_text)
  {
    LSPCompletionItem it;
    it.label = label;
    it.insert_text = insert_text;
    it.filter_text = label;
    it.insert_text_format = 1;
    it.kind = 3; // method/function, as the popup's rows are read
    return it;
  }

  // A file whose content is `text`, opened with the caret at `line`:`col`. The
  // typed prefix is the token before the caret, exactly as a keystroke leaves it.
  void open_with_caret(Editor &e, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_completion_import_" + std::to_string(::getpid()) + "_"
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
} // namespace

TEST_CASE("Completion auto-import: the item's import is written with the completion", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  wid\n}\n", 1, 5);

  LSPCompletionItem widget = item("Widget", "Widget");
  widget.additional_text_edits.push_back({0, 0, 0, 0, "#include \"widget.hpp\"\n"});
  REQUIRE(e.seed_lsp_completion_for_test({widget}));
  REQUIRE(e.lsp_completion_visible_for_test());

  accept(e);

  // The import is on the row above everything it was inserted in front of, and
  // the completion still replaced the typed prefix rather than appending to it.
  REQUIRE(e.buffer_for_test().line(0) == "#include \"widget.hpp\"");
  REQUIRE(e.buffer_for_test().line(1) == "int main() {");
  REQUIRE(e.buffer_for_test().line(2) == "  Widget");
  REQUIRE(e.buffer_for_test().line(3) == "}");
}

TEST_CASE("Completion auto-import: an edit below the insertion point moves with it", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "a\nWid\nc\nd\n", 1, 3);

  LSPCompletionItem widget = item("Widget", "Widget\nx");
  // Line 3 is the `d` row in the document the server saw.
  widget.additional_text_edits.push_back({3, 0, 3, 0, "// tail"});
  REQUIRE(e.seed_lsp_completion_for_test({widget}));

  accept(e);

  const auto &buf = e.buffer_for_test();
  REQUIRE(buf.line(0) == "a");
  REQUIRE(buf.line(1) == "Widget");
  REQUIRE(buf.line(2) == "x");
  REQUIRE(buf.line(3) == "c");
  // The insert added a line, so the server's "line 3" is now line 4.
  REQUIRE(buf.line(4) == "// taild");
}

TEST_CASE("Completion auto-import: an edit that overlaps the completion is dropped", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "a\nWid\n", 1, 3);

  LSPCompletionItem widget = item("Widget", "Widget");
  // Starts after the replaced span on the same row: there is no column this can
  // be written at without guessing, so it must not be written at all.
  widget.additional_text_edits.push_back({1, 3, 1, 3, "!"});
  REQUIRE(e.seed_lsp_completion_for_test({widget}));

  accept(e);

  REQUIRE(e.buffer_for_test().line(1) == "Widget");
  REQUIRE(e.buffer_for_test().line_count() == 2);
}

TEST_CASE("Completion auto-import: an item without extra edits leaves the file alone", "[jot][lsp]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  wid\n}\n", 1, 5);

  REQUIRE(e.seed_lsp_completion_for_test({item("Widget", "Widget")}));
  accept(e);

  REQUIRE(e.buffer_for_test().line(0) == "int main() {");
  REQUIRE(e.buffer_for_test().line(1) == "  Widget");
  REQUIRE(e.buffer_for_test().line_count() == 3);
}
