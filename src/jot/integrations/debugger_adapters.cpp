// Debug adapter resolution: binary lookup, command-line construction, and shell-word splitting.
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
bool command_exists(const std::string &name)
{
  if (name.empty())
  {
    return false;
  }
#ifdef _WIN32
  return std::system(("where " + name + " >NUL 2>NUL").c_str()) == 0;
#else
  return std::system(("command -v " + name + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

std::vector<std::string> adapter_command_for(const std::string &adapter)
{
  std::string lower = to_lower_copy(adapter);
  if (lower == "lldb" || lower == "lldb-dap")
  {
    return {"lldb-dap"};
  }
  return {"gdb", "--interpreter=dap"};
}

std::string adapter_binary_for(const std::string &adapter)
{
  auto command = adapter_command_for(adapter);
  return command.empty() ? "" : command.front();
}

std::vector<std::string> split_shell_words(const std::string &text)
{
  std::vector<std::string> out;
  std::string cur;
  bool single = false;
  bool dbl = false;
  bool esc = false;
  for (char c : text)
  {
    if (esc)
    {
      cur.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\' && !single)
    {
      esc = true;
      continue;
    }
    if (c == '\'' && !dbl)
    {
      single = !single;
      continue;
    }
    if (c == '"' && !single)
    {
      dbl = !dbl;
      continue;
    }
    if (std::isspace((unsigned char)c) && !single && !dbl)
    {
      if (!cur.empty())
      {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty())
  {
    out.push_back(cur);
  }
  return out;
}
} // namespace debugger_internal

