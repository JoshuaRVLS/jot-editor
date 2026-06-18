#include <catch2/catch_test_macros.hpp>
#include "types.h"
#include "tree_sitter/catalog.h"
#include "tree_sitter/language_spec.h"
#include "tree_sitter/manager.h"
#include <set>
#include <string>

TEST_CASE("Tree Sitter Language Registration", "[jot]") {
  TreeSitterManager manager;

  REQUIRE(manager.has_language(".c"));
  REQUIRE(manager.has_language(".py"));
  REQUIRE(manager.has_language(".rs"));
  REQUIRE(manager.has_language(".md"));
  REQUIRE(manager.has_language(".rb"));
  REQUIRE(manager.has_language(".vue"));
  REQUIRE(manager.language_id_for_extension(".js") == "javascript");
  REQUIRE(manager.language_id_for_extension(".jsx") == "javascript");
  REQUIRE(manager.language_id_for_extension(".mjs") == "javascript");
  REQUIRE(manager.language_id_for_extension(".cjs") == "javascript");
  REQUIRE(manager.language_id_for_extension(".ts") == "typescript");
  REQUIRE(manager.language_id_for_extension(".mts") == "typescript");
  REQUIRE(manager.language_id_for_extension(".cts") == "typescript");
  REQUIRE(manager.language_id_for_extension(".tsx") == "tsx");
  REQUIRE_FALSE(manager.has_language(".unknown"));
}

TEST_CASE("Tree Sitter Runtime Overrides", "[jot]") {
  TreeSitterManager manager;
  manager.set_runtime_options({}, {}, {".foo:zig", "bar=python"});

  REQUIRE(manager.has_language(".foo"));
  REQUIRE(manager.language_id_for_extension(".foo") == "zig");
  REQUIRE(manager.has_language_override(".foo"));
  REQUIRE(manager.has_language(".bar"));
  REQUIRE(manager.language_id_for_extension(".bar") == "python");
  REQUIRE(manager.has_language_override(".bar"));
  REQUIRE_FALSE(manager.has_language_override(".py"));
}

TEST_CASE("Tree Sitter Header Override Can Select C++", "[jot]") {
  TreeSitterManager manager;
  REQUIRE(manager.language_id_for_extension(".h") == "c");
  REQUIRE_FALSE(manager.has_language_override(".h"));

  manager.set_runtime_options({}, {}, {".h:cpp"});
  REQUIRE(manager.language_id_for_extension(".h") == "cpp");
  REQUIRE(manager.has_language_override(".h"));
}

TEST_CASE("Tree Sitter Runtime Status Unsupported Extension", "[jot]") {
  TreeSitterManager manager;
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".unknown");

  REQUIRE_FALSE(status.has_language);
  REQUIRE_FALSE(status.parser_loaded);
  REQUIRE_FALSE(status.query_loaded);
  REQUIRE(status.parser_message.find("unsupported extension") !=
              std::string::npos);
}

TEST_CASE("C++ Query Covers Scoped And Primitive Tokens", "[jot]") {
  TreeSitterManager manager;
  const TSLanguageEntry *entry = manager.get_language(".cpp");

  REQUIRE(entry != nullptr);
  std::string query = entry->highlight_query_source;
  REQUIRE(query.find("(primitive_type) @type.builtin") !=
              std::string::npos);
  REQUIRE(query.find("@keyword.control") != std::string::npos);
  REQUIRE(query.find("@keyword.storage") != std::string::npos);
  REQUIRE(query.find("@keyword.directive") != std::string::npos);
  REQUIRE(query.find("@function.method") != std::string::npos);
  REQUIRE(query.find("@constant.macro") != std::string::npos);
  REQUIRE(query.find("@string.escape") != std::string::npos);
  REQUIRE(query.find("@punctuation.bracket") != std::string::npos);
  REQUIRE(query.find("@punctuation.delimiter") != std::string::npos);
  REQUIRE(query.find("(namespace_identifier) @namespace") !=
              std::string::npos);
  REQUIRE(query.find("qualified_identifier scope: (namespace_identifier) "
                         "@namespace") != std::string::npos);
  REQUIRE(query.find("(call_expression function: (qualified_identifier") !=
              std::string::npos);
}

