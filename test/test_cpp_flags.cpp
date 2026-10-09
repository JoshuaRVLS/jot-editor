// The compile flags clangd is given for a C/C++ workspace
// (src/syntax/cpp_flags.*).
//
// These cases pin the cascade, because each step is a decision about how much
// to trust the flags on disk: a database beside the sources must be left to
// clangd's own search (interpolation between a header and a nearby source
// included), a database under the build directory must be pointed at, and a
// workspace with none must get a generated one rather than nothing -- the
// "nothing" case being the one that made a C++20 project read as a syntax
// error.
#include "syntax/cpp_flags.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
// A scratch workspace, gone when the case ends. The generated databases are
// sent to a scratch data directory too, so a test never writes into the one the
// user's servers are installed under.
struct Scratch
{
  fs::path root;
  fs::path data;
  std::string old_data_home;
  bool had_data_home = false;

  Scratch()
  {
    char workspace[] = "/tmp/jot_cpp_flags_tree_XXXXXX";
    root = mkdtemp(workspace);
    char data_dir[] = "/tmp/jot_cpp_flags_data_XXXXXX";
    data = mkdtemp(data_dir);
    const char *previous = std::getenv("XDG_DATA_HOME");
    had_data_home = previous != nullptr;
    if (had_data_home)
    {
      old_data_home = previous;
    }
    setenv("XDG_DATA_HOME", data.string().c_str(), 1);
  }

  ~Scratch()
  {
    // Put XDG_DATA_HOME back before removing the directory it named. Leaving
    // it behind pointed every later test that resolves a data root -- the
    // tree-sitter parser install root among them, read from XDG_DATA_HOME
    // first -- at a path this destructor had just deleted, which silently
    // turned every grammar lookup into "not installed" and skipped those
    // cases depending on the run order.
    if (had_data_home)
    {
      setenv("XDG_DATA_HOME", old_data_home.c_str(), 1);
    }
    else
    {
      unsetenv("XDG_DATA_HOME");
    }
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::remove_all(data, ec);
  }

  Scratch(const Scratch &) = delete;
  Scratch &operator=(const Scratch &) = delete;

  std::string write(const std::string &relative, const std::string &content) const
  {
    const fs::path path = root / relative;
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
    out.close();
    return fs::absolute(path).string();
  }

  std::string path_of(const std::string &relative) const
  {
    return (root / relative).string();
  }
};

std::string read_file(const fs::path &path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string joined(const std::vector<std::string> &arguments)
{
  std::string out;
  for (const auto &argument : arguments)
  {
    if (!out.empty())
    {
      out += " ";
    }
    out += argument;
  }
  return out;
}
} // namespace

TEST_CASE("clangd flags: a database beside the sources is left to clangd", "[cpp_flags]")
{
  Scratch scratch;
  scratch.write("compile_commands.json", "[]\n");
  const std::string file = scratch.write("app/main.cpp", "int main() { return 0; }\n");

  // The file is passed as the editor would have it: the caller wants arguments
  // for this file, not for the workspace in the abstract.
  REQUIRE(CppFlags::clangd_args_for(scratch.root.string(), file).empty());

  // A compile_flags.txt counts as flags too: a project that carries one is not
  // handed a generated database on top of it.
  Scratch second;
  second.write("compile_flags.txt", "-std=gnu++23\n");
  const std::string other = second.write("main.cpp", "int main() { return 0; }\n");
  REQUIRE(CppFlags::clangd_args_for(second.root.string(), other).empty());
}

TEST_CASE("clangd flags: a database in the build directory is pointed at", "[cpp_flags]")
{
  const Scratch scratch;
  scratch.write("build/compile_commands.json", "[]\n");
  const std::string file = scratch.write("src/main.cpp", "int main() { return 0; }\n");

  const auto arguments = CppFlags::clangd_args_for(scratch.root.string(), file);

  REQUIRE(arguments.size() == 1);
  REQUIRE(arguments.front() == "--compile-commands-dir=" + scratch.path_of("build"));
  // Nothing is generated when the project already has a database.
  REQUIRE_FALSE(fs::exists(fs::path(CppFlags::fallback_database_dir(scratch.root.string()))
                           / "compile_commands.json"));
}

