// Debugger launch-config loading and per-config run commands, plus the state-file path helpers.
#include "commands/utils.h"
#include "editor.h"
#include "jot/lua/api.h"
#include "jot/integrations/debugger_internal.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace CommandLineUtils;
using namespace debugger_internal;

namespace debugger_internal
{
std::string default_debug_config_path(const std::string &root_dir)
{
  if (!root_dir.empty())
  {
    fs::path local = fs::path(root_dir) / ".jot" / "debug.json";
    std::error_code ec;
    if (fs::exists(local, ec) && !ec)
    {
      return local.string();
    }
  }
  const char *override_home = std::getenv("JOT_CONFIG_HOME");
  if (override_home && *override_home)
  {
    return (fs::path(override_home) / "configs" / "debug.json").string();
  }
#ifdef _WIN32
  const char *app_data = std::getenv("APPDATA");
  if (app_data && *app_data)
  {
    return (fs::path(app_data) / "jot" / "configs" / "debug.json").string();
  }
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home)
  {
    return "";
  }
  return (fs::path(home) / ".config" / "jot" / "configs" / "debug.json").string();
}
} // namespace debugger_internal

void Editor::load_debugger_configs()
{
  debugger_configs.clear();
  std::string path = default_debug_config_path(root_dir);
  if (path.empty())
  {
    return;
  }
  std::ifstream in(path);
  if (!in.is_open())
  {
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  debugger_configs = parse_debugger_config_text(ss.str());
  for (auto &config : debugger_configs)
  {
    if (config.cwd.empty())
    {
      config.cwd = root_dir.empty() ? "." : root_dir;
    }
  }
}

bool Editor::run_debugger_config(const std::string &name)
{
  load_debugger_configs();
  if (debugger_configs.empty())
  {
    set_message("No debug configs found");
    return false;
  }
  std::string needle = to_lower_copy(trim_copy(name));
  for (auto config : debugger_configs)
  {
    if (needle.empty() || to_lower_copy(config.name) == needle)
    {
      return start_debugger_session(config);
    }
  }
  set_message("Debug config not found: " + name);
  return false;
}

std::vector<std::string> Editor::list_debugger_config_names()
{
  load_debugger_configs();
  std::vector<std::string> names;
  for (const auto &config : debugger_configs)
  {
    names.push_back(config.name);
  }
  return names;
}
