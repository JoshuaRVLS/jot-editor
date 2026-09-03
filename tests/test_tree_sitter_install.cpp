#include "tree_sitter/install.h"
#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
  void register_install_metadata()
  {
    for (const auto &name : {"cpp", "javascript", "typescript", "tsx", "zig"})
    {
      std::string repo =
          name == std::string("typescript") || name == std::string("tsx") ? "typescript" : name;
      TreeSitterInstall::register_language(
          {name,
           std::string("https://github.com/tree-sitter/tree-sitter-") + repo,
           name == std::string("typescript") || name == std::string("tsx") ? name : "",
           {std::string("libtree-sitter-") + name + ".so"}});
    }
  }
} // namespace

TEST_CASE("Tree Sitter Install Language Validation", "[jot]")
{
  register_install_metadata();
  REQUIRE(TreeSitterInstall::is_supported_language("cpp"));
  REQUIRE(TreeSitterInstall::is_supported_language("CPP"));
  REQUIRE(TreeSitterInstall::is_supported_language("jsx"));
  REQUIRE(
      TreeSitterInstall::is_supported_language("https://github.com/tree-sitter/tree-sitter-zig"));
  REQUIRE(
      TreeSitterInstall::is_supported_language("github.com/tree-sitter-grammars/tree-sitter-zig"));
  REQUIRE_FALSE(TreeSitterInstall::is_supported_language("unknown"));
}

TEST_CASE("Tree Sitter Install Command Mapping", "[jot]")
{
  register_install_metadata();
  auto cpp = TreeSitterInstall::command_for_language("cpp");
#ifdef _WIN32
  REQUIRE_FALSE(cpp.supported);
  REQUIRE(cpp.message.find("not implemented on Windows") != std::string::npos);
#else
  REQUIRE(cpp.supported);
  REQUIRE(cpp.language == "cpp");
  REQUIRE(cpp.command.find("github.com/tree-sitter/tree-sitter-cpp") != std::string::npos);
  REQUIRE(cpp.command.find("/parsers") != std::string::npos);
  REQUIRE(cpp.command.find("XDG_DATA_HOME") != std::string::npos);
  REQUIRE(cpp.command.find("queries/cpp") != std::string::npos);
  REQUIRE(cpp.command.find("install root is not writable") != std::string::npos);
  REQUIRE(cpp.command.find("objdir=\"$work/.jot-build\"") != std::string::npos);
  REQUIRE(cpp.command.find("set -- \"$@\" \"$objdir/parser.o\"") != std::string::npos);
  REQUIRE(cpp.command.find("$cxx \"$linkflag\" \"$@\"") != std::string::npos);
  REQUIRE(cpp.command.find("linkflag=-dynamiclib") != std::string::npos);
  REQUIRE(cpp.command.find("linkflag=-shared") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] start cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] clone cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] build cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] link cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] query cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] success cpp") != std::string::npos);
  REQUIRE(cpp.command.find("[jot:treesitter] failed cpp exit=$rc") != std::string::npos);
  REQUIRE(cpp.command.find("find \"$work/queries\"") != std::string::npos);
  REQUIRE(cpp.command.find("-name '*.scm'") != std::string::npos);

  auto javascript = TreeSitterInstall::command_for_language("javascript");
  REQUIRE(javascript.supported);
  REQUIRE(javascript.language == "javascript");
  REQUIRE(javascript.command.find("github.com/tree-sitter/tree-sitter-javascript")
          != std::string::npos);
  auto override_root = TreeSitterInstall::command_for_language("cpp", "/tmp/jot-ts");
  REQUIRE(override_root.command.find("prefix='/tmp/jot-ts'") != std::string::npos);

  auto jsx = TreeSitterInstall::command_for_language("jsx");
  REQUIRE(jsx.supported);
  REQUIRE(jsx.language == "javascript");
  REQUIRE(jsx.command.find("github.com/tree-sitter/tree-sitter-javascript") != std::string::npos);

  auto typescript = TreeSitterInstall::command_for_language("typescript");
  REQUIRE(typescript.supported);
  REQUIRE(typescript.command.find("github.com/tree-sitter/tree-sitter-typescript")
          != std::string::npos);
  REQUIRE(typescript.command.find("typescript/src") != std::string::npos);

  auto tsx = TreeSitterInstall::command_for_language("tsx");
  REQUIRE(tsx.supported);
  REQUIRE(tsx.language == "tsx");
  REQUIRE(tsx.command.find("github.com/tree-sitter/tree-sitter-typescript") != std::string::npos);
  REQUIRE(tsx.command.find("tsx/src") != std::string::npos);

  auto zig = TreeSitterInstall::command_for_language(
      "https://github.com/tree-sitter-grammars/tree-sitter-zig");
  REQUIRE(zig.supported);
  REQUIRE(zig.language == "zig");
  REQUIRE(zig.command.find("tree-sitter-zig") != std::string::npos);

  auto zig_short =
      TreeSitterInstall::command_for_language("github.com/tree-sitter-grammars/tree-sitter-zig");
  REQUIRE(zig_short.supported);
  REQUIRE(zig_short.command.find("https://github.com/tree-sitter-grammars/tree-sitter-zig")
          != std::string::npos);

  auto bad = TreeSitterInstall::command_for_language("unknown");
  REQUIRE_FALSE(bad.supported);
  REQUIRE(bad.message.find("Unsupported Tree-sitter language") != std::string::npos);
#endif
}
