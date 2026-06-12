#include "test_framework.h"
#include "tree_sitter_manager.h"
#include <string>

TEST(TestTreeSitterLanguageRegistration) {
  TreeSitterManager manager;

  ASSERT_TRUE(manager.has_language(".c"));
  ASSERT_TRUE(manager.has_language(".py"));
  ASSERT_TRUE(manager.has_language(".rs"));
  ASSERT_TRUE(manager.has_language(".md"));
  ASSERT_TRUE(!manager.has_language(".unknown"));
}

TEST(TestCppQueryCoversScopedAndPrimitiveTokens) {
  TreeSitterManager manager;
  const TSLanguageEntry *entry = manager.get_language(".cpp");

  ASSERT_TRUE(entry != nullptr);
  std::string query = entry->highlight_query_source;
  ASSERT_TRUE(query.find("(primitive_type) @type") != std::string::npos);
  ASSERT_TRUE(query.find("(namespace_identifier) @type") != std::string::npos);
  ASSERT_TRUE(query.find("qualified_identifier") != std::string::npos);
  ASSERT_TRUE(query.find("(call_expression function: (qualified_identifier") !=
              std::string::npos);
}

TEST(TestTreeSitterCaptureMappingPriority) {
  ASSERT_EQ(tree_sitter_capture_color_for_name("keyword"), 1);
  ASSERT_EQ(tree_sitter_capture_color_for_name("string"), 2);
  ASSERT_EQ(tree_sitter_capture_color_for_name("comment"), 3);
  ASSERT_EQ(tree_sitter_capture_color_for_name("number"), 4);
  ASSERT_EQ(tree_sitter_capture_color_for_name("type"), 5);
  ASSERT_EQ(tree_sitter_capture_color_for_name("function"), 6);
  ASSERT_EQ(tree_sitter_capture_color_for_name("unknown.capture"), 0);

  ASSERT_TRUE(tree_sitter_capture_priority_for_name("comment") >
              tree_sitter_capture_priority_for_name("keyword"));
  ASSERT_TRUE(tree_sitter_capture_priority_for_name("string") >
              tree_sitter_capture_priority_for_name("function"));
  ASSERT_TRUE(tree_sitter_capture_priority_for_name("function") >
              tree_sitter_capture_priority_for_name("type"));
}

#ifdef JOT_TREESITTER
TEST(TestTreeSitterQueryCache) {
  TreeSitterManager manager;

  TSQuery *first = manager.get_highlight_query(".c");
  TSQuery *second = manager.get_highlight_query(".c");
  if (first) {
    ASSERT_EQ(first, second);
  } else {
    ASSERT_EQ(second, nullptr);
  }
}
#endif
