#include "lua_bridge/lua_loader.h"
#include "lua_bridge/embedded_lua.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
  std::filesystem::path user_config_root()
  {
    namespace fs = std::filesystem;
    const char *home = std::getenv("HOME");
    const char *app = std::getenv("APPDATA");
    const char *cfg = std::getenv("JOT_CONFIG_HOME");
    if (cfg && *cfg)
      return fs::path(cfg);
    if (app && *app)
      return fs::path(app) / "jot";
    if (home && *home)
      return fs::path(home) / ".config" / "jot";
    return {};
  }

  // Disposable cache for runtime files that need to exist on disk (the
  // tree-sitter and lsp-installer Lua dofile their siblings, and queries are
  // .scm files). Unlike the config dir this is ours alone: contents are
  // refreshed from the embedded bytes whenever they differ, and deleting the
  // whole directory is always safe.
  std::filesystem::path user_cache_root()
  {
    namespace fs = std::filesystem;
    const char *cache = std::getenv("JOT_CACHE_HOME");
    if (cache && *cache)
      return fs::path(cache);
    const char *xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && *xdg)
      return fs::path(xdg) / "jot";
#ifdef _WIN32
    const char *local = std::getenv("LOCALAPPDATA");
    if (local && *local)
      return fs::path(local) / "jot" / "cache";
#else
    const char *home = std::getenv("HOME");
    if (home && *home)
      return fs::path(home) / ".cache" / "jot";
#endif
    return {};
  }

  std::string read_file(const std::filesystem::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    if (!in)
      return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  bool write_file(const std::filesystem::path &p, const std::string &content)
  {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
  }

  // FNV-1a 64-bit; fingerprints embedded content so cached / materialized
  // copies can be refreshed when the binary's bundled runtime changes.
  std::string content_hash(const unsigned char *data, size_t size)
  {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; i++)
    {
      h ^= data[i];
      h *= 1099511628211ULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return buf;
  }

  // Marker path for a legacy materialized copy: `lua/<rel>.embedded` holds
  // the hash of the embedded content the copy was materialized from. Presence
  // of the marker means "jot wrote this file" (safe to refresh); absence
  // means the user wrote it by hand (override, never touched).
  std::filesystem::path marker_path(const std::filesystem::path &copy)
  {
    return copy.string() + ".embedded";
  }

  std::string embedded_source(const std::string &rel_path)
  {
    size_t size = 0;
    const unsigned char *data = jot_embedded::find(rel_path.c_str(), &size);
    return (data && size > 0) ? std::string((const char *)data, size) : "";
  }

  bool write_marker(const std::filesystem::path &copy, const std::string &hash)
  {
    std::ofstream out(marker_path(copy), std::ios::binary | std::ios::trunc);
    if (!out)
      return false;
    out << hash;
    return static_cast<bool>(out);
  }
} // namespace

std::vector<std::filesystem::path> jot_lua_override_dirs()
{
  std::vector<std::filesystem::path> out;
  const std::filesystem::path user = user_config_root() / "lua";
  if (!user.empty() && std::filesystem::is_directory(user))
  {
    out.push_back(user);
  }
#ifdef JOT_LUA_SOURCE_DIR
  // Developer source dir outranks an installed copy: iteration never needs a
  // rebuild to see bundled-Lua edits, and a stale install copy cannot shadow
  // the current source.
  {
    const std::filesystem::path dev = std::filesystem::path(JOT_LUA_SOURCE_DIR);
    if (std::filesystem::is_directory(dev))
    {
      out.push_back(dev);
    }
  }
#endif
#ifdef JOT_DEFAULT_DATA_DIR
  {
    const std::filesystem::path inst = std::filesystem::path(JOT_DEFAULT_DATA_DIR) / "lua";
    if (std::filesystem::is_directory(inst))
    {
      out.push_back(inst);
    }
  }
#endif
#ifdef _WIN32
  // Installed layout fallback: <exe>\..\share\jot\lua. Per-user installers
  // unpack to arbitrary locations, so JOT_DEFAULT_DATA_DIR rarely matches.
  {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH)
    {
      const std::filesystem::path exe(buf);
      const std::filesystem::path rel = exe.parent_path().parent_path() / "share" / "jot" / "lua";
      if (std::filesystem::is_directory(rel))
      {
        out.push_back(rel);
      }
    }
  }
