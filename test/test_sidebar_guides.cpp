// Explorer tree indent guides: rows carry neo-tree style connectors
// (chevrons for directories, ├─/└─ elbows for files, │ ancestor guides)
// that keep the 2-cells-per-level layout of the old indent scheme.
#include "editor.h"
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
  // Last top-level file gets the └─ elbow, two cells wide like the old
  // indent slot so names never shift.
  REQUIRE(rows[2].guide == "└─");
  REQUIRE(rows[2].guide_cells == 2);
  REQUIRE(rows[2].label.rfind("└─", 0) == 0);
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
  // Non-last file gets the ├─ elbow.
  REQUIRE(rows[0].guide == "├─");
  REQUIRE(rows[0].guide_cells == 2);
  REQUIRE(rows[1].guide == "└─");
}