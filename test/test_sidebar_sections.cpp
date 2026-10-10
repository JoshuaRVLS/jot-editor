// The left column: one tree under its header, with the sidebar's footer kept on
// the column's last row -- the single geometry the painter, the mouse hit-test
// and the keyboard walk all read.
#include "editor.h"
#include "editor/model/sidebar_sections.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_sidebar_sections_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  void write_file(const std::string &path, const std::string &text)
  {
    std::ofstream out(path);
    out << text;
  }

  // One painted row of the grid, read back from the UI's cells: what the eye
  // sees, not what the model holds.
  std::string row_text(Editor &e, int y)
  {
    const UI *ui = e.ui_for_test();
    std::string text;
    for (int x = 0; x < e.ui_width_for_test(); ++x)
    {
      const UICell *cell = ui->cell_at(x, y);
      text += cell ? cell->ch : " ";
    }
    return text;
  }
} // namespace

TEST_CASE("The sidebar is one tree under its header, the footer on the last row", "[jot]")
{
  // A 20-row column: the header takes row 0, the footer the last row, and the
  // tree every row between them.
  const auto layout = jot_sidebar::compute(0, 20, /*explorer_open=*/true, /*show_footer=*/true);
  REQUIRE(layout.explorer.header_y == 0);
  REQUIRE(layout.explorer.y == 1);
  REQUIRE(layout.explorer.h == 18);
  REQUIRE(layout.footer_y == 19);
  REQUIRE(layout.explorer.y + layout.explorer.h == layout.footer_y);

  // The offset is the column's own: a sidebar pushed down by a tab strip keeps
  // its rows in place.
  const auto offset = jot_sidebar::compute(2, 10, true, true);
  REQUIRE(offset.explorer.header_y == 2);
  REQUIRE(offset.explorer.y == 3);
  REQUIRE(offset.explorer.h == 8);
  REQUIRE(offset.footer_y == 11);

  // A collapsed tree keeps its header (the click that reopens it) and shows no
  // rows at all.
  const auto collapsed = jot_sidebar::compute(0, 20, /*explorer_open=*/false, true);
  REQUIRE(collapsed.explorer.header_y == 0);
  REQUIRE(collapsed.explorer.h == 0);
  REQUIRE(collapsed.explorer.y == -1);
  REQUIRE(collapsed.footer_y == 19);

  // With no footer the tree takes the last row too.
  const auto no_footer = jot_sidebar::compute(0, 20, true, /*show_footer=*/false);
  REQUIRE(no_footer.footer_y == -1);
  REQUIRE(no_footer.explorer.h == 19);

  // The footer owns the last row, so a one-row column is the footer and the
  // header gets no row at all; the same single row is the header when there is
  // no footer to take it. A column with no rows lays out nothing.
  const auto footer_only = jot_sidebar::compute(0, 1, true, true);
  REQUIRE(footer_only.explorer.header_y == -1);
  REQUIRE(footer_only.explorer.h == 0);
  REQUIRE(footer_only.footer_y == 0);
  const auto header_only = jot_sidebar::compute(0, 1, true, /*show_footer=*/false);
  REQUIRE(header_only.explorer.header_y == 0);
  REQUIRE(header_only.explorer.h == 0);
  REQUIRE(header_only.footer_y == -1);
  const auto empty = jot_sidebar::compute(0, 0, true, true);
  REQUIRE(empty.explorer.header_y == -1);
  REQUIRE(empty.footer_y == -1);
}

TEST_CASE("A sidebar row maps to the tree index it paints", "[jot]")
{
  const auto layout = jot_sidebar::compute(0, 20, true, true);
  bool header = false;
  REQUIRE(jot_sidebar::index_in_section(layout.explorer, layout.explorer.header_y, &header) == -1);
  REQUIRE(header);
  REQUIRE(jot_sidebar::index_in_section(layout.explorer, layout.explorer.y, &header) == 0);
  REQUIRE_FALSE(header);
  REQUIRE(jot_sidebar::index_in_section(
              layout.explorer, layout.explorer.y + layout.explorer.h - 1, &header)
          == layout.explorer.h - 1);
  REQUIRE(jot_sidebar::index_in_section(layout.explorer, layout.footer_y, &header) == -2);
  REQUIRE(jot_sidebar::index_in_section(layout.explorer, 999, &header) == -2);
}

