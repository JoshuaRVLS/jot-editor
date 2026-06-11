#include "telescope.h"
#include "test_framework.h"

#include <algorithm>

TEST(TestTelescopeFuzzyMatch) {
  ASSERT_TRUE(Telescope::fuzzy_match("src/tools/telescope.cpp", "tsp"));
  ASSERT_TRUE(Telescope::fuzzy_match("RenderOverlay", "ro"));
  ASSERT_TRUE(!Telescope::fuzzy_match("README.md", "xyz"));
}

TEST(TestTelescopeFuzzyScoreRanking) {
  int exact = Telescope::fuzzy_score("telescope.cpp", "telescope.cpp");
  int substring = Telescope::fuzzy_score("telescope_preview.cpp", "preview");
  int scattered = Telescope::fuzzy_score("src/tools/telescope.cpp", "tsp");

  ASSERT_TRUE(exact > substring);
  ASSERT_TRUE(substring > scattered);
  ASSERT_TRUE(Telescope::fuzzy_score("README.md", "xyz") == 0);
}

TEST(TestTelescopeApplyResultsSelectionAndDisplay) {
  Telescope telescope;
  std::vector<FileMatch> matches;
  matches.push_back({"/repo/src/tools/telescope.cpp", "telescope.cpp",
                     "src/tools/telescope.cpp", "src/tools", 100, false});
  matches.push_back({"/repo/src/render", "render", "src/render", "src", 90,
                     true});

  telescope.apply_results(matches);
  ASSERT_EQ(telescope.get_result_count(), 2);
  ASSERT_EQ(telescope.get_selected_path(), "/repo/src/tools/telescope.cpp");
  ASSERT_EQ(telescope.get_selected_relative_path(), "src/tools/telescope.cpp");

  telescope.move_down();
  ASSERT_EQ(telescope.get_selected_path(), "/repo/src/render");

  telescope.apply_results({});
  ASSERT_EQ(telescope.get_result_count(), 0);
  ASSERT_EQ(telescope.get_selected_index(), 0);
}
