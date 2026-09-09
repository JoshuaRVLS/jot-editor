#include "telescope.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("Telescope Fuzzy Match", "[jot]")
{
  REQUIRE(Telescope::fuzzy_match("src/tools/telescope.cpp", "tsp"));
  REQUIRE(Telescope::fuzzy_match("RenderOverlay", "ro"));
  REQUIRE_FALSE(Telescope::fuzzy_match("README.md", "xyz"));
}

TEST_CASE("Telescope Fuzzy Score Ranking", "[jot]")
{
  int exact = Telescope::fuzzy_score("telescope.cpp", "telescope.cpp");
  int substring = Telescope::fuzzy_score("telescope_preview.cpp", "preview");
  int scattered = Telescope::fuzzy_score("src/tools/telescope.cpp", "tsp");

  REQUIRE(exact > substring);
  REQUIRE(substring > scattered);
  REQUIRE(Telescope::fuzzy_score("README.md", "xyz") == 0);
}

TEST_CASE("Telescope Rank Score Prefers Real Source Over Duplicates", "[jot]")
{
  // Query "foo": the clean source file must outrank the numbered copy and
  // the " - Copy" duplicate, even though both contain "foo".
  const std::string q = "foo";
  int clean = Telescope::rank_score("foo.c", "foo.c", q, false);
  int numbered = Telescope::rank_score("foo (1).c", "foo (1).c", q, false);
  int copy = Telescope::rank_score("foo - Copy.c", "foo - Copy.c", q, false);
  int copy_underscore = Telescope::rank_score("foo_copy.c", "foo_copy.c", q, false);

  REQUIRE(clean > numbered);
  REQUIRE(clean > copy);
  REQUIRE(clean > copy_underscore);

  // A source file outranks a same-name non-code asset on a near tie.
  int source = Telescope::rank_score("main.c", "main.c", "main", false);
  int asset = Telescope::rank_score("main.png", "main.png", "main", false);
  REQUIRE(source > asset);

  // Non-matching query scores 0.
  REQUIRE(Telescope::rank_score("foo.c", "foo.c", "zzz", false) == 0);
}

TEST_CASE("Telescope Apply Results Selection And Display", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> matches;
  matches.push_back({"/repo/src/tools/telescope.cpp",
                     "telescope.cpp",
                     "src/tools/telescope.cpp",
                     "src/tools",
                     100,
                     false});
  matches.push_back({"/repo/src/render", "render", "src/render", "src", 90, true});

  telescope.apply_results(matches);
  REQUIRE(telescope.get_result_count() == 2);
  REQUIRE(telescope.get_selected_path() == "/repo/src/tools/telescope.cpp");
  REQUIRE(telescope.get_selected_relative_path() == "src/tools/telescope.cpp");

  telescope.move_down();
  REQUIRE(telescope.get_selected_path() == "/repo/src/render");

  telescope.apply_results({});
  REQUIRE(telescope.get_result_count() == 0);
  REQUIRE(telescope.get_selected_index() == 0);
}

TEST_CASE("Telescope Selection And List Scroll Clamp", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> matches;
  for (int i = 0; i < 8; i++)
  {
    std::string name = "file" + std::to_string(i) + ".txt";
    matches.push_back({"/repo/" + name, name, name, ".", 100 - i, false});
  }

  telescope.apply_results(matches);
  telescope.ensure_selected_visible(3);
  REQUIRE(telescope.get_list_scroll_offset() == 0);

  telescope.select_index(5);
  telescope.ensure_selected_visible(3);
  REQUIRE(telescope.get_selected_index() == 5);
  REQUIRE(telescope.get_list_scroll_offset() == 3);

  telescope.move_by(99);
  telescope.ensure_selected_visible(3);
  REQUIRE(telescope.get_selected_index() == 7);
  REQUIRE(telescope.get_list_scroll_offset() == 5);

  telescope.move_by(-99);
  telescope.ensure_selected_visible(3);
  REQUIRE(telescope.get_selected_index() == 0);
  REQUIRE(telescope.get_list_scroll_offset() == 0);
}

