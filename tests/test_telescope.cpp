#include "telescope.h"
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

TEST_CASE("Telescope Fuzzy Match", "[jot]") {
  REQUIRE(Telescope::fuzzy_match("src/tools/telescope.cpp", "tsp"));
  REQUIRE(Telescope::fuzzy_match("RenderOverlay", "ro"));
  REQUIRE_FALSE(Telescope::fuzzy_match("README.md", "xyz"));
}

TEST_CASE("Telescope Fuzzy Score Ranking", "[jot]") {
  int exact = Telescope::fuzzy_score("telescope.cpp", "telescope.cpp");
  int substring = Telescope::fuzzy_score("telescope_preview.cpp", "preview");
  int scattered = Telescope::fuzzy_score("src/tools/telescope.cpp", "tsp");

  REQUIRE(exact > substring);
  REQUIRE(substring > scattered);
  REQUIRE(Telescope::fuzzy_score("README.md", "xyz") == 0);
}

TEST_CASE("Telescope Apply Results Selection And Display", "[jot]") {
  Telescope telescope;
  std::vector<FileMatch> matches;
  matches.push_back({"/repo/src/tools/telescope.cpp", "telescope.cpp",
                     "src/tools/telescope.cpp", "src/tools", 100, false});
  matches.push_back({"/repo/src/render", "render", "src/render", "src", 90,
                     true});

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

TEST_CASE("Telescope Selection And List Scroll Clamp", "[jot]") {
  Telescope telescope;
  std::vector<FileMatch> matches;
  for (int i = 0; i < 8; i++) {
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

TEST_CASE("Telescope Preview Scroll Clamp", "[jot]") {
  Telescope telescope;
  std::vector<FileMatch> matches;
  matches.push_back({"/repo/missing.txt", "missing.txt", "missing.txt", ".",
                     100, false});
  telescope.apply_results(matches);

  telescope.scroll_preview(10, 1);
  REQUIRE(telescope.get_preview_scroll_offset() == 0);

  telescope.select_index(0);
  REQUIRE(telescope.get_preview_scroll_offset() == 0);
}
