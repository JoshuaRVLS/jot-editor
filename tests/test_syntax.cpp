#include "test_framework.h"
#include "tree_sitter_manager.h"
#include <string>

TEST(TestTreeSitterLanguageRegistration) {
  TreeSitterManager manager;

  ASSERT_TRUE(manager.has_language(".c"));
  ASSERT_TRUE(manager.has_language(".py"));
  ASSERT_TRUE(manager.has_language(".rs"));
  ASSERT_TRUE(manager.has_language(".md"));
  ASSERT_TRUE(manager.has_language(".rb"));
  ASSERT_TRUE(manager.has_language(".vue"));
  ASSERT_EQ(manager.language_id_for_extension(".ts"), "typescript");
  ASSERT_EQ(manager.language_id_for_extension(".tsx"), "tsx");
  ASSERT_TRUE(!manager.has_language(".unknown"));
}

TEST(TestTreeSitterRuntimeOverrides) {
  TreeSitterManager manager;
  manager.set_runtime_options({}, {}, {".foo:zig", "bar=python"});

  ASSERT_TRUE(manager.has_language(".foo"));
  ASSERT_EQ(manager.language_id_for_extension(".foo"), "zig");
  ASSERT_TRUE(manager.has_language_override(".foo"));
  ASSERT_TRUE(manager.has_language(".bar"));
  ASSERT_EQ(manager.language_id_for_extension(".bar"), "python");
  ASSERT_TRUE(manager.has_language_override(".bar"));
  ASSERT_TRUE(!manager.has_language_override(".py"));
}

TEST(TestTreeSitterHeaderOverrideCanSelectCpp) {
  TreeSitterManager manager;
  ASSERT_EQ(manager.language_id_for_extension(".h"), "c");
  ASSERT_TRUE(!manager.has_language_override(".h"));

  manager.set_runtime_options({}, {}, {".h:cpp"});
  ASSERT_EQ(manager.language_id_for_extension(".h"), "cpp");
  ASSERT_TRUE(manager.has_language_override(".h"));
}

TEST(TestTreeSitterRuntimeStatusUnsupportedExtension) {
  TreeSitterManager manager;
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".unknown");

  ASSERT_TRUE(!status.has_language);
  ASSERT_TRUE(!status.parser_loaded);
  ASSERT_TRUE(!status.query_loaded);
  ASSERT_TRUE(status.parser_message.find("unsupported extension") !=
              std::string::npos);
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
TEST(TestTreeSitterMissingParserReportsDiagnostic) {
  TreeSitterManager manager;
  manager.set_runtime_options({}, {}, {".missing:missing_language"});

  ASSERT_TRUE(manager.get_highlight_query(".missing") == nullptr);
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".missing");

  ASSERT_TRUE(status.has_language);
  ASSERT_EQ(status.language_id, "missing_language");
  ASSERT_TRUE(!status.parser_loaded);
  ASSERT_TRUE(status.parser_message.find("parser not loaded") !=
              std::string::npos);
}

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

TEST(TestTreeSitterCppQueryAvailableWhenParserInstalled) {
  TreeSitterManager manager;

  TSQuery *query = manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".cpp");
  if (status.parser_loaded) {
    ASSERT_TRUE(query != nullptr);
    ASSERT_TRUE(status.query_loaded);
  } else {
    ASSERT_TRUE(query == nullptr);
  }
}

TEST(TestTreeSitterReloadReattemptsParserAndQuery) {
  TreeSitterManager manager;

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus before =
      manager.runtime_status_for_extension(".cpp");

  manager.reload();
  TreeSitterRuntimeStatus after_reload =
      manager.runtime_status_for_extension(".cpp");
  ASSERT_TRUE(after_reload.has_language);
  ASSERT_TRUE(!after_reload.parser_loaded);
  ASSERT_TRUE(!after_reload.query_loaded);

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus after_retry =
      manager.runtime_status_for_extension(".cpp");
  if (before.parser_loaded) {
    ASSERT_TRUE(after_retry.parser_loaded);
    ASSERT_TRUE(after_retry.query_loaded);
  }
}
#endif
