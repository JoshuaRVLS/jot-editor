// Git panel model tests: porcelain status -> section/rank/glyph mapping, the
// flattened row layout shared by the renderer and mouse hit-testing, and the
// tab / hint / scroll helpers both of those read.
#include "ui/text.h" // ui_cell_count
#include "workspace/git_panel_models.h"
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

TEST_CASE("Every view has one tab, and its key selects it", "[jot]")
{
  const std::vector<ViewTab> tabs = view_tabs();
  REQUIRE(tabs.size() == 4);
  // The tab's number is the key that switches to it (lazygit's numbering), and
  // the label names that number, so a user reading the strip knows the key.
  for (const ViewTab &tab : tabs)
  {
    REQUIRE(tab.key == (int)tab.view);
    REQUIRE(tab.label.find(std::to_string(tab.key)) != std::string::npos);
    REQUIRE(ui_cell_count(tab.label) > 4);
  }
  // Distinct views, distinct labels: the hit-test walks this list in order.
  REQUIRE(tabs[0].view == View::Files);
  REQUIRE(tabs[1].view == View::Branches);
  REQUIRE(tabs[2].view == View::Commits);
  REQUIRE(tabs[3].view == View::Stash);
}

TEST_CASE("The view hints fit the default panel", "[jot]")
{
  // The footer row is drawn in the panel's content width (the default
  // right_panel_width minus its two border columns), so a hint that does not
  // fit is a hint nobody reads.
  const int content_w = 42 - 2;
  for (const ViewTab &tab : view_tabs())
  {
    const std::string hint = view_hint(tab.view);
    REQUIRE_FALSE(hint.empty());
    REQUIRE(ui_cell_count(hint) <= content_w);
  }
  REQUIRE(view_hint(View::Files).find("stage") != std::string::npos);
  REQUIRE(view_hint(View::Branches).find("checkout") != std::string::npos);
  REQUIRE(view_hint(View::Commits).find("checkout") != std::string::npos);
  REQUIRE(view_hint(View::Stash).find("apply") != std::string::npos);
}

TEST_CASE("The selection maps to a flat row and scrolls itself into view", "[jot]")
{
  State s;
  s.files = {{"", "a.cpp", " M"}, {"", "b.cpp", "??"}};
  const auto rows = build_flat_rows(s);
  // Section headers sit between entries: the second entry is not row 1.
  REQUIRE(flat_index_of_selection(rows, 0) == 1);
  REQUIRE(flat_index_of_selection(rows, 1) == 3);
  REQUIRE(flat_index_of_selection(rows, 9) == -1);

  // A selection already on screen leaves the window alone...
  REQUIRE(scroll_to_show(0, 1, 10, (int)rows.size()) == 0);
  // ...one below it moves it by the least that shows the row...
  REQUIRE(scroll_to_show(0, 12, 10, 30) == 3);
  // ...one above it comes back to the row...
  REQUIRE(scroll_to_show(5, 2, 10, 30) == 2);
  // ...and the window never runs past the list's end.
  REQUIRE(scroll_to_show(0, 29, 10, 30) == 20);
  REQUIRE(scroll_to_show(7, 3, 10, 4) == 0);
}