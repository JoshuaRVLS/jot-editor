// :tsupdate, the half of the install flow that rebuilds what is already there.
//
// Two things have to hold at once: an update only touches parsers this editor
// can find, and it only runs for a language whose grammar revision is pinned.
// A grammar nvim-treesitter does not ship (the registry carries a few forks of
// its own) has no revision to move back to, so :tsupdate has to say that rather
// than quietly rebuilding its default branch and calling it an update.
//
// The shell side of the pin -- clone, fetch, checkout, and the script's own
// success marker -- is a pty probe (test/treesitter_pin_probe.py); these cases
// cover the decision that comes before any job is queued.
#include "core/editor.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace
{
  namespace fs = std::filesystem;

  // HOME-shaped roots for one case: they decide where an installed parser is
  // looked for, so a case can stage one (or stage none) without touching the
  // machine's real install root.
  std::string seed_roots()
  {
    char tmpl[] = "/tmp/jot_tsupdate_XXXXXX";
    const char *dir = mkdtemp(tmpl);
    REQUIRE(dir != nullptr);
    const std::string root = dir;
    setenv("JOT_TREESITTER_PREFIX", (root + "/ts").c_str(), 1);
    setenv("JOT_CONFIG_HOME", root.c_str(), 1);
    setenv("JOT_CACHE_HOME", root.c_str(), 1);
    setenv("XDG_DATA_HOME", (root + "/data").c_str(), 1);
    setenv("XDG_CACHE_HOME", (root + "/cache").c_str(), 1);
    return root;
  }

  // A parser library at the path the manager searches, which is all "installed"
  // means to :tsupdate. Its bytes never matter: nothing loads it here.
  void stage_parser(const std::string &root, const std::string &language)
  {
    const fs::path dir = fs::path(root) / "ts" / "parsers";
    fs::create_directories(dir);
    std::ofstream out((dir / ("libtree-sitter-" + language + ".so")).string());
    out << "not a real library\n";
  }
} // namespace

TEST_CASE("tsupdate refuses a parser that is not installed", "[jot]")
{
  const std::string root = seed_roots();
  Editor editor;
  REQUIRE(editor.register_tree_sitter_language_for_test("pinnedlang", std::string(40, 'a')));

  REQUIRE_FALSE(editor.tree_sitter_parser_installed_for_test("pinnedlang"));
  REQUIRE_FALSE(editor.update_tree_sitter_languages_for_test("pinnedlang"));
  REQUIRE(editor.message_for_test().find("is not installed") != std::string::npos);
  REQUIRE(editor.message_for_test().find(":tsinstall pinnedlang") != std::string::npos);

  // Nothing installed means a bare :tsupdate has nothing to do either, and it
  // says so instead of reporting a queue it never filled.
  REQUIRE_FALSE(editor.update_tree_sitter_languages_for_test(""));
  REQUIRE(editor.message_for_test().find("No installed Tree-sitter parser to update")
          != std::string::npos);
}

TEST_CASE("tsupdate refuses a grammar with no pinned revision", "[jot]")
{
  const std::string root = seed_roots();
  stage_parser(root, "unpinnedlang");
  Editor editor;
  // Installed, but the registry has no upstream revision for it: :tsinstall
  // would clone a default branch, so there is no earlier revision to return to.
  REQUIRE(editor.register_tree_sitter_language_for_test("unpinnedlang", ""));

  REQUIRE(editor.tree_sitter_parser_installed_for_test("unpinnedlang"));
  REQUIRE_FALSE(editor.update_tree_sitter_languages_for_test("unpinnedlang"));
  REQUIRE(editor.message_for_test().find("has no pinned revision") != std::string::npos);
  REQUIRE(editor.message_for_test().find(":tsinstall unpinnedlang") != std::string::npos);
}

TEST_CASE("tsupdate sees an installed parser on disk", "[jot]")
{
  const std::string root = seed_roots();
  stage_parser(root, "pinnedlang");
  Editor editor;
  REQUIRE(editor.register_tree_sitter_language_for_test("pinnedlang", std::string(40, 'b')));
  REQUIRE(editor.register_tree_sitter_language_for_test("otherlang", std::string(40, 'c')));

  // "Installed" is the parser library being in a search root, not the language
  // being registered: otherlang is registered with no library and stays out of
  // the list a bare :tsupdate walks.
  REQUIRE(editor.tree_sitter_parser_installed_for_test("pinnedlang"));
  REQUIRE_FALSE(editor.tree_sitter_parser_installed_for_test("otherlang"));
  REQUIRE(editor.tree_sitter_installed_language_count_for_test() == 1);
}
