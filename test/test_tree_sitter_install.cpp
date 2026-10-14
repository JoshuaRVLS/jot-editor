#include "syntax/install.h"
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

namespace
{
  // Saves and restores an environment variable, so a test that forces a
  // platform or a toolchain never leaks it into the next one.
  struct EnvGuard
  {
    explicit EnvGuard(const char *name) : name_(name)
    {
      const char *value = std::getenv(name);
      had_ = value != nullptr;
      if (had_)
      {
        old_ = value;
      }
    }
    ~EnvGuard()
    {
      if (had_)
      {
        setenv(name_.c_str(), old_.c_str(), 1);
      }
      else
      {
        unsetenv(name_.c_str());
      }
    }
    EnvGuard(const EnvGuard &) = delete;
    EnvGuard &operator=(const EnvGuard &) = delete;
    std::string name_;
    std::string old_;
    bool had_ = false;
  };

  // The commit :tsinstall builds for cpp in this test. Real lockfile revisions
  // are 40-character hashes; the renderers must pass this through untouched, so
  // the value only has to be distinctive.
  const char *CPP_REVISION = "e5cea0ec884c5c3d2d1e41a741a66ce13da4d945";

  void register_install_metadata()
  {
    for (const auto &name : {"cpp", "javascript", "typescript", "tsx", "zig"})
    {
      std::string repo =
          name == std::string("typescript") || name == std::string("tsx") ? "typescript" : name;
      // cpp is pinned, zig deliberately is not: the URL-install path and the
      // registry's forks have no upstream revision, and they must still render
      // a script that clones and builds.
      const std::string revision = name == std::string("cpp") ? CPP_REVISION : "";
      TreeSitterInstall::register_language(
          {name,
           std::string("https://github.com/tree-sitter/tree-sitter-") + repo,
           name == std::string("typescript") || name == std::string("tsx") ? name : "",
           {std::string("libtree-sitter-") + name + ".so"},
           revision});
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
  // Which script gets generated follows the installer's platform, not the
  // host, so both renderers are covered from anywhere.
  EnvGuard platform("JOT_INSTALL_PLATFORM");
  setenv("JOT_INSTALL_PLATFORM", "linux", 1);
  auto cpp = TreeSitterInstall::command_for_language("cpp");
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

  // The pin: the clone is followed by a shallow fetch of the recorded commit
  // and a detached checkout of that commit. The checkout names the commit
  // rather than FETCH_HEAD, so a fetch that landed on nothing usable fails
  // under `set -e` instead of silently building the default branch.
  REQUIRE(TreeSitterInstall::has_revision("cpp"));
  REQUIRE(TreeSitterInstall::has_revision("CPP"));
  REQUIRE_FALSE(TreeSitterInstall::has_revision("zig"));
  REQUIRE(cpp.command.find("echo '[jot:treesitter] pin cpp'") != std::string::npos);
  REQUIRE(cpp.command.find("git -C \"$work\" fetch --depth 1 origin " + std::string(CPP_REVISION))
          != std::string::npos);
  REQUIRE(cpp.command.find("git -C \"$work\" checkout --detach " + std::string(CPP_REVISION))
          != std::string::npos);
  REQUIRE(cpp.command.find("checkout --detach FETCH_HEAD") == std::string::npos);
  // A server that refuses to fetch one commit by hash still gets a full fetch
  // of the tags, which is where the commit then comes from.
  REQUIRE(cpp.command.find("fetch --tags origin") != std::string::npos);
  REQUIRE(cpp.command.find("git clone --depth 1")
          < cpp.command.find("fetch --depth 1 origin " + std::string(CPP_REVISION)));
  REQUIRE(cpp.command.find("fetch --depth 1 origin " + std::string(CPP_REVISION))
          < cpp.command.find("[jot:treesitter] build cpp"));
  // An unpinned language keeps the plain clone-and-build script.
  auto unpinned = TreeSitterInstall::command_for_language("zig");
  REQUIRE(unpinned.command.find("[jot:treesitter] pin") == std::string::npos);
  REQUIRE(unpinned.command.find("fetch --depth 1 origin") == std::string::npos);

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
}

TEST_CASE("Tree Sitter Install Renders A cmd.exe Build On Windows", "[jot]")
{
  register_install_metadata();
  EnvGuard platform("JOT_INSTALL_PLATFORM");
  EnvGuard local_app_data("LOCALAPPDATA");
  EnvGuard app_data("APPDATA");
  EnvGuard user_profile("USERPROFILE");
  EnvGuard ts_prefix("JOT_TREESITTER_PREFIX");
  EnvGuard temp("TEMP");
  EnvGuard cxx("CXX");
  setenv("JOT_INSTALL_PLATFORM", "win", 1);
  setenv("LOCALAPPDATA", "/tmp/jot-ts-win", 1);
  setenv("TEMP", "/tmp/jot-ts-win", 1);
  unsetenv("JOT_TREESITTER_PREFIX");

  auto cpp = TreeSitterInstall::command_for_language("cpp");
  REQUIRE(cpp.supported);
  REQUIRE(cpp.language == "cpp");
  REQUIRE(cpp.command.find("github.com/tree-sitter/tree-sitter-cpp") != std::string::npos);
  REQUIRE(cpp.command.find("git clone --depth 1") != std::string::npos);
  // The parser lands where the runtime searches for it, as a DLL (the name
  // list the tree-sitter registry offers on Windows).
  REQUIRE(cpp.command.find("libtree-sitter-cpp.dll") != std::string::npos);
  REQUIRE(cpp.command.find("-Wl,--export-all-symbols") != std::string::npos);
  REQUIRE(cpp.command.find("-static-libstdc++") != std::string::npos);
  // parser.c is C99: the C driver compiles it, in both build variants.
  REQUIRE(cpp.command.find("-x c ") != std::string::npos);
  REQUIRE(cpp.command.find("gcc") != std::string::npos);
  // Markers, each on its own short line: the command is echoed into the
  // terminal it runs in, and a wrapped row must not start with a marker.
  REQUIRE(cpp.command.find("echo [jot:treesitter] start cpp") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] prefix ") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] clone cpp") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] build cpp") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] query cpp") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] success cpp") != std::string::npos);
  REQUIRE(cpp.command.find("echo [jot:treesitter] failed cpp exit=1") != std::string::npos);
  REQUIRE(cpp.command.find("treesitter") != std::string::npos);
  // The pin is fetched and checked out with cmd.exe lines (no for/f, which
  // spells its loop variable differently in a batch than on a command line),
  // and success is gated on the checkout having landed: a DLL left from an
  // earlier build must not report an update that never happened.
  REQUIRE(TreeSitterInstall::has_revision("cpp"));
  REQUIRE(cpp.command.find("echo [jot:treesitter] pin cpp") != std::string::npos);
  REQUIRE(cpp.command.find("jot-tree-sitter-cpp\" fetch --depth 1 origin "
                           + std::string(CPP_REVISION))
          != std::string::npos);
  REQUIRE(cpp.command.find("jot-tree-sitter-cpp\" checkout --detach " + std::string(CPP_REVISION)
                           + " && set \"_jot_pin=1\"")
          != std::string::npos);
  REQUIRE(cpp.command.find("if defined _jot_pin if exist") != std::string::npos);
  REQUIRE(cpp.command.find("for /f") == std::string::npos);
  // An unpinned language writes the same flag unconditionally, so the success
  // gate is not tied to a pin it does not have.
  auto unpinned = TreeSitterInstall::command_for_language("zig");
  REQUIRE(unpinned.command.find("[jot:treesitter] pin") == std::string::npos);
  REQUIRE(unpinned.command.find("set \"_jot_pin=1\"") != std::string::npos);
  REQUIRE(unpinned.command.find("if defined _jot_pin if exist") != std::string::npos);

  // Nothing POSIX may survive: no shell, no uname, no mktemp, no find.
  REQUIRE(cpp.command.find("$HOME") == std::string::npos);
  REQUIRE(cpp.command.find("uname") == std::string::npos);
  REQUIRE(cpp.command.find("mktemp") == std::string::npos);
  REQUIRE(cpp.command.find("set -e") == std::string::npos);
  REQUIRE(cpp.command.find("/bin/sh") == std::string::npos);

  // The explicit prefix overrides the LOCALAPPDATA default, and CC/CXX pick the
  // compiler when the environment names one.
  auto custom = TreeSitterInstall::command_for_language("cpp", "/tmp/jot-ts-out");
  REQUIRE(custom.command.find("/tmp/jot-ts-out") != std::string::npos);
  setenv("CXX", "clang++", 1);
  auto tooled = TreeSitterInstall::command_for_language("cpp");
  REQUIRE(tooled.command.find("clang++") != std::string::npos);

  // With no install root to write into there is nothing to generate, and the
  // reason says so instead of producing a script that cannot work.
  unsetenv("JOT_TREESITTER_PREFIX");
  unsetenv("LOCALAPPDATA");
  unsetenv("APPDATA");
  unsetenv("USERPROFILE");
  auto rootless = TreeSitterInstall::command_for_language("cpp");
  REQUIRE_FALSE(rootless.supported);
  REQUIRE(rootless.message.find("JOT_TREESITTER_PREFIX") != std::string::npos);
}
