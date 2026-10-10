// Tests for the exe-relative install layout (src/base/exe_path.h).
//
// A release archive is unpacked wherever the user wants it and the Windows
// installer writes into a directory the build machine cannot know, so "where is
// the data tree" has to be answered from the binary's own location rather than
// from the prefix compiled into JOT_DEFAULT_DATA_DIR. Pinned here:
//   - the installed layout wins: <exe>/../share/jot for <root>/bin/jot,
//   - a flattened tree is still found: <exe>/share/jot,
//   - nothing beside the binary stays nothing, so the callers fall through to
//     the embedded runtime / compiled-in prefix / built-in defaults instead of
//     reading a directory that is not there,
//   - the running binary can name its own directory on every platform the
//     suite runs on.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "base/exe_path.h"
#include "lua/lua_loader.h"

namespace fs = std::filesystem;

namespace
{
  // A scratch root, removed with the test. Not mkdtemp: this only ever needs
  // directories, and the name has to be unique without touching the file
  // system first.
  struct TmpTree
  {
    fs::path root;
    TmpTree()
    {
      std::random_device rd;
      root = fs::temp_directory_path() / ("jot_exe_path_test_" + std::to_string(rd()));
      fs::remove_all(root);
      fs::create_directories(root);
    }
    ~TmpTree() { fs::remove_all(root); }
  };
} // namespace

TEST_CASE("exe path: the installed layout is found from the binary's directory")
{
  TmpTree tree;
  const fs::path bin = tree.root / "bin";
  fs::create_directories(bin);
  fs::create_directories(tree.root / "share" / "jot" / "configs" / "colors");

  REQUIRE(exe_path::data_dir_from(bin) == tree.root / "share" / "jot");
}

TEST_CASE("exe path: a data tree beside the binary is found too")
{
  TmpTree tree;
  fs::create_directories(tree.root / "share" / "jot" / "lua");

  // The binary sits in the tree root rather than in bin/: same data, one less
  // level, and a copy that was flattened by hand still works.
  REQUIRE(exe_path::data_dir_from(tree.root) == tree.root / "share" / "jot");
}

TEST_CASE("exe path: the installed layout outranks one beside the binary")
{
  TmpTree tree;
  const fs::path bin = tree.root / "bin";
  fs::create_directories(bin / "share" / "jot");
  fs::create_directories(tree.root / "share" / "jot");

  REQUIRE(exe_path::data_dir_from(bin) == tree.root / "share" / "jot");
}

TEST_CASE("exe path: no data tree beside the binary is no answer")
{
  TmpTree tree;
  const fs::path bin = tree.root / "bin";
  fs::create_directories(bin);

  REQUIRE(exe_path::data_dir_from(bin).empty());
  REQUIRE(exe_path::data_dir_from(fs::path()).empty());

  // A directory of the same shape but the wrong name is not the data tree.
  fs::create_directories(tree.root / "share" / "jot-real");
  REQUIRE(exe_path::data_dir_from(bin).empty());
}

TEST_CASE("exe path: the running binary names its own directory")
{
  const fs::path dir = exe_path::dir();

  REQUIRE_FALSE(dir.empty());
  REQUIRE(fs::is_directory(dir));
}

TEST_CASE("exe path: the runtime tree beside the binary is what the loader loads from")
{
  // The end-to-end half of the contract: staging the released layout beside a
  // real executable has to make the Lua loader read the staged copy. The test
  // binary is the only executable a test can stand in for, so the tree is
  // staged beside it (`<exe>/../share/jot`) and removed again.
  const fs::path exe_dir = exe_path::dir();
  REQUIRE_FALSE(exe_dir.empty());

  const fs::path data = exe_dir.parent_path() / "share" / "jot";
  const std::string leaf = "exe_path_staged_probe.lua";
  const fs::path staged = data / "lua" / leaf;

  // Refuse to clobber something already at that path rather than deleting it
  // afterwards: a directory that is not ours is not ours to remove.
  REQUIRE_FALSE(fs::exists(staged));
  fs::create_directories(staged.parent_path());
  {
    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << "-- staged by test_exe_path.cpp\n";
  }

  const fs::path resolved = jot_lua_resolve_path(leaf);
  const bool found = fs::is_regular_file(resolved);
  std::error_code ec;
  fs::remove(staged, ec);
  fs::remove(staged.parent_path(), ec);
  fs::remove(data, ec);
  fs::remove(data.parent_path(), ec);

  REQUIRE(resolved == staged);
  REQUIRE(found);
}
