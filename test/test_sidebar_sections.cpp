// The sidebar's stacked sections (EXPLORER / OUTLINE / TIMELINE) and the
// timeline model behind them: the one geometry the painter, the mouse
// hit-test and the keyboard walk read, the git log parser, and the compact
// relative ages the timeline rows show.
#include "jot/model/sidebar_sections.h"
#include "jot/workspace/timeline.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The sidebar stacks explorer, outline and timeline under their headers", "[jot]")
{
  // A 20-row column with a 12-row tree, 4 symbols and 3 commits: the tree
  // fills what the other two leave, each section keeps its own header, and the
  // footer stays on the last row.
  const auto layout = jot_sidebar::compute(0, 20, 12, 4, 3, true, true, true, true);
  REQUIRE(layout.explorer.header_y == 0);
  REQUIRE(layout.explorer.y == 1);
  REQUIRE(layout.explorer.h == 9);
  REQUIRE(layout.outline.header_y == layout.explorer.y + layout.explorer.h);
  REQUIRE(layout.outline.y == layout.outline.header_y + 1);
  REQUIRE(layout.outline.h == 4);
  REQUIRE(layout.timeline.header_y == layout.outline.y + layout.outline.h);
  REQUIRE(layout.timeline.y == layout.timeline.header_y + 1);
  REQUIRE(layout.timeline.h == 3);
  REQUIRE(layout.footer_y == 19);
  REQUIRE(layout.timeline.y + layout.timeline.h == layout.footer_y);

  // A collapsed section keeps its header (the click that reopens it) and gives
  // its rows to the tree.
  const auto collapsed = jot_sidebar::compute(0, 20, 12, 4, 3, true, false, true, true);
  REQUIRE(collapsed.outline.header_y >= 0);
  REQUIRE(collapsed.outline.h == 0);
  REQUIRE(collapsed.outline.y == -1);
  REQUIRE(collapsed.timeline.h == 3);
  REQUIRE(collapsed.explorer.h == 9 + 4);

  // A collapsed explorer gives its rows away and stops being navigable.
  const auto no_tree = jot_sidebar::compute(0, 20, 12, 4, 3, false, true, true, true);
  REQUIRE(no_tree.explorer.h == 0);
  REQUIRE(no_tree.explorer.y == -1);
  REQUIRE(no_tree.outline.h == 4);
  REQUIRE(no_tree.timeline.h == 3);

  // The tree's floor beats the other sections: on a short column they shrink
  // to nothing rather than squeezing the explorer under its minimum.
  const auto tight = jot_sidebar::compute(0, 8, 12, 4, 3, true, true, true, true);
  REQUIRE(tight.explorer.h >= jot_sidebar::kTreeMinRows);
  REQUIRE(tight.explorer.h + tight.outline.h + tight.timeline.h + 3 + 1 == 8);

  // Too short for three sections and the tree's floor together: the explorer
  // keeps the column alone.
  const auto tiny = jot_sidebar::compute(0, 6, 12, 4, 3, true, true, true, true);
  REQUIRE(tiny.explorer.header_y == 0);
  REQUIRE(tiny.explorer.h == 4);
  REQUIRE(tiny.outline.header_y == -1);
  REQUIRE(tiny.timeline.header_y == -1);
  REQUIRE(tiny.footer_y == 5);
}

TEST_CASE("A sidebar row maps to the section and index it paints", "[jot]")
{
  const auto layout = jot_sidebar::compute(0, 20, 12, 4, 3, true, true, true, true);
  REQUIRE(jot_sidebar::section_at(layout, layout.explorer.header_y)
          == jot_sidebar::SECTION_EXPLORER);
  REQUIRE(jot_sidebar::section_at(layout, layout.explorer.y) == jot_sidebar::SECTION_EXPLORER);
  REQUIRE(jot_sidebar::section_at(layout, layout.outline.header_y) == jot_sidebar::SECTION_OUTLINE);
  REQUIRE(jot_sidebar::section_at(layout, layout.timeline.header_y)
          == jot_sidebar::SECTION_TIMELINE);
  REQUIRE(jot_sidebar::section_at(layout, layout.footer_y) == -1);

  bool header = false;
  REQUIRE(jot_sidebar::index_in_section(layout.outline, layout.outline.header_y, &header) == -1);
  REQUIRE(header);
  REQUIRE(jot_sidebar::index_in_section(layout.outline, layout.outline.y, &header) == 0);
  REQUIRE_FALSE(header);
  REQUIRE(jot_sidebar::index_in_section(
              layout.outline, layout.outline.y + layout.outline.h - 1, &header)
          == layout.outline.h - 1);
  REQUIRE(jot_sidebar::index_in_section(layout.outline, layout.footer_y, &header) == -2);
}

TEST_CASE("The timeline parses git log output and compacts ages", "[jot]")
{
  const std::string log = "abc1234\t1700000000\tMake the status line one row\n"
                          "def5678\t1600000000\tChrome: draw borders only where two panes meet\n"
                          "\n"
                          "not-a-commit-line\n"
                          "9999999\tnot-a-time\tSubject with a bad timestamp\n";
  const std::vector<jot_timeline::Commit> commits = jot_timeline::parse_log(log);
  REQUIRE(commits.size() == 3);
  REQUIRE(commits[0].hash == "abc1234");
  REQUIRE(commits[0].epoch == 1700000000);
  REQUIRE(commits[0].subject == "Make the status line one row");
  REQUIRE(commits[1].subject == "Chrome: draw borders only where two panes meet");
  REQUIRE(commits[2].epoch == 0);
  REQUIRE(jot_timeline::parse_log("").empty());

  const long long now = 1700000000;
  REQUIRE(jot_timeline::relative_age(now - 30, now) == "now");
  REQUIRE(jot_timeline::relative_age(now - 5 * 60, now) == "5m");
  REQUIRE(jot_timeline::relative_age(now - 3 * 3600, now) == "3h");
  REQUIRE(jot_timeline::relative_age(now - 2 * 86400, now) == "2d");
  REQUIRE(jot_timeline::relative_age(now - 14 * 86400, now) == "2 wks");
  REQUIRE(jot_timeline::relative_age(now - 21 * 86400, now) == "3 wks");
  REQUIRE(jot_timeline::relative_age(now - 40 * 86400, now) == "1 mo");
  REQUIRE(jot_timeline::relative_age(now - 400 * 86400, now) == "1 yr");
  REQUIRE(jot_timeline::relative_age(0, now) == "");
}
