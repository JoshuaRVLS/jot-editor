// The completion popup's selected row: which one Up/Down put there, and which
// one the next frame keeps there.
//
// The list is re-filtered on every frame (render_lsp_completion runs the same
// pass a keystroke does, so the popup always shows the word as it is now), and
// that pass used to re-derive the selection from the *label* of the row that was
// selected. A server's overloads share one label -- every `std::includes` row is
// labeled "std::includes" -- so the lookup landed on the family's first row
// again on the very next frame and the arrow keys could never get past it: the
// highlight and the preview stayed on row 1 for every press. The selection now
// carries across an unchanged list by index, and a genuinely new list follows
// the symbol (label plus the parameter list in labelDetails) rather than the
// label alone.
#include "core/editor.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_completion_selection_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // One overload row: the label clangd sends, the parameter list it carries in
  // labelDetails (the one thing that tells one overload from another), and what
  // accepting inserts. No filterText, like most clangd items -- the typed word
  // is then matched against the insert text.
  LSPCompletionItem overload(const std::string &label,
                             const std::string &parameter_list,
                             const std::string &insert_text)
  {
    LSPCompletionItem it;
    it.label = label;
    it.label_detail = parameter_list;
    it.insert_text = insert_text;
    it.kind = 3;
    return it;
  }

  std::string open_with_caret(Editor &e, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_completion_selection_" + std::to_string(::getpid())
                             + "_" + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
    return path;
  }
} // namespace

TEST_CASE("Completion selection: Down survives the frame's re-filter", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  incl\n}\n", 1, 6);

  // The shape clangd really answers `incl` with: a family of overloads that all
  // read `std::includes` and differ only in their parameter lists.
  const std::vector<LSPCompletionItem> family = {
      overload("std::includes", "(ExecutionPolicy &&policy, ...)", "includes"),
      overload("std::includes", "(InputIt1 first1, InputIt1 last1, ...)", "includes"),
      overload("std::includes", "(InputIt1 first1, InputIt1 last1, ...)", "includes"),
      overload("std::inclusive_scan", "(InputIt first, InputIt last, ...)", "inclusive_scan"),
  };
  REQUIRE(e.seed_lsp_completion_for_test(family));
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.lsp_completion_selected_for_test() == 0);

  // Down, then the frame that follows it (the render path re-filters): the row
  // the key picked has to still be the selected one.
  e.raw_key_for_test(1009);
  e.refresh_lsp_completion_for_test();
  REQUIRE(e.lsp_completion_selected_for_test() == 1);

  e.raw_key_for_test(1009);
  e.refresh_lsp_completion_for_test();
  REQUIRE(e.lsp_completion_selected_for_test() == 2);
  // ...and the preview follows the highlight: accepting the `includes` overload
  // writes `includes`, so the rest of the word is what the caret previews.
  REQUIRE(e.lsp_completion_ghost_for_test() == "udes");

  // Up walks back and is preserved the same way.
  e.raw_key_for_test(1008);
  e.refresh_lsp_completion_for_test();
  REQUIRE(e.lsp_completion_selected_for_test() == 1);

  // The bottom of the list stays the bottom: three more Downs leave it on the
  // last row rather than wrapping back to the top.
  e.raw_key_for_test(1009);
  e.raw_key_for_test(1009);
  e.raw_key_for_test(1009);
  e.refresh_lsp_completion_for_test();
  REQUIRE(e.lsp_completion_selected_for_test() == 3);
  REQUIRE(e.lsp_completion_ghost_for_test() == "usive_scan");
}

TEST_CASE("Completion selection: the wheel over the popup walks the list", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // Long enough that the pane can scroll: the last check is that a wheel which
  // lands outside the popup still belongs to the code behind it.
  std::string text = "int main() {\n  incl\n";
  for (int i = 0; i < 60; i++)
  {
    text += "  // filler\n";
  }
  text += "}\n";
  open_with_caret(e, text, 1, 6);

  const std::vector<LSPCompletionItem> family = {
      overload("std::includes", "(ExecutionPolicy &&policy, ...)", "includes"),
      overload("std::includes", "(InputIt1 first1, InputIt1 last1, ...)", "includes"),
      overload("std::includes", "(InputIt2 first2, InputIt2 last2, ...)", "includes"),
      overload("std::includes", "(InputIt3 first3, InputIt3 last3, ...)", "includes"),
      overload("std::inclusive_scan", "(InputIt first, InputIt last, ...)", "inclusive_scan"),
      overload("std::inner_product", "(InputIt1 first1, InputIt1 last1, ...)", "inner_product"),
  };
  REQUIRE(e.seed_lsp_completion_for_test(family));

  // The wheel is aimed at the box the frame painted, the way a user aims it.
  e.render_for_test();
  REQUIRE(e.lsp_completion_box_w_for_test() > 0);
  REQUIRE(e.lsp_completion_box_h_for_test() > 1);
  const int box_x = e.lsp_completion_box_x_for_test();
  const int box_y = e.lsp_completion_box_y_for_test();
  const int wheel_x = box_x + 3;
  const int wheel_y = box_y + 2;

  // One notch is three rows, the step the palette and the quick pick take; the
  // notch does not fall through to the buffer behind the list.
  e.wheel_event_for_test(wheel_x, wheel_y, false, true);
  REQUIRE(e.lsp_completion_selected_for_test() == 3);
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.buffer_for_test().scroll_offset == 0);

  // A notch up walks back, and the clamp holds at the top of the list.
  e.wheel_event_for_test(wheel_x, wheel_y, true, false);
  REQUIRE(e.lsp_completion_selected_for_test() == 0);
  e.wheel_event_for_test(wheel_x, wheel_y, true, false);
  REQUIRE(e.lsp_completion_selected_for_test() == 0);

  // Outside the box the wheel keeps its old owner: the popup closes and the
  // viewport takes the notch.
  e.wheel_event_for_test(std::max(1, box_x - 5), wheel_y, false, true);
  REQUIRE(!e.lsp_completion_visible_for_test());
  REQUIRE(e.buffer_for_test().scroll_offset != 0);
}

