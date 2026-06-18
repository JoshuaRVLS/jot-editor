#include "tools/symbols/index.h"
#include <catch2/catch_test_macros.hpp>
#include "tools/workspace/search.h"

#include <vector>

TEST_CASE("Workspace Search Skips Heavy Folders", "[jot]") {
  REQUIRE(WorkspaceSearch::should_skip_path_component(".git"));
  REQUIRE(WorkspaceSearch::should_skip_path_component("node_modules"));
  REQUIRE(WorkspaceSearch::should_skip_path_component("build"));
  REQUIRE_FALSE(WorkspaceSearch::should_skip_path_component("src"));
}

TEST_CASE("Workspace Search Detects Binary Text", "[jot]") {
  REQUIRE(WorkspaceSearch::text_looks_binary(std::string("abc\0def", 7)));
  REQUIRE_FALSE(WorkspaceSearch::text_looks_binary("plain text"));
}

TEST_CASE("Symbol Index Python Symbols", "[jot]") {
  std::vector<std::string> lines = {
      "class Editor:",
      "    def render(self):",
      "        pass",
  };
  auto symbols = SymbolIndex::extract_document_symbols(lines, "app.py");
  REQUIRE((int)symbols.size() == 2);
  REQUIRE(symbols[0].name == "Editor");
  REQUIRE(symbols[0].kind == "class");
  REQUIRE(symbols[1].name == "render");
  REQUIRE(symbols[1].kind == "function");
}

TEST_CASE("Symbol Index C++ Function", "[jot]") {
  std::vector<std::string> lines = {
      "struct Buffer {",
      "};",
      "int render_buffer(const Buffer& buffer) {",
      "  return 0;",
      "}",
  };
  auto symbols = SymbolIndex::extract_document_symbols(lines, "render.cpp");
  REQUIRE((int)symbols.size() >= 2);
  REQUIRE(symbols[0].name == "Buffer");
  REQUIRE(symbols[1].name == "render_buffer");
}
