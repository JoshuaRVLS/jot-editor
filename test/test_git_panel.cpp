// Git panel model tests: porcelain status -> section/rank/glyph mapping and
// the flattened row layout shared by the renderer and mouse hit-testing.
#include "jot/workspace/git_panel_models.h"
#include <catch2/catch_test_macros.hpp>

using namespace jot_git_panel;

TEST_CASE("Git status maps to panel sections", "[jot]")
{
  REQUIRE(status_section("??") == "untracked");
  REQUIRE(status_section("M ") == "staged");
  REQUIRE(status_section(" M") == "unstaged");
  REQUIRE(status_section("MM") == "staged"); // staged wins for display
  REQUIRE(status_section("A ") == "staged");
  REQUIRE(status_section(" D") == "unstaged");
  REQUIRE(status_section("DD") == "conflict");
  REQUIRE(status_section("UU") == "conflict");
  REQUIRE(status_section("AU") == "conflict");
  REQUIRE(status_section("R ") == "staged");
}

TEST_CASE("Git status ranks order sections", "[jot]")
{
  REQUIRE(status_rank("DD") < status_rank("M "));
  REQUIRE(status_rank("M ") < status_rank(" M"));
  REQUIRE(status_rank(" M") < status_rank("??"));
  REQUIRE(status_rank("A ") == status_rank("M "));
}

TEST_CASE("Git status glyphs show the active letters", "[jot]")
{
  REQUIRE(status_glyph("??") == "??");
  REQUIRE(status_glyph("M ") == "M");
  REQUIRE(status_glyph(" M") == "M");
  REQUIRE(status_glyph("MM") == "MM");
  REQUIRE(status_glyph("  ") == "-");
  REQUIRE(status_glyph("") == "?");
}

TEST_CASE("Files view flattens into sections with row indices", "[jot]")
{
  State s;
  s.view = View::Files;
  s.files = {
      {"", "z.txt", "??"},
      {"", "a.txt", " M"},
      {"", "b.txt", "M "},
      {"", "c.txt", "DD"},
  };
  const auto rows = build_flat_rows(s);

  // conflict (c), staged (b), unstaged (a), untracked (z) sections.
  REQUIRE(rows.size() == 8);
  REQUIRE(rows[0].section);
  REQUIRE(rows[0].label == "conflict");
  REQUIRE_FALSE(rows[1].section);
  REQUIRE(rows[1].label == "c.txt");
  REQUIRE(rows[1].detail == "DD");
  REQUIRE(rows[2].section);
  REQUIRE(rows[2].label == "staged");
  REQUIRE_FALSE(rows[3].section);
  REQUIRE(rows[3].label == "b.txt");
  REQUIRE(rows[4].section);
  REQUIRE(rows[4].label == "unstaged");
  REQUIRE_FALSE(rows[5].section);
  REQUIRE(rows[5].label == "a.txt");
  REQUIRE(rows[6].section);
  REQUIRE(rows[6].label == "untracked");
  REQUIRE_FALSE(rows[7].section);
  REQUIRE(rows[7].label == "z.txt");
  // Indices point back into the files vector.
  REQUIRE(rows[1].index == 3);
  REQUIRE(rows[3].index == 2);
  REQUIRE(rows[5].index == 1);
  REQUIRE(rows[7].index == 0);
}

TEST_CASE("Empty sections are skipped in the files view", "[jot]")
{
  State s;
  s.view = View::Files;
  s.files = {{"", "only.txt", "??"}};
  const auto rows = build_flat_rows(s);
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[0].section);
  REQUIRE(rows[0].label == "untracked");
  REQUIRE_FALSE(rows[1].section);
}

TEST_CASE("Branches and commits flatten one row per entry", "[jot]")
{
  State s;
  s.view = View::Branches;
  s.branches = {{"main", true}, {"feature", false}};
  auto rows = build_flat_rows(s);
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[0].label == "* main");
  REQUIRE(rows[0].index == 0);
  REQUIRE(rows[1].label == "  feature");

  s.view = View::Commits;
  s.commits = {{"abc1234", "2026-09-01", "fix things"}, {"def5678", "2026-08-30", "add stuff"}};
  rows = build_flat_rows(s);
  REQUIRE(rows.size() == 2);
  REQUIRE(rows[0].label == "abc1234  fix things");
  REQUIRE(rows[0].detail == "2026-09-01");
}

TEST_CASE("Stash rows carry their ref as the label prefix", "[jot]")
{
  State s;
  s.view = View::Stash;
  s.stashes = {{"stash@{0}", "WIP on main"}};
  const auto rows = build_flat_rows(s);
  REQUIRE(rows.size() == 1);
  REQUIRE(rows[0].label == "stash@{0}  WIP on main");
}