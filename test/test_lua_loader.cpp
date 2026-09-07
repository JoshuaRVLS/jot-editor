// Headless tests of the bundled-runtime loader (src/jot/lua/lua_loader.cpp).
// Pins the override contract:
//   - the user config dir is user territory: unmarked files always win and
//     are never rewritten (the original stale-copy bug),
//   - `.embedded`-marked copies are legacy jot materializations and get
//     refreshed when the binary's bundled runtime moves on,
//   - the cache dir is disposable: embedded files are extracted there when
//     nothing exists on disk, and refresh whenever content differs,
//   - the developer source dir is the fallback when no config dir exists.
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <fstream>
#include <string>

#include "jot/lua/embedded_lua.h"
#include "jot/lua/lua_loader.h"

namespace fs = std::filesystem;

namespace
{
  // Scope-restricted env mutation: restores the previous value (or unsets)
  // when the scope ends so other tests never see the override.
  struct EnvGuard
  {
    explicit EnvGuard(const char *name, const std::string &value) : name_(name)
    {
      const char *old = std::getenv(name);
      if (old)
      {
        had_old_ = true;
        old_ = old;
      }
      setenv(name, value.c_str(), 1);
    }
    ~EnvGuard()
    {
      if (had_old_)
        setenv(name_.c_str(), old_.c_str(), 1);
      else
        unsetenv(name_.c_str());
    }
    std::string name_;
    std::string old_;
    bool had_old_ = false;
  };

  struct TmpRoot
  {
    fs::path config;
    fs::path cache;
    fs::path work;
    TmpRoot()
    {
      std::random_device rd;
      work = fs::temp_directory_path()
             / ("jot_loader_test_" + std::to_string(rd()));
      fs::remove_all(work);
      config = work / "config";
      cache = work / "cache";
      fs::create_directories(config / "lua");
      fs::create_directories(cache);
    }
    ~TmpRoot() { fs::remove_all(work); }
  };

