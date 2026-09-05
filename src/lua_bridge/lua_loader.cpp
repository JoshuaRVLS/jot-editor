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

  std::string read_file(const std::filesystem::path &p)
  {
    std::ifstream in(p, std::ios::binary);
    if (!in)
      return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }

  // FNV-1a 64-bit; used to fingerprint embedded content so materialized
  // copies can be refreshed when the binary's bundled lua changes.
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

  // Marker path for a materialized copy: `lua/<rel>.embedded` holds the hash
  // of the embedded content the copy was materialized from. Presence of the
  // marker means "jot wrote this file" (refreshable cache); absence means the
  // user wrote it by hand (override, never touched).
  std::filesystem::path marker_path(const std::filesystem::path &copy)
  {
    return copy.string() + ".embedded";
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
#ifdef JOT_DEFAULT_DATA_DIR
  {
    const std::filesystem::path inst = std::filesystem::path(JOT_DEFAULT_DATA_DIR) / "lua";
    if (std::filesystem::is_directory(inst))
    {
      out.push_back(inst);
    }
  }
#endif
#ifdef JOT_LUA_SOURCE_DIR
  {
    const std::filesystem::path dev = std::filesystem::path(JOT_LUA_SOURCE_DIR);
    if (std::filesystem::is_directory(dev))
    {
      out.push_back(dev);
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

std::filesystem::path jot_lua_resolve_path(const std::string &rel_path)
{
  const auto candidates = jot_lua_candidate_paths(rel_path);

  size_t size = 0;
  const unsigned char *data = jot_embedded::find(rel_path.c_str(), &size);
  const std::string embedded = (data && size > 0) ? std::string((const char *)data, size) : "";
  const std::string hash = (data && size > 0) ? content_hash(data, size) : "";

  // No copy anywhere: materialize the embedded file into the user config dir
  // (with a marker so future embedded updates refresh it) and return it.
  if (candidates.empty())
  {
    if (embedded.empty())
    {
      return {};
    }
    std::filesystem::path root = user_config_root();
    if (root.empty())
    {
      return {};
    }
    std::error_code ec;
    const std::filesystem::path target = root / "lua" / rel_path;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
    {
      return {};
    }
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      return {};
    }
    out.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    if (!out)
    {
      return {};
    }
    write_marker(target, hash);
    return target;
  }

  const std::filesystem::path user_root = user_config_root();
  const std::filesystem::path first = candidates.front();

  // Overwrite `path` with the embedded content and (re)stamp its marker.
  // Returns true when the refresh succeeded; false leaves the file alone
  // (e.g. a read-only system install) and the caller keeps the copy as-is.
  auto refresh_with_embedded = [&](const std::filesystem::path &path)
  {
    if (embedded.empty())
    {
      return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      return false;
    }
    out.write(embedded.data(), static_cast<std::streamsize>(embedded.size()));
    if (!out)
    {
      return false;
    }
    write_marker(path, hash);
    return true;
  };

  // Hand-written overrides in the user dir (no marker) are kept verbatim and
  // win over everything: that is the documented community-edit escape hatch.
  const bool in_user_dir =
      !user_root.empty() && first.string().rfind(user_root.string(), 0) == 0;

  // Case A: the winning candidate is the user-dir copy.
  if (in_user_dir)
  {
    const bool marker_ok = std::filesystem::is_regular_file(marker_path(first));
    const std::string on_disk = read_file(first);

    if (marker_ok)
    {
      // We materialized this file. Keep it when it still matches the
      // embedded content; refresh it (and the marker) when the binary's
      // bundled lua moved on. Never applies to hand-written files.
      if (!embedded.empty() && on_disk != embedded)
      {
        if (refresh_with_embedded(first))
        {
          return first;
        }
      }
      return first;
    }

    // Unmarked file: could be a hand edit (keep) or a pre-marker
    // materialization (refresh). If a lower-precedence dir (installed or
    // source) carries a copy that matches the embedded content while this
    // one differs, this is stale cache: refresh it and mark it.
    if (!embedded.empty() && on_disk != embedded)
    {
      for (size_t i = 1; i < candidates.size(); i++)
      {
        if (read_file(candidates[i]) == embedded)
        {
          refresh_with_embedded(first);
          return first;
        }
      }
    }

    // The user copy matches the embedded content, but a lower dir differs
    // (e.g. an unrebuilt edit in the source dir): prefer the newer copy so
    // dev iteration on src/lua is not shadowed by stale cache.
    if (on_disk == embedded)
    {
      for (size_t i = 1; i < candidates.size(); i++)
      {
        if (read_file(candidates[i]) != embedded)
        {
          return candidates[i];
        }
      }
    }

    return first;
  }

  // Case B: no user-dir copy; an installed (or dev-source) copy wins.
  // Installed copies are jot's own payload, not user edits: refresh them
  // when they lag the embedded content (marked or pre-marker stale) so
  // `make install` snapshots track the bundled lua, while the dev source
  // dir stays the developer's working copy and is never overwritten.
  {
    const bool marker_ok = std::filesystem::is_regular_file(marker_path(first));
    const std::string on_disk = read_file(first);

    if (marker_ok)
    {
      // jot wrote this file: refresh when the binary's bundled lua moved on.
      if (!embedded.empty() && on_disk != embedded)
      {
        if (refresh_with_embedded(first))
        {
          return first;
        }
      }
      return first;
    }

    // Unmarked: keep hand edits, but refresh a stale pre-marker install
    // when a lower-precedence copy (the dev source dir) carries the current
    // content. The dev source itself never matches this branch (no lower
    // dirs left), so in-progress edits there are always kept.
    if (!embedded.empty() && on_disk != embedded)
    {
      for (size_t i = 1; i < candidates.size(); i++)
      {
        if (read_file(candidates[i]) == embedded)
        {
          refresh_with_embedded(first);
          return first;
        }
      }
    }

    // Installed copy matches the embedded content but a lower dir differs
    // (e.g. an unrebuilt edit in the source dir): prefer the newer copy so
    // dev iteration on src/lua is not shadowed by a stale install.
    if (on_disk == embedded)
    {
      for (size_t i = 1; i < candidates.size(); i++)
      {
        if (read_file(candidates[i]) != embedded)
        {
          return candidates[i];
        }
      }
    }

    return first;
  }
}