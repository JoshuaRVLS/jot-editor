// Explorer tree indent guides: rows carry neo-tree style connectors
// (expander chevrons for directories, "│ " bars and "└ " feet for files)
// that keep the 2-cells-per-level layout of the old indent scheme. The
// connector under an expanded folder comes from the children's own markers,
// so it stays visible even when the folder is the last child.
#include "editor.h"
#include "sidebar_guides.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_sb_test_XXXXXX";
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
} // namespace

TEST_CASE("Explorer rows carry tree indent guides", "[jot]")
{
  Editor &e = probe_editor();
  char tmpl[] = "/tmp/jot_guide_XXXXXX";
  mkdtemp(tmpl);
  const std::string root = tmpl;
  fs::create_directory(root + "/src");
  fs::create_directory(root + "/tests");
  write_file(root + "/src/a.cpp", "int a;");
  write_file(root + "/src/b.cpp", "int b;");
  write_file(root + "/README.md", "# hi");

  // open_workspace shows the sidebar by default.
  e.host().io.open_workspace(root);
  REQUIRE(e.host().render.layout().sidebar_visible);

  const auto &rows = e.sidebar_render_cache().rows;
  // Directories sort first (by name): src, tests, then README.md.
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[0].is_dir);
  REQUIRE(rows[0].guide == " "); // collapsed directory chevron at the root
  REQUIRE(rows[1].is_dir);
  REQUIRE(rows[1].guide == " ");
  REQUIRE_FALSE(rows[2].is_dir);
  // Last top-level file gets the └ foot, two cells wide like the old
  // indent slot so names never shift.
  REQUIRE(rows[2].guide == "└ ");
  REQUIRE(rows[2].guide_cells == 2);
  REQUIRE(rows[2].label.rfind("└ ", 0) == 0);
}

TEST_CASE("Children always get the connector under their folder", "[jot]")
{
  using sidebar_guides::children_guide;
  using sidebar_guides::row_guide;

  // Regression: a top-level folder that is the LAST (or only) item used to
  // leave its children without any connector. The children's own markers
  // (at the folder's name column) form the vertical line regardless.
  REQUIRE(row_guide(false, false, false, 1, "") == "  │ ");
  REQUIRE(row_guide(false, false, true, 1, "") == "  └ ");
  // A top-level folder with following siblings: its column continues with
  // a │ on the children's rows.
  const std::string under_non_last = children_guide(false, "");
  REQUIRE(row_guide(false, false, false, 2, under_non_last) == "  │ │ ");
  REQUIRE(row_guide(false, false, true, 2, under_non_last) == "  │ └ ");

  // Directories keep their expander chevron at the marker column.
  REQUIRE(row_guide(true, false, false, 0, "") == " ");
  REQUIRE(row_guide(true, true, false, 0, "") == " ");
  REQUIRE(row_guide(true, true, false, 1, "") == "   ");

  // Nested trace: src (not last) -> sub (expanded, not last) -> files;
  // the full tree reads:
  //    src
  //      sub
  //     │ │ x.c
  //     │ └ y.h
  //     └ b.cpp
  //   └ README
  REQUIRE(row_guide(true, false, false, 0, "") == " ");
  REQUIRE(row_guide(true, true, false, 1, "") == "   ");
  const std::string sub_children = children_guide(false, ""); // sub not last
  REQUIRE(row_guide(false, false, false, 2, sub_children) == "  │ │ ");
  REQUIRE(row_guide(false, false, true, 2, sub_children) == "  │ └ ");
  REQUIRE(row_guide(false, false, true, 1, "") == "  └ ");
  REQUIRE(row_guide(false, false, true, 0, "") == "└ ");
}

TEST_CASE("Guide slots keep the row width at 2 cells per level", "[jot]")
{
  using sidebar_guides::children_guide;
  using sidebar_guides::row_guide;
  auto cells = [](const std::string &s)
  {
    int n = 0;
    for (size_t i = 0; i < s.size();)
    {
      const unsigned char c = (unsigned char)s[i];
      i += (c & 0x80) ? ((c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4) : 1;
      n++;
    }
    return n;
  };
  // depth 0: 2 cells, depth 1: 4 cells, depth 2: 6 cells — same widths as
  // the old indent + chevron layout.
  REQUIRE(cells(row_guide(false, false, true, 0, "")) == 2);
  REQUIRE(cells(row_guide(false, false, false, 1, "")) == 4);
  REQUIRE(cells(row_guide(false, false, false, 2, children_guide(true, ""))) == 6);
  REQUIRE(cells(row_guide(false, false, false, 2, children_guide(false, ""))) == 6);
}

TEST_CASE("Guide rows keep the label layout width", "[jot]")
{
  Editor &e = probe_editor();
  char tmpl[] = "/tmp/jot_guide2_XXXXXX";
  mkdtemp(tmpl);
  const std::string root = tmpl;
  write_file(root + "/z.txt", "z");
  write_file(root + "/a.txt", "a");

  e.host().io.open_workspace(root);

  const auto &rows = e.sidebar_render_cache().rows;
  REQUIRE(rows.size() == 2);
  // Non-last file gets the │ bar, the last one the └ foot.
  REQUIRE(rows[0].guide == "│ ");
  REQUIRE(rows[0].guide_cells == 2);
  REQUIRE(rows[1].guide == "└ ");
}