TEST_CASE("clangd flags: the newest build directory wins", "[cpp_flags]")
{
  const Scratch scratch;
  scratch.write("out/compile_commands.json", "[]\n");
  scratch.write("build/compile_commands.json", "[]\n");
  const std::string file = scratch.write("src/main.cpp", "int main() { return 0; }\n");

  // A tree usually has more than one; the one configured last is the one in
  // use, so the stamps are set explicitly rather than relying on write order.
  std::error_code ec;
  const auto now = fs::file_time_type::clock::now();
  fs::last_write_time(scratch.root / "out" / "compile_commands.json", now - std::chrono::hours(48), ec);
  fs::last_write_time(scratch.root / "build" / "compile_commands.json", now, ec);

  const auto arguments = CppFlags::clangd_args_for(scratch.root.string(), file);
  REQUIRE(arguments.size() == 1);
  REQUIRE(arguments.front() == "--compile-commands-dir=" + scratch.path_of("build"));
}

TEST_CASE("clangd flags: a workspace with no database gets a generated one", "[cpp_flags]")
{
  const Scratch scratch;
  const std::string app = scratch.write("src/app.cpp", "int main() { return 0; }\n");
  scratch.write("src/widget.h", "#pragma once\n");
  scratch.write("src/helper.c", "int helper(void) { return 1; }\n");
  // Output directories and hidden ones are not sources: a database listing them
  // would describe the build tree rather than the project.
  scratch.write("build/stale.cpp", "int stale() { return 0; }\n");
  scratch.write(".git/objects/odd.cpp", "int odd() { return 0; }\n");

  const auto arguments = CppFlags::clangd_args_for(scratch.root.string(), app);

  REQUIRE(arguments.size() == 1);
  const std::string directory = CppFlags::fallback_database_dir(scratch.root.string());
  REQUIRE(arguments.front() == "--compile-commands-dir=" + directory);
  // Written under the data directory, so the project itself is untouched.
  REQUIRE(directory.find(scratch.data.string()) == 0);
  REQUIRE_FALSE(directory.find(scratch.root.string()) == 0);

  const fs::path database = fs::path(directory) / "compile_commands.json";
  REQUIRE(fs::is_regular_file(database));
  const std::string text = read_file(database);

  REQUIRE(text.find("\"file\": \"" + scratch.path_of("src/app.cpp") + "\"") != std::string::npos);
  REQUIRE(text.find("\"file\": \"" + scratch.path_of("src/widget.h") + "\"") != std::string::npos);
  REQUIRE(text.find("\"file\": \"" + scratch.path_of("src/helper.c") + "\"") != std::string::npos);
  REQUIRE(text.find("stale.cpp") == std::string::npos);
  REQUIRE(text.find("odd.cpp") == std::string::npos);

  // The standard follows the file's own language: a `.c` file handed gnu++20
  // is a hard error from clang, not a warning.
  const size_t c_entry = text.find("helper.c");
  REQUIRE(text.find("-std=gnu17", c_entry) != std::string::npos);
  REQUIRE(text.find("-std=gnu++20") != std::string::npos);
  // ...and the workspace's own include directories come with it. The quotes
  // around a path are escaped in the JSON, and a path with a space in it is
  // otherwise split into two arguments inside the command.
  REQUIRE(text.find("-I\\\"" + scratch.root.string() + "\\\"") != std::string::npos);
  REQUIRE(text.find("-I\\\"" + scratch.path_of("src") + "\\\"") != std::string::npos);
}

TEST_CASE("clangd flags: a file opened later is added, not re-scanned away", "[cpp_flags]")
{
  const Scratch scratch;
  const std::string first = scratch.write("src/app.cpp", "int main() { return 0; }\n");

  REQUIRE(CppFlags::clangd_args_for(scratch.root.string(), first).size() == 1);
  // Written after the scan, so this is the append path a file created during
  // the session takes.
  const std::string second = scratch.write("src/later.cpp", "int later() { return 1; }\n");
  const fs::path database =
      fs::path(CppFlags::fallback_database_dir(scratch.root.string())) / "compile_commands.json";
  const std::string before = read_file(database);

  REQUIRE(CppFlags::clangd_args_for(scratch.root.string(), second).size() == 1);
  const std::string after = read_file(database);

  // Both files, once each, and the first entry is still exactly where it was.
  REQUIRE(after.find("\"file\": \"" + first + "\"") != std::string::npos);
  REQUIRE(after.find("\"file\": \"" + second + "\"") != std::string::npos);
  REQUIRE(after.size() > before.size());
  REQUIRE(after.find(first) == before.find(first));
  REQUIRE(std::count(after.begin(), after.end(), '{') == 2);

  // Asking again for the same file leaves the database alone.
  REQUIRE(CppFlags::clangd_args_for(scratch.root.string(), second).size() == 1);
  REQUIRE(read_file(database) == after);
}
