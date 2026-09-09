// Shell command helpers (src/tools/shell_util.h).
//
// Header-only, dependency-free helpers for building and running shell
// commands: argument quoting (shell_quote), stderr silencing
// (null_redirect), popen/pclose wrappers (open_command_pipe /
// close_command_pipe), and exit-status decoding (command_exit_code).
// This is the single source of truth for these helpers — workspace code,
// tools, input commands, and the Lua bridge all include it instead of
// keeping private copies.

#ifndef SHELL_UTIL_H
#define SHELL_UTIL_H

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <cstdlib>
#else
#include <sys/wait.h>
#endif

namespace shell_util
{
  // Quotes a single command argument for the platform's shell. POSIX wraps
  // in single quotes ('...' with '\'' escaping); Windows wraps in double
  // quotes with "" escaping.
  inline std::string shell_quote(const std::string &value)
  {
#ifdef _WIN32
    std::string out = "\"";
    out.reserve(value.size() + 8);
    for (char c : value)
    {
      if (c == '"')
      {
        out += "\"\"";
      }
      else
      {
        out.push_back(c);
      }
    }
    out.push_back('"');
    return out;
#else
    std::string out = "'";
    out.reserve(value.size() + 8);
    for (char c : value)
    {
      if (c == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out.push_back(c);
      }
    }
    out.push_back('\'');
    return out;
#endif
  }

  // Redirect for silencing stderr on the platform's shell.
  inline std::string null_redirect()
  {
#ifdef _WIN32
    return " 2>NUL";
#else
    return " 2>/dev/null";
#endif
  }

  // Platform popen wrapper.
  inline FILE *open_command_pipe(const std::string &command, const char *mode)
  {
#ifdef _WIN32
    return _popen(command.c_str(), mode);
#else
    return popen(command.c_str(), mode);
#endif
  }

  // Platform pclose wrapper.
  inline int close_command_pipe(FILE *pipe)
  {
#ifdef _WIN32
    return _pclose(pipe);
#else
    return pclose(pipe);
#endif
  }

  // Decodes a pclose() status into a plain exit code (1 on abnormal exit).
  inline int command_exit_code(int status)
  {
#ifdef _WIN32
    return status;
#else
    if (status != -1 && WIFEXITED(status))
    {
      return WEXITSTATUS(status);
    }
    return 1;
#endif
  }
} // namespace shell_util

#endif // SHELL_UTIL_H