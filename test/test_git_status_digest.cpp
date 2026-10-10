// Git status polling (src/workspace/git_run.h, src/workspace/git.cpp).
//
// The poll runs every 1.5 s and used to re-parse the whole porcelain output
// into a path -> status map on every tick. On a repo with tens of thousands of
// dirty files that parse is tens of megabytes, and each task queue worker that
// ran one kept its own freed arena: RSS climbed from 180 MB towards 300 MB on
// the report that started this. The digest cases are pure; the editor case runs
// a real repo and checks the poll skips a parse only while the output is
// byte-for-byte the one the installed map was built from.
#include "base/shell_util.h"
#include "core/editor.h"
#include "git/git_run.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_git_status_test_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }
} // namespace

TEST_CASE("Git output digests are stable and sensitive to the bytes", "[jot][git]")
{
  const jot_git::OutputDigest first = jot_git::digest_command("echo one");
  const jot_git::OutputDigest again = jot_git::digest_command("echo one");
  REQUIRE(first.ok());
  REQUIRE(first.bytes >= 0);
  REQUIRE(first.hash == again.hash);
  REQUIRE(first.bytes == again.bytes);

  const jot_git::OutputDigest other = jot_git::digest_command("echo two");
  REQUIRE(other.hash != first.hash);

  // Seeded with the repo root: byte-identical output from two repositories
  // must not read as the same status.
  REQUIRE(jot_git::digest("/tmp/jot_digest_a", "status").hash
          != jot_git::digest("/tmp/jot_digest_b", "status").hash);
}

TEST_CASE("A status snapshot only matches its own output", "[jot][git]")
{
  const jot_git::OutputDigest current{42, 7, 0};
  const jot_git::StatusSnapshot snapshot{42, 7, true};
  REQUIRE(jot_git::status_snapshot_matches(snapshot, current));
  REQUIRE_FALSE(jot_git::status_snapshot_matches(jot_git::StatusSnapshot(), current));
  REQUIRE_FALSE(jot_git::status_snapshot_matches(snapshot, {42, 8, 0}));
  REQUIRE_FALSE(jot_git::status_snapshot_matches(snapshot, {43, 7, 0}));
  // A failed command is never "the same status": nothing was parsed from it.
  REQUIRE_FALSE(jot_git::status_snapshot_matches(snapshot, {42, 7, 1}));
}

TEST_CASE("An unchanged git status is not parsed twice", "[jot][git]")
{
  if (!shell_util::command_exists("git"))
  {
    SKIP("git is not installed");
  }
  seed_config_home();
  char dir[] = "/tmp/jot_git_status_refresh_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "one.txt");
    out << "one\n";
  }
  REQUIRE(std::system(("git -C " + shell_util::shell_quote(root.string()) + " init -q").c_str())
          == 0);

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  REQUIRE(e.git_untracked_count_for_test() == 1);
  const long long first = e.git_status_parse_count_for_test();
  REQUIRE(first >= 1);

  // Byte-for-byte the same porcelain output: the installed map already
  // describes it, so the poll has nothing to parse.
  e.refresh_git_status_for_test();
  REQUIRE(e.git_status_parse_count_for_test() == first);

  // A new untracked file changes the output; the poll has to notice.
  {
    std::ofstream out(root / "two.txt");
    out << "two\n";
  }
  e.refresh_git_status_for_test();
  REQUIRE(e.git_status_parse_count_for_test() == first + 1);
  REQUIRE(e.git_untracked_count_for_test() == 2);

  // Clearing drops the comparison baseline: the next poll parses again even
  // though the repo has not moved since it was last read.
  e.clear_git_status_for_test();
  REQUIRE(e.git_untracked_count_for_test() == 0);
  e.refresh_git_status_for_test();
  REQUIRE(e.git_status_parse_count_for_test() == first + 2);
  REQUIRE(e.git_untracked_count_for_test() == 2);
}
