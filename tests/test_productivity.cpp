#include "symbol_index.h"
#include "test_framework.h"
#include "workspace_search.h"

#include <vector>

TEST(TestWorkspaceSearchSkipsHeavyFolders) {
  ASSERT_TRUE(WorkspaceSearch::should_skip_path_component(".git"));
  ASSERT_TRUE(WorkspaceSearch::should_skip_path_component("node_modules"));
  ASSERT_TRUE(WorkspaceSearch::should_skip_path_component("build"));
  ASSERT_TRUE(!WorkspaceSearch::should_skip_path_component("src"));
}

TEST(TestWorkspaceSearchDetectsBinaryText) {
  ASSERT_TRUE(WorkspaceSearch::text_looks_binary(std::string("abc\0def", 7)));
  ASSERT_TRUE(!WorkspaceSearch::text_looks_binary("plain text"));
}

TEST(TestSymbolIndexPythonSymbols) {
  std::vector<std::string> lines = {
      "class Editor:",
      "    def render(self):",
      "        pass",
  };
  auto symbols = SymbolIndex::extract_document_symbols(lines, "app.py");
  ASSERT_EQ((int)symbols.size(), 2);
  ASSERT_EQ(symbols[0].name, "Editor");
  ASSERT_EQ(symbols[0].kind, "class");
  ASSERT_EQ(symbols[1].name, "render");
  ASSERT_EQ(symbols[1].kind, "function");
}

TEST(TestSymbolIndexCppFunction) {
  std::vector<std::string> lines = {
      "struct Buffer {",
      "};",
      "int render_buffer(const Buffer& buffer) {",
      "  return 0;",
      "}",
  };
  auto symbols = SymbolIndex::extract_document_symbols(lines, "render.cpp");
  ASSERT_TRUE((int)symbols.size() >= 2);
  ASSERT_EQ(symbols[0].name, "Buffer");
  ASSERT_EQ(symbols[1].name, "render_buffer");
}
