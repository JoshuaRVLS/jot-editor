#include "test_framework.h"
#include "types.h"
#include "tree_sitter/catalog.h"
#include "tree_sitter/language_spec.h"
#include "tree_sitter/manager.h"
#include <set>
#include <string>

TEST(TestTreeSitterLanguageRegistration) {
  TreeSitterManager manager;

  ASSERT_TRUE(manager.has_language(".c"));
  ASSERT_TRUE(manager.has_language(".py"));
  ASSERT_TRUE(manager.has_language(".rs"));
  ASSERT_TRUE(manager.has_language(".md"));
  ASSERT_TRUE(manager.has_language(".rb"));
  ASSERT_TRUE(manager.has_language(".vue"));
  ASSERT_EQ(manager.language_id_for_extension(".js"), "javascript");
  ASSERT_EQ(manager.language_id_for_extension(".jsx"), "javascript");
  ASSERT_EQ(manager.language_id_for_extension(".mjs"), "javascript");
  ASSERT_EQ(manager.language_id_for_extension(".cjs"), "javascript");
  ASSERT_EQ(manager.language_id_for_extension(".ts"), "typescript");
  ASSERT_EQ(manager.language_id_for_extension(".mts"), "typescript");
  ASSERT_EQ(manager.language_id_for_extension(".cts"), "typescript");
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
  ASSERT_TRUE(query.find("(primitive_type) @type.builtin") !=
              std::string::npos);
  ASSERT_TRUE(query.find("@keyword.control") != std::string::npos);
  ASSERT_TRUE(query.find("@keyword.storage") != std::string::npos);
  ASSERT_TRUE(query.find("@keyword.directive") != std::string::npos);
  ASSERT_TRUE(query.find("@function.method") != std::string::npos);
  ASSERT_TRUE(query.find("@constant.macro") != std::string::npos);
  ASSERT_TRUE(query.find("@string.escape") != std::string::npos);
  ASSERT_TRUE(query.find("@punctuation.bracket") != std::string::npos);
  ASSERT_TRUE(query.find("@punctuation.delimiter") != std::string::npos);
  ASSERT_TRUE(query.find("(namespace_identifier) @namespace") !=
              std::string::npos);
  ASSERT_TRUE(query.find("qualified_identifier scope: (namespace_identifier) "
                         "@namespace") != std::string::npos);
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
  ASSERT_EQ(tree_sitter_capture_token_for_name("variable"), TS_TOKEN_VARIABLE);
  ASSERT_EQ(tree_sitter_capture_token_for_name("variable.parameter"),
            TS_TOKEN_PARAMETER);
  ASSERT_EQ(tree_sitter_capture_token_for_name("function.builtin"),
            TS_TOKEN_BUILTIN);
  ASSERT_EQ(tree_sitter_capture_token_for_name("function.method"),
            TS_TOKEN_FUNCTION_METHOD);
  ASSERT_EQ(tree_sitter_capture_token_for_name("function.constructor"),
            TS_TOKEN_FUNCTION_CONSTRUCTOR);
  ASSERT_EQ(tree_sitter_capture_token_for_name("constant.builtin"),
            TS_TOKEN_BUILTIN);
  ASSERT_EQ(tree_sitter_capture_token_for_name("constant.macro"),
            TS_TOKEN_CONSTANT_MACRO);
  ASSERT_EQ(tree_sitter_capture_token_for_name("keyword.control"),
            TS_TOKEN_KEYWORD_CONTROL);
  ASSERT_EQ(tree_sitter_capture_token_for_name("keyword.storage"),
            TS_TOKEN_KEYWORD_STORAGE);
  ASSERT_EQ(tree_sitter_capture_token_for_name("keyword.directive"),
            TS_TOKEN_KEYWORD_PREPROC);
  ASSERT_EQ(tree_sitter_capture_token_for_name("operator"), TS_TOKEN_OPERATOR);
  ASSERT_EQ(tree_sitter_capture_token_for_name("punctuation.bracket"),
            TS_TOKEN_PUNCTUATION_BRACKET);
  ASSERT_EQ(tree_sitter_capture_token_for_name("punctuation.delimiter"),
            TS_TOKEN_PUNCTUATION_DELIMITER);
  ASSERT_EQ(tree_sitter_capture_token_for_name("string.escape"),
            TS_TOKEN_STRING_ESCAPE);
  ASSERT_EQ(tree_sitter_capture_token_for_name("tag.attribute"),
            TS_TOKEN_ATTRIBUTE);
  ASSERT_EQ(tree_sitter_capture_token_for_name("type.builtin"),
            TS_TOKEN_TYPE_BUILTIN);
  ASSERT_EQ(tree_sitter_capture_token_for_name("property"), TS_TOKEN_FIELD);
  ASSERT_EQ(tree_sitter_capture_color_for_name("unknown.capture"), 0);

  ASSERT_TRUE(tree_sitter_capture_priority_for_name("comment") >
              tree_sitter_capture_priority_for_name("keyword"));
  ASSERT_TRUE(tree_sitter_capture_priority_for_name("string") >
              tree_sitter_capture_priority_for_name("function"));
  ASSERT_TRUE(tree_sitter_capture_priority_for_name("function") >
              tree_sitter_capture_priority_for_name("type"));
  ASSERT_TRUE(tree_sitter_capture_priority_for_name("tag.attribute") >
              tree_sitter_capture_priority_for_name("property"));
}

