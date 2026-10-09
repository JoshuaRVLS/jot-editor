// The outline's pointer. The dock's list is painted by render_outline_panel
// but reached by the dispatcher's handle_mouse -- the live press/motion path,
// which used to route nothing to it (only the wheel entry point did), so the
// list was inert: no hover band, no click, and the chevrons could not fold.
// The sidebar's outline section shares the same symbol model; its hits are
// display rows inside the section's scroll window, so they have to be read
// back through that window before they name a symbol.
#include "editor.h"
#include "editor/model/sidebar_sections.h"
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

  // A row reads indent (two cells per depth), the chevron's two cells, the kind
  // icon, a space, then the name. The icon is the cell that labels the name it
  // belongs to, so a nested row's icon moves in with its indent instead of
  // hanging on the column's left edge.
  const jot_sidebar::Layout layout = e.sidebar_layout_for_test();
  REQUIRE(layout.outline.h >= (int)visible.size());
  REQUIRE(e.sidebar_sections_for_test().outline_scroll == 0);
  REQUIRE(e.outline_scroll_for_test() == 0);
  const int sidebar_x = e.sidebar_content_x_for_test();
  for (size_t row = 0; row < visible.size(); row++)
  {
    const SymbolMatch &s = symbols[(size_t)visible[row]];
    const int y = layout.outline.y + (int)row;
    const int icon_x = sidebar_x + 3 + s.depth * 2;
    REQUIRE(ui->cell_at(icon_x, y)->ch == Winbar::symbol_icon(s.kind));
    REQUIRE(ui->cell_at(icon_x + 1, y)->ch == " ");
    REQUIRE(ui->cell_at(icon_x + 2, y)->ch == std::string(1, s.name[0]));
    if (s.depth > 0)
    {
      REQUIRE(ui->cell_at(sidebar_x + 1, y)->ch == " "); // no glyph at the edge
    }
  }

  // The dock's list paints the same row, one cell in from the panel edge. The
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

TEST_CASE("The sidebar outline keeps every row's name on the row", "[jot]")
{
  // A narrow column with a deep tree and long declared types: the type beside a
  // name used to win the row, leaving an icon and a type where a symbol should
  // be (a row could read as ".. void"), and a deep row's indent could do the
  // same on its own.
  const std::string root = make_workspace("narrow",
                                          "narrow.cpp",
                                          "namespace app {\n"
                                          "struct Outer {\n"
                                          "  struct Inner {\n"
                                          "    struct Deep {\n"
                                          "      unsigned long long member_with_long_name;\n"
                                          "      void method_with_long_name();\n"
                                          "    };\n"
                                          "  };\n"
                                          "};\n"
                                          "}\n");
  Editor e;
  open_with_outline(e, root, "narrow.cpp");
  const jot_sidebar::Layout layout = e.sidebar_layout_for_test();
  const UI *ui = e.ui_for_test();
  const std::vector<SymbolMatch> symbols = e.outline_symbols_for_test();
  const std::vector<int> visible = e.outline_visible_for_test();
  REQUIRE(visible.size() == 6); // app, Outer, Inner, Deep, and its two members
  REQUIRE(e.sidebar_sections_for_test().outline_scroll == 0);
  REQUIRE(layout.outline.h >= (int)visible.size());

  const int content_x = e.sidebar_content_x_for_test();
  int checked = 0;
  for (size_t row = 0; row < visible.size(); row++)
  {
    const SymbolMatch &s = symbols[(size_t)visible[row]];
    const int y = layout.outline.y + (int)row;
    std::string painted;
    for (int x = content_x; x <= content_x + 25; x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      painted += cell ? cell->ch : " ";
    }
    // The name is on the row -- trimmed if the column is tight, never gone.
    REQUIRE(s.name.size() >= 3);
    REQUIRE(painted.find(s.name.substr(0, 3)) != std::string::npos);
    checked++;
  }
  REQUIRE(checked == (int)visible.size());
}

