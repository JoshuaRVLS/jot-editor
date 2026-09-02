#include "tree_sitter/manager.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Lua Tree-sitter registry maps extensions") {
  TreeSitterManager manager;
  manager.register_language("cpp", {".cpp"});
  REQUIRE(manager.language_for_extension(".cpp") == "cpp");
  REQUIRE(manager.language_for_extension("cpp") == "cpp");
  REQUIRE(manager.language_for_extension("unknown") == "");
  REQUIRE(manager.register_language("Test Language", {".test"}));
  REQUIRE(manager.language_for_extension(".test") == "test_language");
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
