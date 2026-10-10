// The outline's pointer. The dock's list is painted by render_outline_panel
// but reached by the dispatcher's handle_mouse -- the live press/motion path,
// which used to route nothing to it (only the wheel entry point did), so the
// list was inert: no hover band, no click, and the chevrons could not fold.
#include "editor.h"
#include "render/winbar_model.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  void write_file(const std::string &path, const std::string &text)
  {
    std::ofstream out(path);
    out << text;
  }

  // A fresh config home + workspace root holding `file_name`.
  std::string make_workspace(const char *tag, const std::string &file_name, const std::string &text)
  {
    char home[] = "/tmp/jot_outline_mouse_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
    const std::string root = std::string(home) + "/" + tag;
    fs::create_directories(root);
    write_file(root + "/" + file_name, text);
    return root;
  }

  // Opens the workspace, clicks the file's explorer row (row 0 is the
  // EXPLORER header, row 1 the pinned root, row 2 the tree's first row), puts
  // the outline dock up and paints a frame, so the geometry a case clicks in
  // is the geometry the renderer used.
  void open_with_outline(Editor &e, const std::string &root, const std::string &file_name)
  {
    e.set_home_menu_visible(false);
    e.apply_resize_for_test(120, 32);
    e.host().io.open_workspace(root);
    e.render_for_test();
    e.mouse_event_for_test(5, 2, /*bstate=*/1);
    REQUIRE(fs::path(e.buffer_for_test().filepath).filename().string() == file_name);
    e.open_right_panel_tab(RIGHT_PANEL_SYMBOLS);
    e.render_for_test();
  }
} // namespace

TEST_CASE("The dock outline takes the press: hover, jump, chevron fold", "[jot]")
{
  const std::string root = make_workspace("widget",
                                          "widget.hpp",
                                          "namespace app {\n"
                                          "struct Widget {\n"
                                          "  int height;\n"
                                          "  void draw();\n"
                                          "};\n"
                                          "}\n"
                                          "void top();\n");
  Editor e;
  open_with_outline(e, root, "widget.hpp");

  const int panel_x = e.ui_width_for_test() - e.right_panel_width_for_test();
  const int body_y = e.outline_body_y_for_test();
  REQUIRE(e.outline_body_h_for_test() >= 5);
  const std::vector<int> rows = e.outline_visible_for_test();
  REQUIRE(rows.size() == 5); // app, Widget, height, draw, top

  // Motion over the `height` row paints the hover band on it, and the row above
  // it keeps the panel's own background.
  e.mouse_event_for_test(panel_x + 6, body_y + 2, /*bstate=*/32);
  REQUIRE(e.outline_hover_row_for_test() == 2);
  e.render_for_test();
  const UI *ui = e.ui_for_test();
  REQUIRE(ui->cell_at(panel_x + 6, body_y + 2)->bg == e.theme_for_test().bg_selection);
  REQUIRE(ui->cell_at(panel_x + 6, body_y + 1)->bg == e.theme_for_test().bg_terminal);

  // The pointer leaving the dock drops the band.
  e.mouse_event_for_test(e.ui_width_for_test() / 2, body_y + 2, /*bstate=*/32);
  REQUIRE(e.outline_hover_row_for_test() == -1);

  // A press on the last row -- the bottom of the list -- selects it and jumps
  // the buffer to the symbol's line.
  e.mouse_event_for_test(panel_x + 6, body_y + 4, /*bstate=*/1);
  REQUIRE(e.outline_selected_for_test() == 4);
  REQUIRE(e.buffer_for_test().cursor.y == 6);

  // The struct's chevron (depth 1, so two cells in from the content edge)
  // folds its members away.
  const int struct_flat = rows[1];
  REQUIRE(e.outline_expanded_for_test(struct_flat));
  e.mouse_event_for_test(panel_x + 3, body_y + 1, /*bstate=*/1);
  REQUIRE_FALSE(e.outline_expanded_for_test(struct_flat));
  REQUIRE(e.outline_visible_for_test().size() == 3);

  // The rebuild a keystroke schedules must not spring the fold open: the
  // outline is a dropdown, not a list that re-opens itself every edit.
  e.ensure_outline_fresh_for_test(true);
  REQUIRE_FALSE(e.outline_expanded_for_test(struct_flat));
  REQUIRE(e.outline_visible_for_test().size() == 3);

  // The same chevron press opens it again.
  e.mouse_event_for_test(panel_x + 3, body_y + 1, /*bstate=*/1);
  REQUIRE(e.outline_expanded_for_test(struct_flat));
  REQUIRE(e.outline_visible_for_test().size() == 5);
}

TEST_CASE("The outline rows put the kind icon beside the name", "[jot]")
{
  const std::string root = make_workspace("icons",
                                          "icons.cpp",
                                          "namespace app {\n"
                                          "struct Box {\n"
                                          "  int w;\n"
                                          "};\n"
                                          "}\n");
  Editor e;
  open_with_outline(e, root, "icons.cpp");
  const UI *ui = e.ui_for_test();
  const std::vector<SymbolMatch> symbols = e.outline_symbols_for_test();
  const std::vector<int> visible = e.outline_visible_for_test();
  REQUIRE(visible.size() == 3); // app, Box, w

  // The dock's list paints each row one cell in from the panel edge. The
  // generic list kit spends the cursor bar cell first on the selected row, so
  // the rest of that row follows it -- every other row starts on the edge.
  const int panel_x = e.ui_width_for_test() - e.right_panel_width_for_test();
  const int body_y = e.outline_body_y_for_test();
  REQUIRE(e.outline_body_h_for_test() >= (int)visible.size());
  for (size_t row = 0; row < visible.size(); row++)
  {
    const SymbolMatch &s = symbols[(size_t)visible[row]];
    const int y = body_y + (int)row;
    const bool bar = ui->cell_at(panel_x + 1, y)->ch == "\u258C";
    const int icon_x = panel_x + 3 + s.depth * 2 + (bar ? 1 : 0);
    REQUIRE(ui->cell_at(icon_x, y)->ch == Winbar::symbol_icon(s.kind));
    REQUIRE(ui->cell_at(icon_x + 2, y)->ch == std::string(1, s.name[0]));
    if (s.depth > 0)
    {
      REQUIRE(ui->cell_at(panel_x + 1 + (bar ? 1 : 0), y)->ch == " ");
    }
  }
}