TEST_CASE("Tree Sitter Capture Mapping Priority", "[jot]") {
  REQUIRE(tree_sitter_capture_color_for_name("keyword") == 1);
  REQUIRE(tree_sitter_capture_color_for_name("string") == 2);
  REQUIRE(tree_sitter_capture_color_for_name("comment") == 3);
  REQUIRE(tree_sitter_capture_color_for_name("number") == 4);
  REQUIRE(tree_sitter_capture_color_for_name("type") == 5);
  REQUIRE(tree_sitter_capture_color_for_name("function") == 6);
  REQUIRE(tree_sitter_capture_token_for_name("variable") == TS_TOKEN_VARIABLE);
  REQUIRE(tree_sitter_capture_token_for_name("variable.parameter") == TS_TOKEN_PARAMETER);
  REQUIRE(tree_sitter_capture_token_for_name("function.builtin") == TS_TOKEN_BUILTIN);
  REQUIRE(tree_sitter_capture_token_for_name("function.method") == TS_TOKEN_FUNCTION_METHOD);
  REQUIRE(tree_sitter_capture_token_for_name("function.constructor") == TS_TOKEN_FUNCTION_CONSTRUCTOR);
  REQUIRE(tree_sitter_capture_token_for_name("constant.builtin") == TS_TOKEN_BUILTIN);
  REQUIRE(tree_sitter_capture_token_for_name("constant.macro") == TS_TOKEN_CONSTANT_MACRO);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.control") == TS_TOKEN_KEYWORD_CONTROL);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.storage") == TS_TOKEN_KEYWORD_STORAGE);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.directive") == TS_TOKEN_KEYWORD_PREPROC);
  REQUIRE(tree_sitter_capture_token_for_name("operator") == TS_TOKEN_OPERATOR);
  REQUIRE(tree_sitter_capture_token_for_name("punctuation.bracket") == TS_TOKEN_PUNCTUATION_BRACKET);
  REQUIRE(tree_sitter_capture_token_for_name("punctuation.delimiter") == TS_TOKEN_PUNCTUATION_DELIMITER);
  REQUIRE(tree_sitter_capture_token_for_name("string.escape") == TS_TOKEN_STRING_ESCAPE);
  REQUIRE(tree_sitter_capture_token_for_name("tag.attribute") == TS_TOKEN_ATTRIBUTE);
  REQUIRE(tree_sitter_capture_token_for_name("type.builtin") == TS_TOKEN_TYPE_BUILTIN);
  REQUIRE(tree_sitter_capture_token_for_name("property") == TS_TOKEN_FIELD);
  REQUIRE(tree_sitter_capture_color_for_name("unknown.capture") == 0);

  REQUIRE(tree_sitter_capture_priority_for_name("comment") >
              tree_sitter_capture_priority_for_name("keyword"));
  REQUIRE(tree_sitter_capture_priority_for_name("string") >
              tree_sitter_capture_priority_for_name("function"));
  REQUIRE(tree_sitter_capture_priority_for_name("function") >
              tree_sitter_capture_priority_for_name("type"));
  REQUIRE(tree_sitter_capture_priority_for_name("tag.attribute") >
              tree_sitter_capture_priority_for_name("property"));
}

TEST_CASE("Tree Sitter Built In Queries Expose Rich Captures", "[jot]") {
  TreeSitterManager manager;

  const TSLanguageEntry *cpp = manager.get_language(".cpp");
  REQUIRE(cpp != nullptr);
  REQUIRE(cpp->highlight_query_source.find("@variable") !=
              std::string::npos);
  REQUIRE(cpp->highlight_query_source.find("@property") !=
              std::string::npos);

  const TSLanguageEntry *python = manager.get_language(".py");
  REQUIRE(python != nullptr);
  REQUIRE(python->highlight_query_source.find("@variable.parameter") !=
              std::string::npos);

  const TSLanguageEntry *json = manager.get_language(".json");
  REQUIRE(json != nullptr);
  REQUIRE(json->highlight_query_source.find("@property") !=
              std::string::npos);

  const TSLanguageEntry *javascript = manager.get_language(".jsx");
  REQUIRE(javascript != nullptr);
  REQUIRE(javascript->language_id == "javascript");
  REQUIRE(javascript->highlight_query_source.find("@tag") !=
              std::string::npos);
  REQUIRE(javascript->highlight_query_source.find("@tag.attribute") !=
              std::string::npos);
  REQUIRE(javascript->highlight_query_source.find(
                  "(jsx_attribute (jsx_namespace_name) @tag.attribute)") !=
              std::string::npos);
  REQUIRE(javascript->highlight_query_source.find("@function.method") !=
              std::string::npos);

  const TSLanguageEntry *typescript = manager.get_language(".ts");
  REQUIRE(typescript != nullptr);
  REQUIRE(typescript->highlight_query_source.find("@type.builtin") !=
              std::string::npos);
  REQUIRE(typescript->highlight_query_source.find("@variable.parameter") !=
              std::string::npos);

  const TSLanguageEntry *tsx = manager.get_language(".tsx");
  REQUIRE(tsx != nullptr);
  REQUIRE(tsx->language_id == "tsx");
  REQUIRE(tsx->highlight_query_source.find("@tag") !=
              std::string::npos);
  REQUIRE(tsx->highlight_query_source.find("@tag.attribute") !=
              std::string::npos);
  REQUIRE(tsx->highlight_query_source.find(
                  "(jsx_attribute (jsx_namespace_name) @tag.attribute)") !=
              std::string::npos);
  REQUIRE(tsx->highlight_query_source.find("@type.builtin") !=
              std::string::npos);
}