TEST_CASE("The explorer header toggles the tree", "[jot]")
{
  Editor &e = probe_editor();
  char tmpl[] = "/tmp/jot_sidebar_header_XXXXXX";
  mkdtemp(tmpl);
  const std::string root = tmpl;
  write_file(root + "/a.txt", "one\n");

  e.host().io.open_workspace(root);
  e.apply_resize_for_test(120, 40);
  e.render_for_test();

  const int header_y = e.sidebar_layout_for_test().explorer.header_y;
  REQUIRE(header_y == 0);
  REQUIRE(e.sidebar_sections_for_test().explorer_open);

  // A click on the header folds the tree away; the same click brings it back.
  e.mouse_event_for_test(10, header_y, /*bstate=*/1);
  REQUIRE_FALSE(e.sidebar_sections_for_test().explorer_open);
  e.render_for_test();
  REQUIRE(e.sidebar_layout_for_test().explorer.y == -1);

  e.mouse_event_for_test(10, header_y, /*bstate=*/1);
  REQUIRE(e.sidebar_sections_for_test().explorer_open);
  e.render_for_test();
  REQUIRE(e.sidebar_layout_for_test().explorer.h > 0);
}

TEST_CASE("The sidebar footer speaks for the workspace, not the selected row", "[jot]")
{
  Editor &e = probe_editor();
  char tmpl[] = "/tmp/jot_footer_ws_XXXXXX";
  mkdtemp(tmpl);
  const std::string root = tmpl;
  write_file(root + "/alpha.txt", "alpha\n");
  write_file(root + "/beta.txt", "beta\n");

  e.host().io.open_workspace(root);
  e.apply_resize_for_test(120, 40);
  e.render_for_test();

  // Row 2 is the first tree row (0 is the EXPLORER header, 1 the pinned root):
  // a click opens alpha.txt and leaves it selected.
  e.mouse_event_for_test(5, 2, /*bstate=*/1);
  e.render_for_test();
  REQUIRE(fs::path(e.buffer_for_test().filepath).filename().string() == "alpha.txt");
  REQUIRE_FALSE(e.sidebar_root_selected_for_test());

  // The footer is the column's last row. It used to repeat the selected row's
  // glyph and name, which read as a stray file label there; it belongs to the
  // workspace instead. This folder is not a repository, so it stays clean.
  const std::string footer = row_text(e, e.ui_height_for_test() - 2);
  REQUIRE(footer.find("alpha.txt") == std::string::npos);
  REQUIRE(footer.find("beta.txt") == std::string::npos);
}

TEST_CASE("The explorer's expanded folders come back open", "[jot]")
{
  char home[] = "/tmp/jot_explorer_expanded_XXXXXX";
  mkdtemp(home);
  setenv("JOT_CONFIG_HOME", home, 1);
  setenv("JOT_CACHE_HOME", home, 1);
  const std::string root = std::string(home) + "/ws";
  fs::create_directories(root + "/pkg");
  fs::create_directories(root + "/closed");
  write_file(root + "/pkg/inside.txt", "one\n");
  write_file(root + "/closed/shut.txt", "two\n");
  write_file(root + "/top.txt", "three\n");

  // Left behind with pkg open: a click on the first tree row opens it (folders
  // sort before files), and closed is never touched.
  {
    Editor e;
    e.host().io.open_workspace(root);
    e.apply_resize_for_test(120, 40);
    e.render_for_test();
    int pkg_row = -1;
    const SidebarRenderCache &cache = e.sidebar_render_cache();
    for (int i = 0; i < (int)cache.rows.size(); i++)
    {
      if (cache.rows[i].name == "pkg")
      {
        pkg_row = i;
      }
    }
    REQUIRE(pkg_row >= 0);
    // Row 0 is the section header and row 1 the pinned root, so the tree's own
    // rows start on row 2.
    e.mouse_event_for_test(5, 2 + pkg_row, /*bstate=*/1);
    e.render_for_test();
    bool opened = false;
    for (const SidebarRenderRow &row : e.sidebar_render_cache().rows)
    {
      opened = opened || row.name == "inside.txt";
    }
    REQUIRE(opened);
    e.save_workspace_session_for_test();
  }

  // The next session opens the same workspace, which is what restores it.
  {
    Editor e2;
    e2.host().io.open_workspace(root);
    bool opened = false;
    bool shut = false;
    for (const SidebarRenderRow &row : e2.sidebar_render_cache().rows)
    {
      opened = opened || row.name == "inside.txt";
      shut = shut || row.name == "shut.txt";
    }
    REQUIRE(opened);
    REQUIRE_FALSE(shut);
  }
}
