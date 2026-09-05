#include "tree_sitter/manager.h"
#include "types.h"
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>

namespace
{
  void register_test_languages(TreeSitterManager &manager)
  {
    const std::string cpp = "(primitive_type) @type.builtin\n(keyword) @keyword.control\n"
                            "@keyword.storage @keyword.directive @function.method "
                            "@constant.macro @string.escape @punctuation.bracket "
                            "@punctuation.delimiter (namespace_identifier) @namespace "
                            "qualified_identifier scope: (namespace_identifier) @namespace "
                            "(call_expression function: (qualified_identifier) @function) "
                            "@variable @property";
    manager.register_language("c", {".c", ".h"});
    manager.register_language("cpp",
                              {".cpp", ".hpp", ".cc", ".cxx", ".hh", ".hxx"},
                              cpp,
                              "https://github.com/tree-sitter/tree-sitter-cpp",
                              "",
                              "tree_sitter_cpp",
                              {"libtree-sitter-cpp.so"});
    manager.register_language("javascript",
                              {".js", ".jsx", ".mjs", ".cjs"},
                              "@tag @tag.attribute @function.method\n"
                              "(jsx_attribute (jsx_namespace_name) @tag.attribute)");
    manager.register_language(
        "typescript", {".ts", ".mts", ".cts"}, "@type.builtin @variable.parameter");
    manager.register_language("tsx",
                              {".tsx"},
                              "@tag @tag.attribute @type.builtin\n"
                              "(jsx_attribute (jsx_namespace_name) @tag.attribute)");
    manager.register_language("python", {".py", ".pyw"}, "@variable.parameter");
    manager.register_language("json", {".json", ".jsonc"}, "@property");
    manager.register_language("rust", {".rs"});
    manager.register_language("go", {".go"});
    manager.register_language("markdown", {".md", ".markdown"});
    manager.register_language("ruby", {".rb"});
    manager.register_language("vue", {".vue"});
    manager.set_runtime_options({}, {std::string(JOT_LUA_SOURCE_DIR) + "/treesitter"}, {});
  }
} // namespace

TEST_CASE("Tree Sitter Language Registration", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

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

TEST_CASE("Tree Sitter Runtime Overrides", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);
  manager.set_runtime_options({}, {}, {".foo:zig", "bar=python"});

  REQUIRE(manager.has_language(".foo"));
  REQUIRE(manager.language_id_for_extension(".foo") == "zig");
  REQUIRE(manager.has_language_override(".foo"));
  REQUIRE(manager.has_language(".bar"));
  REQUIRE(manager.language_id_for_extension(".bar") == "python");
  REQUIRE(manager.has_language_override(".bar"));
  REQUIRE_FALSE(manager.has_language_override(".py"));
}

TEST_CASE("Tree Sitter Header Override Can Select C++", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);
  REQUIRE(manager.language_id_for_extension(".h") == "c");
  REQUIRE_FALSE(manager.has_language_override(".h"));

  manager.set_runtime_options({}, {}, {".h:cpp"});
  REQUIRE(manager.language_id_for_extension(".h") == "cpp");
  REQUIRE(manager.has_language_override(".h"));
}

TEST_CASE("Tree Sitter Runtime Status Unsupported Extension", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);
  TreeSitterRuntimeStatus status = manager.runtime_status_for_extension(".unknown");

  REQUIRE_FALSE(status.has_language);
  REQUIRE_FALSE(status.parser_loaded);
  REQUIRE_FALSE(status.query_loaded);
  REQUIRE(status.parser_message.find("unsupported extension") != std::string::npos);
}

TEST_CASE("C++ Query Covers Scoped And Primitive Tokens", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);
  const TSLanguageEntry *entry = manager.get_language(".cpp");

  REQUIRE(entry != nullptr);
  std::string query = entry->highlight_query_source;
  REQUIRE(query.find("(primitive_type) @type.builtin") != std::string::npos);
  REQUIRE(query.find("@keyword.control") != std::string::npos);
  REQUIRE(query.find("@keyword.storage") != std::string::npos);
  REQUIRE(query.find("@keyword.directive") != std::string::npos);
  REQUIRE(query.find("@function.method") != std::string::npos);
  REQUIRE(query.find("@constant.macro") != std::string::npos);
  REQUIRE(query.find("@string.escape") != std::string::npos);
  REQUIRE(query.find("@punctuation.bracket") != std::string::npos);
  REQUIRE(query.find("@punctuation.delimiter") != std::string::npos);
  REQUIRE(query.find("(namespace_identifier) @namespace") != std::string::npos);
  REQUIRE(query.find("qualified_identifier scope: (namespace_identifier) "
                     "@namespace")
          != std::string::npos);
  REQUIRE(query.find("(call_expression function: (qualified_identifier") != std::string::npos);
}

