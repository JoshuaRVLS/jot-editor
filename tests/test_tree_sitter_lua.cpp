#include "tree_sitter/manager.h"
#include "tree_sitter/install.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

TEST_CASE("Lua Tree-sitter registry maps extensions") {
  TreeSitterManager manager;
  manager.register_language("cpp", {".cpp"});
  REQUIRE(manager.language_for_extension(".cpp") == "cpp");
  REQUIRE(manager.language_for_extension("cpp") == "cpp");
  REQUIRE(manager.language_for_extension("unknown") == "");
  REQUIRE(manager.register_language("Test Language", {".test"}));
  REQUIRE(manager.language_for_extension(".test") == "test_language");
}

TEST_CASE("Tree-sitter install metadata follows Lua language registration") {
  TreeSitterInstall::clear_languages();
  {
    TreeSitterManager manager;
    manager.register_language(
        "zig", {".zig"}, "", "https://github.com/tree-sitter-grammars/tree-sitter-zig",
        "", "tree_sitter_zig", {"libtree-sitter-zig.so"}, "");
    const auto &languages = TreeSitterInstall::supported_languages();
    REQUIRE(std::find(languages.begin(), languages.end(), "zig") != languages.end());
    auto command = TreeSitterInstall::command_for_language("zig");
    REQUIRE(command.supported);
    REQUIRE(command.command.find("tree-sitter-zig") != std::string::npos);
  }
  TreeSitterInstall::clear_languages();
  REQUIRE(TreeSitterInstall::supported_languages().empty());
}

TEST_CASE("Lua Tree-sitter handles reject invalid lifecycle operations") {
  TreeSitterManager manager;
  REQUIRE_FALSE(manager.delete_parser_handle(42));
  REQUIRE_FALSE(manager.delete_tree_handle(42));
  REQUIRE_FALSE(manager.delete_query_handle(42));
  std::string error;
  REQUIRE_FALSE(manager.set_query_source(".unknown", "(", error));
  REQUIRE_FALSE(error.empty());
  REQUIRE(manager.parse_handle(42, "text") == 0);

#ifdef JOT_TREESITTER
  if (manager.status(".cpp").parser_loaded) {
    TreeSitterHandle parser = manager.create_parser_handle(".cpp");
    REQUIRE(parser != 0);
    TreeSitterHandle tree = manager.parse_handle(parser, "int value = 1;");
    REQUIRE(tree != 0);
    TreeSitterHandle query = manager.compile_query_handle(".cpp", "(");
    REQUIRE(query == 0); // malformed query must not leak a native handle
    query = manager.compile_query_handle(".cpp", "(identifier) @variable");
    REQUIRE(query != 0);
    REQUIRE_FALSE(manager.captures_for_handles(query, tree).empty());
    REQUIRE(manager.delete_query_handle(query));
    REQUIRE(manager.delete_tree_handle(tree));
    REQUIRE(manager.delete_parser_handle(parser));
  }
#endif
}

TEST_CASE("Lua Tree-sitter registration accepts query overrides") {
  TreeSitterManager manager;
  REQUIRE(manager.register_language("example", {".example"}, ""));
  REQUIRE(manager.status(".example").language_id == "example");
  REQUIRE(manager.status("example").language_id == "example");
}

TEST_CASE("Lua query source is accepted before a parser is installed") {
  TreeSitterManager manager;
  REQUIRE(manager.register_language("cpp", {".cpp"}, ""));
  std::string error;
  // Regression: startup used to fail here when the parser library was not
  // installed yet, which disabled the language and made :tsinstall/:tsstatus
  // list nothing.
  REQUIRE(manager.set_query_source(".cpp", "(identifier) @variable", error));
  REQUIRE(manager.status(".cpp").language_id == "cpp");
  REQUIRE_FALSE(manager.set_query_source(".unknown", "(identifier) @variable", error));
  REQUIRE_FALSE(error.empty());
}