TEST(TestTreeSitterBuiltInQueriesExposeRichCaptures) {
  TreeSitterManager manager;

  const TSLanguageEntry *cpp = manager.get_language(".cpp");
  ASSERT_TRUE(cpp != nullptr);
  ASSERT_TRUE(cpp->highlight_query_source.find("@variable") !=
              std::string::npos);
  ASSERT_TRUE(cpp->highlight_query_source.find("@property") !=
              std::string::npos);

  const TSLanguageEntry *python = manager.get_language(".py");
  ASSERT_TRUE(python != nullptr);
  ASSERT_TRUE(python->highlight_query_source.find("@variable.parameter") !=
              std::string::npos);

  const TSLanguageEntry *json = manager.get_language(".json");
  ASSERT_TRUE(json != nullptr);
  ASSERT_TRUE(json->highlight_query_source.find("@property") !=
              std::string::npos);

  const TSLanguageEntry *javascript = manager.get_language(".jsx");
  ASSERT_TRUE(javascript != nullptr);
  ASSERT_EQ(javascript->language_id, "javascript");
  ASSERT_TRUE(javascript->highlight_query_source.find("@tag") !=
              std::string::npos);
  ASSERT_TRUE(javascript->highlight_query_source.find("@tag.attribute") !=
              std::string::npos);
  ASSERT_TRUE(javascript->highlight_query_source.find(
                  "(jsx_attribute (jsx_namespace_name) @tag.attribute)") !=
              std::string::npos);
  ASSERT_TRUE(javascript->highlight_query_source.find("@function.method") !=
              std::string::npos);

  const TSLanguageEntry *typescript = manager.get_language(".ts");
  ASSERT_TRUE(typescript != nullptr);
  ASSERT_TRUE(typescript->highlight_query_source.find("@type.builtin") !=
              std::string::npos);
  ASSERT_TRUE(typescript->highlight_query_source.find("@variable.parameter") !=
              std::string::npos);

  const TSLanguageEntry *tsx = manager.get_language(".tsx");
  ASSERT_TRUE(tsx != nullptr);
  ASSERT_EQ(tsx->language_id, "tsx");
  ASSERT_TRUE(tsx->highlight_query_source.find("@tag") !=
              std::string::npos);
  ASSERT_TRUE(tsx->highlight_query_source.find("@tag.attribute") !=
              std::string::npos);
  ASSERT_TRUE(tsx->highlight_query_source.find(
                  "(jsx_attribute (jsx_namespace_name) @tag.attribute)") !=
              std::string::npos);
  ASSERT_TRUE(tsx->highlight_query_source.find("@type.builtin") !=
              std::string::npos);
}