TEST_CASE("Tree Sitter Language Descriptors Cover Catalog", "[jot]") {
  std::set<std::string> descriptor_names;
  for (const auto &spec : TreeSitterLanguageSpecs::all()) {
    REQUIRE_FALSE(spec.name.empty());
    REQUIRE(spec.name == TreeSitterCatalog::normalize_language_name(spec.name));
    REQUIRE(descriptor_names.insert(spec.name).second);
  }

  for (const auto &entry : TreeSitterCatalog::entries()) {
    const auto *spec = TreeSitterLanguageSpecs::find(entry.name);
    REQUIRE(spec != nullptr);
    REQUIRE(spec->name == entry.name);
    REQUIRE(spec->url == entry.url);
    REQUIRE(spec->source_subdir == entry.source_subdir);
    REQUIRE(spec->extensions.size() == entry.extensions.size());
  }
}

TEST_CASE("Theme Syntax Palette Falls Back To Readable Theme Colors", "[jot]") {
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

  REQUIRE(theme.fg_variable == 252);
  REQUIRE(theme.bg_variable == 234);
  REQUIRE(theme.fg_parameter == 252);
  REQUIRE(theme.fg_field == 252);
  REQUIRE(theme.fg_punctuation == 252);
  REQUIRE(theme.fg_operator == 81);
  REQUIRE(theme.fg_tag == 81);
  REQUIRE(theme.fg_constant == 179);
  REQUIRE(theme.fg_builtin == 110);
  REQUIRE(theme.fg_attribute == 110);
  REQUIRE(theme.fg_namespace == 252);
  REQUIRE(theme.fg_module == 252);
  REQUIRE(theme.fg_keyword_control == 81);
  REQUIRE(theme.fg_keyword_storage == 110);
  REQUIRE(theme.fg_keyword_preproc == 179);
  REQUIRE(theme.fg_function_method == theme.fg_function);
  REQUIRE(theme.fg_function_constructor == 110);
  REQUIRE(theme.fg_type_builtin == 110);
  REQUIRE(theme.fg_constant_macro == 179);
  REQUIRE(theme.fg_string_escape == 110);
  REQUIRE(theme.fg_punctuation_bracket == 252);
  REQUIRE(theme.fg_punctuation_delimiter == 252);
}

TEST_CASE("Theme Syntax Palette Keeps Explicit Syntax Slots", "[jot]") {
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

  REQUIRE(theme.fg_field == 203);
  REQUIRE(theme.bg_field == 235);
  REQUIRE(theme.fg_operator == 214);
  REQUIRE(theme.fg_keyword_control == 197);
  REQUIRE(theme.fg_string_escape == 170);
  REQUIRE(theme.fg_variable == 252);
  REQUIRE(theme.fg_builtin == 110);
}

#ifdef JOT_TREESITTER
TEST_CASE("Tree Sitter Missing Parser Reports Diagnostic", "[jot]") {
  TreeSitterManager manager;
  manager.set_runtime_options({}, {}, {".missing:missing_language"});

  REQUIRE(manager.get_highlight_query(".missing") == nullptr);
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".missing");

  REQUIRE(status.has_language);
  REQUIRE(status.language_id == "missing_language");
  REQUIRE_FALSE(status.parser_loaded);
  REQUIRE(status.parser_message.find("parser not loaded") !=
              std::string::npos);
}

TEST_CASE("Tree Sitter Query Cache", "[jot]") {
  TreeSitterManager manager;

  TSQuery *first = manager.get_highlight_query(".c");
  TSQuery *second = manager.get_highlight_query(".c");
  if (first) {
    REQUIRE(first == second);
  } else {
    REQUIRE(second == nullptr);
  }
}

TEST_CASE("Tree Sitter C++ Query Available When Parser Installed", "[jot]") {
  TreeSitterManager manager;

  TSQuery *query = manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus status =
      manager.runtime_status_for_extension(".cpp");
  if (status.parser_loaded) {
    REQUIRE(query != nullptr);
    REQUIRE(status.query_loaded);
  } else {
    REQUIRE(query == nullptr);
  }
}

TEST_CASE("Tree Sitter JS TS Queries Available When Parsers Installed", "[jot]") {
  TreeSitterManager manager;

  for (const auto *ext : {".jsx", ".ts", ".tsx"}) {
    TSQuery *query = manager.get_highlight_query(ext);
    TreeSitterRuntimeStatus status =
        manager.runtime_status_for_extension(ext);
    if (status.parser_loaded) {
      REQUIRE(query != nullptr);
      REQUIRE(status.query_loaded);
    } else {
      REQUIRE(query == nullptr);
    }
  }
}

TEST_CASE("Tree Sitter Reload Reattempts Parser And Query", "[jot]") {
  TreeSitterManager manager;

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus before =
      manager.runtime_status_for_extension(".cpp");

  manager.reload();
  TreeSitterRuntimeStatus after_reload =
      manager.runtime_status_for_extension(".cpp");
  REQUIRE(after_reload.has_language);
  REQUIRE_FALSE(after_reload.parser_loaded);
  REQUIRE_FALSE(after_reload.query_loaded);

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus after_retry =
      manager.runtime_status_for_extension(".cpp");
  if (before.parser_loaded) {
    REQUIRE(after_retry.parser_loaded);
    REQUIRE(after_retry.query_loaded);
  }
}
#endif