TEST_CASE("Tree Sitter Capture Mapping Priority", "[jot]")
{
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
  REQUIRE(tree_sitter_capture_token_for_name("function.constructor")
          == TS_TOKEN_FUNCTION_CONSTRUCTOR);
  REQUIRE(tree_sitter_capture_token_for_name("constant.builtin") == TS_TOKEN_BUILTIN);
  REQUIRE(tree_sitter_capture_token_for_name("constant.macro") == TS_TOKEN_CONSTANT_MACRO);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.control") == TS_TOKEN_KEYWORD_CONTROL);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.storage") == TS_TOKEN_KEYWORD_STORAGE);
  REQUIRE(tree_sitter_capture_token_for_name("keyword.directive") == TS_TOKEN_KEYWORD_PREPROC);
  REQUIRE(tree_sitter_capture_token_for_name("operator") == TS_TOKEN_OPERATOR);
  REQUIRE(tree_sitter_capture_token_for_name("punctuation.bracket")
          == TS_TOKEN_PUNCTUATION_BRACKET);
  REQUIRE(tree_sitter_capture_token_for_name("punctuation.delimiter")
          == TS_TOKEN_PUNCTUATION_DELIMITER);
  REQUIRE(tree_sitter_capture_token_for_name("string.escape") == TS_TOKEN_STRING_ESCAPE);
  REQUIRE(tree_sitter_capture_token_for_name("tag.attribute") == TS_TOKEN_ATTRIBUTE);
  REQUIRE(tree_sitter_capture_token_for_name("type.builtin") == TS_TOKEN_TYPE_BUILTIN);
  REQUIRE(tree_sitter_capture_token_for_name("property") == TS_TOKEN_FIELD);
  REQUIRE(tree_sitter_capture_color_for_name("unknown.capture") == 0);

  REQUIRE(tree_sitter_capture_priority_for_name("comment")
          > tree_sitter_capture_priority_for_name("keyword"));
  REQUIRE(tree_sitter_capture_priority_for_name("string")
          > tree_sitter_capture_priority_for_name("function"));
  REQUIRE(tree_sitter_capture_priority_for_name("function")
          > tree_sitter_capture_priority_for_name("type"));
  REQUIRE(tree_sitter_capture_priority_for_name("tag.attribute")
          > tree_sitter_capture_priority_for_name("property"));
}

TEST_CASE("Tree Sitter Built In Queries Expose Rich Captures", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

  const TSLanguageEntry *cpp = manager.get_language(".cpp");
  REQUIRE(cpp != nullptr);
  REQUIRE(cpp->highlight_query_source.find("@variable") != std::string::npos);
  REQUIRE(cpp->highlight_query_source.find("@property") != std::string::npos);

  const TSLanguageEntry *python = manager.get_language(".py");
  REQUIRE(python != nullptr);
  REQUIRE(python->highlight_query_source.find("@variable.parameter") != std::string::npos);

  const TSLanguageEntry *json = manager.get_language(".json");
  REQUIRE(json != nullptr);
  REQUIRE(json->highlight_query_source.find("@property") != std::string::npos);

  const TSLanguageEntry *javascript = manager.get_language(".jsx");
  REQUIRE(javascript != nullptr);
  REQUIRE(javascript->language_id == "javascript");
  REQUIRE(javascript->highlight_query_source.find("@tag") != std::string::npos);
  REQUIRE(javascript->highlight_query_source.find("@tag.attribute") != std::string::npos);
  REQUIRE(
      javascript->highlight_query_source.find("(jsx_attribute (jsx_namespace_name) @tag.attribute)")
      != std::string::npos);
  REQUIRE(javascript->highlight_query_source.find("@function.method") != std::string::npos);

  const TSLanguageEntry *typescript = manager.get_language(".ts");
  REQUIRE(typescript != nullptr);
  REQUIRE(typescript->highlight_query_source.find("@type.builtin") != std::string::npos);
  REQUIRE(typescript->highlight_query_source.find("@variable.parameter") != std::string::npos);

  const TSLanguageEntry *tsx = manager.get_language(".tsx");
  REQUIRE(tsx != nullptr);
  REQUIRE(tsx->language_id == "tsx");
  REQUIRE(tsx->highlight_query_source.find("@tag") != std::string::npos);
  REQUIRE(tsx->highlight_query_source.find("@tag.attribute") != std::string::npos);
  REQUIRE(tsx->highlight_query_source.find("(jsx_attribute (jsx_namespace_name) @tag.attribute)")
          != std::string::npos);
  REQUIRE(tsx->highlight_query_source.find("@type.builtin") != std::string::npos);
}