TEST_CASE("The sidebar outline reads its hits through the section's window", "[jot]")
{
  std::string text = "struct Box {\n  int w;\n  int h;\n};\n";
  for (int i = 0; i < 30; i++)
  {
    text += "void f" + std::to_string(i) + "();\n";
  }
  const std::string root = make_workspace("boxes", "boxes.cpp", text);
  Editor e;
  open_with_outline(e, root, "boxes.cpp");

  const jot_sidebar::Layout layout = e.sidebar_layout_for_test();
  REQUIRE(layout.outline.header_y >= 0);
  REQUIRE(layout.outline.h >= 2);
  // Every container open: Box, w, h, then f0..f29 -- more rows than the
  // section can show, so its window really has to scroll.
  const std::vector<int> visible = e.outline_visible_for_test();
  REQUIRE(visible.size() == 33);
  REQUIRE(visible.size() > (size_t)layout.outline.h);

  // Motion over the section's third row paints the hover band there.
  e.mouse_event_for_test(3, layout.outline.y + 2, /*bstate=*/32);
  Editor::SidebarHoverForTest hover = e.sidebar_hover_for_test();
  REQUIRE(hover.section == jot_sidebar::SECTION_OUTLINE);
  REQUIRE(hover.index == 2);
  e.render_for_test();
  REQUIRE(e.ui_for_test()->cell_at(3, layout.outline.y + 2)->bg
          == e.theme_for_test().bg_sidebar_selected_inactive);

  // Leaving the column (over the panes) drops it.
  e.mouse_event_for_test(e.ui_width_for_test() / 2, layout.outline.y + 2, /*bstate=*/32);
  REQUIRE(e.sidebar_hover_for_test().section == -1);

  // The wheel walks the section's cursor -- the dock's outline does the same
  // with its selection -- and the window follows it. Scrolling the window
  // alone used to be undone by the render's keep-the-cursor-visible clamp, so
  // a wheel over the outline did nothing while the cursor sat at the top and
  // the rows below the window could not be reached.
  const int view_h = layout.outline.h;
  for (int i = 0; i < view_h + 2; i++)
  {
    e.wheel_event_for_test(3, layout.outline.y, false, /*down=*/true);
  }
  e.render_for_test();
  REQUIRE(e.sidebar_sections_for_test().outline_cursor == view_h + 2);
  const int scroll = e.sidebar_sections_for_test().outline_scroll;
  // The cursor is the window's last row, so the window's first row is an
  // offset the hit-test has to add back before it can name a symbol.
  REQUIRE(scroll == e.sidebar_sections_for_test().outline_cursor - view_h + 1);
  REQUIRE(scroll > 0);

  // Click the row the window now starts on: the hit is a display row, so it
  // names the symbol painted there (the data row under the window's offset),
  // not the one that used to sit there.
  e.mouse_event_for_test(5, layout.outline.y, /*bstate=*/1);
  REQUIRE(e.sidebar_sections_for_test().outline_cursor == scroll);
  const std::vector<int> painted = e.outline_visible_for_test();
  REQUIRE(scroll < (int)painted.size());
  REQUIRE(e.buffer_for_test().cursor.y == e.outline_symbols_for_test()[(size_t)painted[(size_t)scroll]].line);

  // Back at the top, the Box row's chevron folds the struct instead of jumping
  // to it.
  for (int i = 0; i < view_h + 2; i++)
  {
    e.wheel_event_for_test(3, layout.outline.y, /*up=*/true, false);
  }
  e.render_for_test();
  REQUIRE(e.sidebar_sections_for_test().outline_scroll == 0);
  // The Box row is depth 0, so its chevron starts one cell in from the
  // section's content edge.
  e.mouse_event_for_test(e.sidebar_content_x_for_test() + 1, layout.outline.y, /*bstate=*/1);
  REQUIRE_FALSE(e.outline_expanded_for_test(0));
  REQUIRE(e.outline_visible_for_test().size() == 31); // w and h folded away
  REQUIRE(e.sidebar_sections_for_test().outline_cursor == 0);
}
