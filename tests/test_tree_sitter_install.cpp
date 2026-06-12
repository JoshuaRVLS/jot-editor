#include "test_framework.h"
#include "tree_sitter_install.h"

#include <string>

TEST(TestTreeSitterInstallLanguageValidation) {
  ASSERT_TRUE(TreeSitterInstall::is_supported_language("cpp"));
  ASSERT_TRUE(TreeSitterInstall::is_supported_language("CPP"));
  ASSERT_TRUE(!TreeSitterInstall::is_supported_language("unknown"));
}

TEST(TestTreeSitterInstallCommandMapping) {
  auto cpp = TreeSitterInstall::command_for_language("cpp");
  ASSERT_TRUE(cpp.supported);
  ASSERT_EQ(cpp.language, "cpp");
  ASSERT_TRUE(cpp.command.find("tree-sitter-cpp") != std::string::npos);
  ASSERT_TRUE(cpp.command.find("Rebuild and restart jot") !=
              std::string::npos);

  auto bad = TreeSitterInstall::command_for_language("unknown");
  ASSERT_TRUE(!bad.supported);
  ASSERT_TRUE(bad.message.find("Unsupported Tree-sitter language") !=
              std::string::npos);
}