TEST(TestTreeSitterLanguageDescriptorsCoverCatalog) {
  std::set<std::string> descriptor_names;
  for (const auto &spec : TreeSitterLanguageSpecs::all()) {
    ASSERT_TRUE(!spec.name.empty());
    ASSERT_EQ(spec.name, TreeSitterCatalog::normalize_language_name(spec.name));
    ASSERT_TRUE(descriptor_names.insert(spec.name).second);
  }

  for (const auto &entry : TreeSitterCatalog::entries()) {
    const auto *spec = TreeSitterLanguageSpecs::find(entry.name);
    ASSERT_TRUE(spec != nullptr);
    ASSERT_EQ(spec->name, entry.name);
    ASSERT_EQ(spec->url, entry.url);
    ASSERT_EQ(spec->source_subdir, entry.source_subdir);
    ASSERT_EQ(spec->extensions.size(), entry.extensions.size());
  }
}

TEST(TestThemeSyntaxPaletteFallsBackToReadableThemeColors) {
  Theme theme;
  theme.fg_default = 252;
  theme.bg_default = 234;
  theme.fg_keyword = 81;
  theme.bg_keyword = 234;
  theme.fg_number = 179;
  theme.bg_number = 234;
  theme.fg_type = 110;
  theme.bg_type = 234;

  theme.normalize_syntax_palette();

  ASSERT_EQ(theme.fg_variable, 252);
  ASSERT_EQ(theme.bg_variable, 234);
  ASSERT_EQ(theme.fg_parameter, 252);
  ASSERT_EQ(theme.fg_field, 252);
  ASSERT_EQ(theme.fg_punctuation, 252);
  ASSERT_EQ(theme.fg_operator, 81);
  ASSERT_EQ(theme.fg_tag, 81);
  ASSERT_EQ(theme.fg_constant, 179);
  ASSERT_EQ(theme.fg_builtin, 110);
  ASSERT_EQ(theme.fg_attribute, 110);
  ASSERT_EQ(theme.fg_namespace, 252);
  ASSERT_EQ(theme.fg_module, 252);
  ASSERT_EQ(theme.fg_keyword_control, 81);
  ASSERT_EQ(theme.fg_keyword_storage, 110);
  ASSERT_EQ(theme.fg_keyword_preproc, 179);
  ASSERT_EQ(theme.fg_function_method, theme.fg_function);
  ASSERT_EQ(theme.fg_function_constructor, 110);
  ASSERT_EQ(theme.fg_type_builtin, 110);
  ASSERT_EQ(theme.fg_constant_macro, 179);
  ASSERT_EQ(theme.fg_string_escape, 110);
  ASSERT_EQ(theme.fg_punctuation_bracket, 252);
  ASSERT_EQ(theme.fg_punctuation_delimiter, 252);
}

TEST(TestThemeSyntaxPaletteKeepsExplicitSyntaxSlots) {
  Theme theme;
  theme.fg_default = 252;
  theme.bg_default = 234;
  theme.fg_type = 110;
  theme.bg_type = 234;

  theme.fg_field = 203;
  theme.bg_field = 235;
  theme.mark_syntax_slot_explicit(SyntaxThemeSlot::Field);
  theme.fg_operator = 214;
  theme.mark_syntax_slot_explicit(SyntaxThemeSlot::Operator);
  theme.fg_keyword_control = 197;
  theme.mark_syntax_slot_explicit(SyntaxThemeSlot::KeywordControl);
  theme.fg_string_escape = 170;
  theme.mark_syntax_slot_explicit(SyntaxThemeSlot::StringEscape);

  theme.normalize_syntax_palette();

  ASSERT_EQ(theme.fg_field, 203);
  ASSERT_EQ(theme.bg_field, 235);
  ASSERT_EQ(theme.fg_operator, 214);
  ASSERT_EQ(theme.fg_keyword_control, 197);
  ASSERT_EQ(theme.fg_string_escape, 170);
  ASSERT_EQ(theme.fg_variable, 252);
  ASSERT_EQ(theme.fg_builtin, 110);
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

TEST(TestTreeSitterJsTsQueriesAvailableWhenParsersInstalled) {
  TreeSitterManager manager;

  for (const auto *ext : {".jsx", ".ts", ".tsx"}) {
    TSQuery *query = manager.get_highlight_query(ext);
    TreeSitterRuntimeStatus status =
        manager.runtime_status_for_extension(ext);
    if (status.parser_loaded) {
      ASSERT_TRUE(query != nullptr);
      ASSERT_TRUE(status.query_loaded);
    } else {
      ASSERT_TRUE(query == nullptr);
    }
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