#endif
  return out;
}

std::vector<std::filesystem::path> jot_lua_candidate_paths(const std::string &rel_path)
{
  std::vector<std::filesystem::path> out;
  for (const auto &dir : jot_lua_override_dirs())
  {
    const std::filesystem::path p = dir / rel_path;
    if (std::filesystem::is_regular_file(p))
    {
      out.push_back(p);
    }
  }
  return out;
}

std::filesystem::path jot_lua_cache_path(const std::string &rel_path)
{
  const std::string embedded = embedded_source(rel_path);
  if (embedded.empty())
  {
    return {};
  }
  std::filesystem::path root = user_cache_root();
  if (root.empty())
  {
    return {};
  }
  std::error_code ec;
  const std::filesystem::path target = root / rel_path;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec)
  {
    return {};
  }
  // The cache is disposable: extract (or refresh) whenever the on-disk copy
  // differs from the embedded bytes. Never treats the cache as an override.
  if (!std::filesystem::is_regular_file(target) || read_file(target) != embedded)
  {
    if (!write_file(target, embedded))
    {
      return {};
    }
  }
  return target;
}

std::filesystem::path jot_lua_resolve_path(const std::string &rel_path)
{
  const auto candidates = jot_lua_candidate_paths(rel_path);
  if (!candidates.empty())
  {
    const std::filesystem::path first = candidates.front();

    // The developer source dir is the developer's working file: always win,
    // never refresh, never look past it.
    const auto is_dev_dir = [](const std::filesystem::path &p)
    {
#ifdef JOT_LUA_SOURCE_DIR
      return p.string().rfind(std::filesystem::path(JOT_LUA_SOURCE_DIR).string(), 0) == 0;
#else
      (void)p;
      return false;
#endif
    };
    if (is_dev_dir(first))
    {
      return first;
    }

    const std::string embedded = embedded_source(rel_path);
    const std::string on_disk = read_file(first);

    // Legacy migration: a marked copy is one jot materialized in the user
    // config dir (pre-cache era). Refresh it in place when the binary's
    // bundled runtime moved on, so old installs stay in sync. Unmarked files
    // are hand-written overrides and are never touched.
    if (!embedded.empty() && on_disk != embedded
        && std::filesystem::is_regular_file(marker_path(first)))
    {
      size_t size = 0;
      const unsigned char *data = jot_embedded::find(rel_path.c_str(), &size);
      if (write_file(first, embedded))
      {
        write_marker(first, (data && size > 0) ? content_hash(data, size) : "");
        return first;
      }
    }

    // The winning copy matches the embedded content but the dev source dir
    // carries an in-progress edit: prefer the dev working copy so iteration
    // on src/lua is not shadowed by a stale user/install copy.
    if (!embedded.empty() && on_disk == embedded)
    {
      for (size_t i = 1; i < candidates.size(); i++)
      {
        if (is_dev_dir(candidates[i]) && read_file(candidates[i]) != embedded)
        {
          return candidates[i];
        }
      }
    }

    return first;
  }

  // Nothing on disk anywhere: fall back to the cache dir, extracting the
  // embedded subtree (runtimes like tree-sitter and the lsp installer
  // dofile sibling files and read .scm queries, so they need real files).
  const std::string top = [&]()
  {
    const size_t slash = rel_path.find('/');
    return slash == std::string::npos ? rel_path : rel_path.substr(0, slash);
  }();
  const auto top_prefix = top + "/";
  for (const auto &name : jot_embedded::list_files())
  {
    if (name == top || name.rfind(top_prefix, 0) == 0)
    {
      jot_lua_cache_path(name);
    }
  }
  return jot_lua_cache_path(rel_path);
}