TEST_CASE("Theme Syntax Palette Falls Back To Readable Theme Colors", "[jot]")
{
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

TEST_CASE("Theme Syntax Palette Keeps Explicit Syntax Slots", "[jot]")
{
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
TEST_CASE("Tree Sitter Missing Parser Reports Diagnostic", "[jot]")
{
  TreeSitterManager manager;
  manager.set_runtime_options({}, {}, {".missing:missing_language"});

  REQUIRE(manager.get_highlight_query(".missing") == nullptr);
  TreeSitterRuntimeStatus status = manager.runtime_status_for_extension(".missing");

  REQUIRE(status.has_language);
  REQUIRE(status.language_id == "missing_language");
  REQUIRE_FALSE(status.parser_loaded);
  REQUIRE(status.parser_message.find("parser") != std::string::npos);
}

TEST_CASE("Tree Sitter Query Cache", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

  TSQuery *first = manager.get_highlight_query(".c");
  TSQuery *second = manager.get_highlight_query(".c");
  if (first)
  {
    REQUIRE(first == second);
  }
  else
  {
    REQUIRE(second == nullptr);
  }
}

TEST_CASE("Tree Sitter C++ Query Available When Parser Installed", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

  TSQuery *query = manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus status = manager.runtime_status_for_extension(".cpp");
  if (status.parser_loaded)
  {
    REQUIRE(query != nullptr);
    REQUIRE(status.query_loaded);
  }
  else
  {
    REQUIRE(query == nullptr);
  }
}

TEST_CASE("Tree Sitter JS TS Queries Available When Parsers Installed", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

  for (const auto *ext : {".jsx", ".ts", ".tsx"})
  {
    TSQuery *query = manager.get_highlight_query(ext);
    TreeSitterRuntimeStatus status = manager.runtime_status_for_extension(ext);
    if (status.parser_loaded)
    {
      REQUIRE(query != nullptr);
      REQUIRE(status.query_loaded);
    }
    else
    {
      REQUIRE(query == nullptr);
    }
  }
}

TEST_CASE("Tree Sitter Reload Reattempts Parser And Query", "[jot]")
{
  TreeSitterManager manager;
  register_test_languages(manager);

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus before = manager.runtime_status_for_extension(".cpp");

  manager.reload();
  register_test_languages(manager);
  TreeSitterRuntimeStatus after_reload = manager.runtime_status_for_extension(".cpp");
  REQUIRE(after_reload.has_language);
  REQUIRE_FALSE(after_reload.parser_loaded);
  REQUIRE_FALSE(after_reload.query_loaded);

  (void)manager.get_highlight_query(".cpp");
  TreeSitterRuntimeStatus after_retry = manager.runtime_status_for_extension(".cpp");
  if (before.parser_loaded)
  {
    REQUIRE(after_retry.parser_loaded);
    REQUIRE(after_retry.query_loaded);
  }
}
#endif

// The bracket-depth prefix cache (types.h FileBuffer) keeps absolute depth
// entries valid up to bracket_depth_prefix_upto and mark_edited(anchor)
// must drop only the entries at/after the edited line: entries before an
// edit stay valid because depth at the start of a line depends only on
// earlier lines. If truncation over- or under-shoots, rainbow bracket
// colors drift or go stale after edits.
TEST_CASE("FileBuffer bracket depth prefix truncates at the edit anchor", "[jot]")
{
  FileBuffer buf;
  // Simulate a prefix built for a 6-line buffer: entry i is the depth at
  // the start of line i, valid for i <= upto (vector size == upto + 1).
  buf.bracket_depth_prefix = {0, 1, 2, 3, 4, 5, 6};
  buf.bracket_depth_prefix_upto = 6;

  // Edit at line 2: entries 0..2 stay valid, 3.. are dropped.
  buf.mark_edited(2);
  REQUIRE(buf.bracket_depth_prefix_upto == 2);
  REQUIRE(buf.bracket_depth_prefix.size() == 3);
  REQUIRE(buf.bracket_depth_prefix[0] == 0);
  REQUIRE(buf.bracket_depth_prefix[1] == 1);
  REQUIRE(buf.bracket_depth_prefix[2] == 2);

  // Edit at line 2 again (prefix only covers lines 0..2): nothing drops.
  buf.mark_edited(2);
  REQUIRE(buf.bracket_depth_prefix_upto == 2);
  REQUIRE(buf.bracket_depth_prefix.size() == 3);

  // Edit at line 5 (below the built prefix): still nothing to drop.
  buf.mark_edited(5);
  REQUIRE(buf.bracket_depth_prefix_upto == 2);
  REQUIRE(buf.bracket_depth_prefix.size() == 3);

  // Unknown anchor (0): full truncation back to the always-valid entry 0.
  buf.mark_edited(0);
  REQUIRE(buf.bracket_depth_prefix_upto == 0);
  REQUIRE(buf.bracket_depth_prefix.size() == 1);
  REQUIRE(buf.bracket_depth_prefix[0] == 0);

  // replace_lines anchors at its start line.
  buf.bracket_depth_prefix = {0, 1, 2, 3, 4, 5, 6};
  buf.bracket_depth_prefix_upto = 6;
  buf.replace_lines(3, 1, {"int replaced;"});
  REQUIRE(buf.bracket_depth_prefix_upto == 3);
  REQUIRE(buf.bracket_depth_prefix.size() == 4);
}