  std::string read_file(const fs::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    if (!in)
      return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  void write_file(const fs::path &p, const std::string &content)
  {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
  }

  // Content of the same bundled file in the developer source dir (the tests
  // are compiled with JOT_LUA_SOURCE_DIR, and the embedded bytes are
  // byte-identical to it — pinned by test_lua_ui_kit).
  std::string bundled_content(const std::string &rel)
  {
    return read_file(fs::path(JOT_LUA_SOURCE_DIR) / rel);
  }
} // namespace

TEST_CASE("lua loader: cache extraction into a disposable cache dir")
{
  TmpRoot tmp;
  EnvGuard cfg("JOT_CONFIG_HOME", tmp.config.string());
  EnvGuard cache("JOT_CACHE_HOME", tmp.cache.string());

  // Nothing on disk: extraction lands in the cache dir, never the config dir.
  const fs::path p = jot_lua_cache_path("features/ui.lua");
  REQUIRE_FALSE(p.empty());
  REQUIRE(p.string().rfind(tmp.cache.string(), 0) == 0);
  REQUIRE(read_file(p) == bundled_content("features/ui.lua"));
  REQUIRE(fs::is_empty(tmp.config / "lua"));

  // Idempotent: a second call does not rewrite (mtime stable is hard to
  // assert portably; content equality + success is the contract).
  REQUIRE(jot_lua_cache_path("features/ui.lua") == p);

  // A stale cache copy is refreshed from the embedded bytes.
  write_file(p, "-- OLD STALE CACHE COPY");
  const fs::path again = jot_lua_cache_path("features/ui.lua");
  REQUIRE(again == p);
  REQUIRE(read_file(p) == bundled_content("features/ui.lua"));

  // Unknown files are not embedded: empty path, nothing written.
  REQUIRE(jot_lua_cache_path("nope/missing.lua").empty());
  REQUIRE_FALSE(fs::exists(tmp.cache / "nope"));
}

TEST_CASE("lua loader: unmarked user override wins and is never touched")
{
  TmpRoot tmp;
  EnvGuard cfg("JOT_CONFIG_HOME", tmp.config.string());
  EnvGuard cache("JOT_CACHE_HOME", tmp.cache.string());

  const fs::path override_file = tmp.config / "lua" / "features" / "ui.lua";
  write_file(override_file, "-- MY HAND-WRITTEN OVERRIDE\n");

  const fs::path resolved = jot_lua_resolve_path("features/ui.lua");
  REQUIRE(resolved == override_file);
  // The override differs from the bundled content but has no marker: it must
  // survive resolution byte-for-byte (the regression this pins: a stale copy
  // used to be clobbered or shadow the bundled update).
  REQUIRE(read_file(override_file) == "-- MY HAND-WRITTEN OVERRIDE\n");
  REQUIRE_FALSE(fs::exists(override_file.string() + ".embedded"));
}

TEST_CASE("lua loader: marked legacy copies are refreshed when stale")
{
  TmpRoot tmp;
  EnvGuard cfg("JOT_CONFIG_HOME", tmp.config.string());
  EnvGuard cache("JOT_CACHE_HOME", tmp.cache.string());

  const fs::path copy = tmp.config / "lua" / "features" / "ui.lua";
  write_file(copy, "-- OLD MATERIALIZED COPY (PRE-CACHE ERA)");
  write_file(copy.string() + ".embedded", "deadbeef00000000"); // stale marker

  const fs::path resolved = jot_lua_resolve_path("features/ui.lua");
  REQUIRE(resolved == copy);
  REQUIRE(read_file(copy) == bundled_content("features/ui.lua"));
  REQUIRE(fs::is_regular_file(copy.string() + ".embedded"));

  // A marked copy that already matches the embedded bytes is left alone.
  const fs::path fresh = tmp.config / "lua" / "features" / "hover.lua";
  write_file(fresh, bundled_content("features/hover.lua"));
  write_file(fresh.string() + ".embedded", "whatever");
  const fs::path resolved_fresh = jot_lua_resolve_path("features/hover.lua");
  REQUIRE(resolved_fresh == fresh);
  REQUIRE(read_file(fresh) == bundled_content("features/hover.lua"));
}

TEST_CASE("lua loader: developer source dir is the no-config fallback")
{
  TmpRoot tmp;
  EnvGuard cfg("JOT_CONFIG_HOME", tmp.config.string());
  EnvGuard cache("JOT_CACHE_HOME", tmp.cache.string());
  fs::remove_all(tmp.config); // no user config dir at all

  const fs::path resolved = jot_lua_resolve_path("features/ui.lua");
  REQUIRE_FALSE(resolved.empty());
  REQUIRE(resolved.string().rfind(fs::path(JOT_LUA_SOURCE_DIR).string(), 0) == 0);
  REQUIRE(read_file(resolved) == bundled_content("features/ui.lua"));
}

TEST_CASE("lua loader: unknown files resolve to nothing")
{
  TmpRoot tmp;
  EnvGuard cfg("JOT_CONFIG_HOME", tmp.config.string());
  EnvGuard cache("JOT_CACHE_HOME", tmp.cache.string());

  REQUIRE(jot_lua_resolve_path("nope/missing.lua").empty());
}

TEST_CASE("lua loader: embedded index carries tree-sitter queries")
{
  // The embed step must carry the .scm query files too — the treesitter
  // runtime reads them from disk via runtime_path.
  size_t size = 0;
  const unsigned char *q = jot_embedded::find("treesitter/queries/lua/highlights.scm", &size);
  REQUIRE(q != nullptr);
  REQUIRE(size > 0);
  bool saw_scm = false;
  for (const auto &name : jot_embedded::list_files())
  {
    if (name.rfind("treesitter/queries/", 0) == 0 && name.size() > 4
        && name.substr(name.size() - 4) == ".scm")
    {
      saw_scm = true;
    }
  }
  REQUIRE(saw_scm);
}