TEST_CASE("Completion selection: the pointer takes a row out of the popup", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  incl\n}\n", 1, 6);

  const std::vector<LSPCompletionItem> family = {
      overload("std::includes", "(ExecutionPolicy &&policy, ...)", "includes_policy"),
      overload("std::includes", "(InputIt1 first1, ...)", "includes_iter"),
      overload("std::includes", "(InputIt3 first3, ...)", "includes_third"),
  };
  REQUIRE(e.seed_lsp_completion_for_test(family));
  e.render_for_test();
  const int box_x = e.lsp_completion_box_x_for_test();
  const int box_y = e.lsp_completion_box_y_for_test();
  const int box_h = e.lsp_completion_box_h_for_test();
  REQUIRE(box_h > 2);
  const int row_x = box_x + 3;
  // The stored rect is the box's outside, border included: its first row is the
  // top border, the item rows follow, then the footer, then the bottom border.
  const int item1_y = box_y + 2;             // the second item row
  const int footer_y = box_y + box_h - 2;
  const int bottom_y = box_y + box_h - 1;

  // The border and the footer are the box too: a press there is swallowed
  // rather than reaching the text through the list.
  const Cursor held_caret = e.buffer_for_test().cursor;
  e.mouse_event_for_test(row_x, box_y, /*bstate=*/1);     // top border
  e.mouse_event_for_test(row_x, footer_y, /*bstate=*/1);  // footer row
  e.mouse_event_for_test(row_x, bottom_y, /*bstate=*/1);  // bottom border
  REQUIRE(e.lsp_completion_visible_for_test());
  REQUIRE(e.buffer_for_test().cursor.y == held_caret.y);
  REQUIRE(e.buffer_for_test().cursor.x == held_caret.x);
  REQUIRE(e.buffer_for_test().line(1) == "  incl");

  // Motion over a row selects it and leaves the list up: it used to hide the
  // popup, so reaching for a row with the pointer dismissed it.
  e.mouse_event_for_test(row_x, item1_y, /*bstate=*/32);
  REQUIRE(e.lsp_completion_selected_for_test() == 1);
  REQUIRE(e.lsp_completion_visible_for_test());

  // The press takes that row -- the item's own text lands where the typed word
  // was, and the list closes.
  e.mouse_event_for_test(row_x, item1_y, /*bstate=*/1);
  REQUIRE(!e.lsp_completion_visible_for_test());
  REQUIRE(e.buffer_for_test().line(1) == "  includes_iter");

  // Outside the box the press keeps its old owner: the list closes and the
  // caret goes where the pointer is. A fresh popup, because taking a row left
  // the word the last one was completing.
  open_with_caret(e, "int main() {\n  incl\n}\n", 1, 6);
  REQUIRE(e.seed_lsp_completion_for_test(family));
  e.render_for_test();
  REQUIRE(e.lsp_completion_visible_for_test());
  e.mouse_event_for_test(std::max(1, box_x - 5), item1_y, /*bstate=*/1);
  REQUIRE(!e.lsp_completion_visible_for_test());
}

TEST_CASE("Completion selection: a new list follows the selected overload", "[jot]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  val\n}\n", 1, 5);

  const LSPCompletionItem first = overload("value", "(int)", "value");
  const LSPCompletionItem second = overload("value", "(double)", "value");
  const LSPCompletionItem third = overload("value", "(char)", "value");
  REQUIRE(e.seed_lsp_completion_for_test({first, second, third}));
  REQUIRE(e.lsp_completion_selected_for_test() == 0);

  // Select the `double` overload, then let the server answer again -- a
  // re-request re-ranks the family, so the same row can come back at another
  // index. The selection follows the parameter list, not the label (which every
  // row shares) and not the index.
  e.raw_key_for_test(1009);
  REQUIRE(e.lsp_completion_selected_for_test() == 1);
  REQUIRE(e.seed_lsp_completion_for_test({third, first, second}));
  REQUIRE(e.lsp_completion_selected_for_test() == 2);

  // An answer where that overload is gone falls back to the family's first row.
  REQUIRE(e.seed_lsp_completion_for_test({third, first}));
  REQUIRE(e.lsp_completion_selected_for_test() == 0);
}