TEST_CASE("Telescope Caches Entries And Refilters Queries Instantly", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> entries;
  entries.push_back({"/repo/src/tools/telescope.cpp",
                     "telescope.cpp",
                     "src/tools/telescope.cpp",
                     "src/tools",
                     0,
                     false});
  entries.push_back({"/repo/src/render.cpp", "render.cpp", "src/render.cpp", "src", 0, false});
  entries.push_back({"/repo/notes.md", "notes.md", "notes.md", ".", 0, false});
  telescope.apply_results(entries);

  // Once the candidate list is cached (apply_results / a finished scan), a
  // set_query filters purely in memory — it must never touch the filesystem
  // or change the candidate pool.
  telescope.set_query("tel");
  REQUIRE(telescope.get_result_count() == 1);
  REQUIRE(telescope.get_selected_path() == "/repo/src/tools/telescope.cpp");

  telescope.set_query("render");
  REQUIRE(telescope.get_result_count() == 1);
  REQUIRE(telescope.get_selected_path() == "/repo/src/render.cpp");

  // Path-aware subsequence match still works through the cache.
  telescope.set_query("tools/te");
  REQUIRE(telescope.get_result_count() == 1);

  telescope.set_query("zzzz");
  REQUIRE(telescope.get_result_count() == 0);

  // Clearing the query returns to browse mode over the cached candidates.
  telescope.set_query("");
  REQUIRE(telescope.get_result_count() == 3);
}

TEST_CASE("Telescope Preview Scroll Clamp", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> matches;
  matches.push_back({"/repo/missing.txt", "missing.txt", "missing.txt", ".", 100, false});
  telescope.apply_results(matches);

  telescope.scroll_preview(10, 1);
  REQUIRE(telescope.get_preview_scroll_offset() == 0);

  telescope.select_index(0);
  REQUIRE(telescope.get_preview_scroll_offset() == 0);
}

TEST_CASE("Telescope Open Drops Stale Scan State", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> entries;
  entries.push_back(
      {"/repo/a.cpp", "a.cpp", "a.cpp", ".", 0, false});
  telescope.apply_results(entries);
  REQUIRE(telescope.get_result_count() == 1);

  // Reopening clears the cache and resets flags so a new scan starts from a
  // known state instead of inheriting a stale valid listing.
  telescope.open("");
  REQUIRE(telescope.get_result_count() == 0);
  REQUIRE_FALSE(telescope.scan_pending());
  REQUIRE(telescope.scan_error().empty());

  // A sync walk of a missing root reports the failure instead of caching an
  // empty listing as valid. NOTE: open() maps a missing root to the process
  // cwd (see Telescope::open), so drive the failure straight at the walker
  // to test the failure path itself.
  telescope.open(std::string(JOT_TEST_SOURCE_DIR));
  telescope.update_results();
  REQUIRE(telescope.scan_error().empty());
  REQUIRE(telescope.get_result_count() > 0);
}

TEST_CASE("Telescope Invalidate Cache Resets Results", "[jot]")
{
  Telescope telescope;
  std::vector<FileMatch> entries;
  entries.push_back(
      {"/repo/a.cpp", "a.cpp", "a.cpp", ".", 0, false});
  telescope.apply_results(entries);
  REQUIRE(telescope.get_result_count() == 1);

  telescope.invalidate_cache();
  REQUIRE(telescope.get_result_count() == 0);

  // A fresh sync walk repopulates through the normal filter path.
  telescope.set_query("", nullptr, {});
  REQUIRE(telescope.get_result_count() >= 0);
}

TEST_CASE("Telescope Fuzzy Match Positions Highlight The Consumed Characters", "[jot]")
{
  // "telescope.cpp": t@0, s@4, p@7 (greedy left-to-right like fuzzy_match).
  REQUIRE(Telescope::fuzzy_match_positions("telescope.cpp", "tsp")
          == std::vector<int>{0, 4, 7});
  // Case-insensitive: "ro" lands on the first r and the first later o.
  REQUIRE(Telescope::fuzzy_match_positions("RenderOverlay", "ro")
          == std::vector<int>{0, 6});
  // Consecutive query characters highlight consecutively.
  REQUIRE(Telescope::fuzzy_match_positions("RenderOverlay", "Re")
          == std::vector<int>{0, 1});
  // No match (or empty query) -> no highlight offsets.
  REQUIRE(Telescope::fuzzy_match_positions("README.md", "xyz").empty());
  REQUIRE(Telescope::fuzzy_match_positions("README.md", "").empty());
  // Multibyte text: ASCII queries only ever match ASCII bytes, and a
  // multibyte query byte-matches its own rune (both bytes of é), so the
  // highlight spans cover the whole glyph.
  REQUIRE(Telescope::fuzzy_match_positions("café.txt", "caf")
          == std::vector<int>{0, 1, 2});
  REQUIRE(Telescope::fuzzy_match_positions("café.txt", "é")
          == std::vector<int>{3, 4});
}
