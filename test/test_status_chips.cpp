// The statusline's right-hand chips after they were trimmed
// (src/render/status_line.cpp): the git chip is the branch alone, and the LSP
// chip names the language instead of "cpp @ /path". The git case runs a real
// repository with exactly one file in each state, so a change count that crept
// back into the chip would paint "+1 ~1 ?1" on the row and fail here. The LSP
// chip only exists while a server runs, which is a pty run and not a unit test;
// what is pinned here is the pair of labels it is drawn from -- the language for
// the bar, the root for the log lines.
#include "base/shell_util.h"
#include "editor.h"
#include "tools/lsp/client.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_status_chips_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // The painted status row as text: the bar is the last row of the grid.
  std::string status_row(Editor &e)
  {
    UI *ui = e.ui_for_test();
    std::string row;
    const int y = e.ui_height_for_test() - 1;
    for (int x = 0; x < e.ui_width_for_test(); x++)
    {
      const UICell *cell = ui->cell_at(x, y);
      row += cell ? cell->ch : " ";
    }
    return row;
  }

  // The git branch glyph the chip leads with (nf-oct-git_branch).
  const char *kGitGlyph = "\uE0A0";
} // namespace

TEST_CASE("Status bar: the git chip is the branch, without the change counts",
          "[jot][statusline][git]")
{
  if (!shell_util::command_exists("git"))
  {
    SKIP("git is not installed");
  }
  seed_config_home();
  char dir[] = "/tmp/jot_status_chips_repo_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "tracked.txt");
    out << "one\n";
  }
  const std::string q = shell_util::shell_quote(root.string());
  REQUIRE(std::system(("git -C " + q + " init -q").c_str()) == 0);
  REQUIRE(std::system(("git -C " + q + " add tracked.txt").c_str()) == 0);
  REQUIRE(
      std::system(("git -C " + q + " -c user.email=t@t -c user.name=t commit -q -m init").c_str())
      == 0);
  // One file in each state, so a count that came back would read +1 ~1 ?1.
  {
    std::ofstream out(root / "tracked.txt");
    out << "two\n";
  }
  {
    std::ofstream out(root / "staged.txt");
    out << "staged\n";
  }
  REQUIRE(std::system(("git -C " + q + " add staged.txt").c_str()) == 0);
  {
    std::ofstream out(root / "untracked.txt");
    out << "untracked\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  REQUIRE_FALSE(e.git_branch_for_test().empty());
  REQUIRE(e.git_staged_count_for_test() == 1);
  REQUIRE(e.git_unstaged_count_for_test() == 1);
  REQUIRE(e.git_untracked_count_for_test() == 1);

  e.apply_resize_for_test(120, 30);
  e.render_for_test();
  const std::string row = status_row(e);
  REQUIRE(row.find(e.git_branch_for_test()) != std::string::npos);
  REQUIRE(row.find(" +1") == std::string::npos);
  REQUIRE(row.find(" ~1") == std::string::npos);
  REQUIRE(row.find(" ?1") == std::string::npos);
  REQUIRE(row.find(kGitGlyph) != std::string::npos);
}

TEST_CASE("Status bar: the LSP chip label is the language, the root stays in describe()",
          "[jot][statusline][lsp]")
{
  LSPClient client("cpp", "/tmp/jot_status_chips_root", {});
  // What the chip paints.
  REQUIRE(client.get_language() == "cpp");
  // What the log lines and the format message keep wanting.
  REQUIRE(client.describe() == "cpp @ /tmp/jot_status_chips_root");
}
