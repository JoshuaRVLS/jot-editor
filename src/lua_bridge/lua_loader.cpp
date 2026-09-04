#include "lua_bridge/lua_loader.h"
#include "lua_bridge/embedded_lua.h"

#include <cstdlib>
#include <fstream>

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
  if (!candidates.empty())
  {
    return candidates.front();
  }

  // Nothing on disk: materialize the embedded copy into the user config dir
  // so file-based runtimes (dofile treesitter modules, user edits) work.
  size_t size = 0;
  const unsigned char *data = jot_embedded::find(rel_path.c_str(), &size);
  if (!data || size == 0)
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
  return target;
}