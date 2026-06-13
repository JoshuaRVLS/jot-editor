#include "test_framework.h"
#include "tree_sitter_install.h"

#include <string>

TEST(TestTreeSitterInstallLanguageValidation) {
  ASSERT_TRUE(TreeSitterInstall::is_supported_language("cpp"));
  ASSERT_TRUE(TreeSitterInstall::is_supported_language("CPP"));
  ASSERT_TRUE(TreeSitterInstall::is_supported_language(
      "https://github.com/tree-sitter/tree-sitter-zig"));
  ASSERT_TRUE(TreeSitterInstall::is_supported_language(
      "github.com/tree-sitter-grammars/tree-sitter-zig"));
  ASSERT_TRUE(!TreeSitterInstall::is_supported_language("unknown"));
}

TEST(TestTreeSitterInstallCommandMapping) {
  auto cpp = TreeSitterInstall::command_for_language("cpp");
  ASSERT_TRUE(cpp.supported);
  ASSERT_EQ(cpp.language, "cpp");
  ASSERT_TRUE(cpp.command.find("github.com/tree-sitter/tree-sitter-cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("lib/jot/tree-sitter") != std::string::npos);
  ASSERT_TRUE(cpp.command.find("objdir=\"$work/.jot-build\"") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("set -- \"$@\" \"$objdir/parser.o\"") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("$cxx \"$linkflag\" \"$@\"") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("linkflag=-dynamiclib") != std::string::npos);
  ASSERT_TRUE(cpp.command.find("linkflag=-shared") != std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] start cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] clone cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] build cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] link cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] query cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] success cpp") !=
              std::string::npos);
  ASSERT_TRUE(cpp.command.find("[jot:treesitter] failed cpp exit=$rc") !=
              std::string::npos);

  auto typescript = TreeSitterInstall::command_for_language("typescript");
  ASSERT_TRUE(typescript.supported);
  ASSERT_TRUE(typescript.command.find("github.com/tree-sitter/tree-sitter-typescript") !=
              std::string::npos);
  ASSERT_TRUE(typescript.command.find("typescript/src") != std::string::npos);

  auto tsx = TreeSitterInstall::command_for_language("tsx");
  ASSERT_TRUE(tsx.supported);
  ASSERT_EQ(tsx.language, "tsx");
  ASSERT_TRUE(tsx.command.find("github.com/tree-sitter/tree-sitter-typescript") !=
              std::string::npos);
  ASSERT_TRUE(tsx.command.find("tsx/src") != std::string::npos);

  auto zig = TreeSitterInstall::command_for_language(
      "https://github.com/tree-sitter-grammars/tree-sitter-zig");
  ASSERT_TRUE(zig.supported);
  ASSERT_EQ(zig.language, "zig");
  ASSERT_TRUE(zig.command.find("tree-sitter-zig") != std::string::npos);

  auto zig_short = TreeSitterInstall::command_for_language(
      "github.com/tree-sitter-grammars/tree-sitter-zig");
  ASSERT_TRUE(zig_short.supported);
  ASSERT_TRUE(zig_short.command.find(
                  "https://github.com/tree-sitter-grammars/tree-sitter-zig") !=
              std::string::npos);

  auto bad = TreeSitterInstall::command_for_language("unknown");
  ASSERT_TRUE(!bad.supported);
  ASSERT_TRUE(bad.message.find("Unsupported Tree-sitter language") !=
              std::string::npos);
}
