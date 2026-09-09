// Git command runners (src/jot/workspace/git_run.h).
//
// Header-only helpers for the git panel actions: run `git -C <root> ...`,
// capturing stdout (git_capture) or just the exit status (git_run_ok).
// Deliberately free of Editor dependencies so any workspace code can use
// them; git.cpp keeps its own private variants for the status refresh.

#ifndef GIT_RUN_H
#define GIT_RUN_H

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <cstdlib>
#else
#include <sys/wait.h>
#endif

#ifdef _WIN32
#define GIT_RUN_POPEN _popen
#define GIT_RUN_PCLOSE _pclose
#else
#define GIT_RUN_POPEN popen
#define GIT_RUN_PCLOSE pclose
#endif

namespace jot_git
{
  inline std::string shell_quote(const std::string &value)
  {
    std::string out = "'";
    for (char c : value)
    {
      if (c == '\'')
      {
        out += "'\\''";
      }
      else
      {
        out += c;
      }
    }
    out += "'";
    return out;
  }

  // Captured stdout plus the command's exit status, so callers can tell a
  // clean empty output (exit 0, e.g. `git status` on a clean tree) apart
  // from a failed command (nonzero exit).
  struct Captured
  {
    std::string output;
    int exit_code = 1;

    bool ok() const
    {
      return exit_code == 0;
    }
  };

  // Like capture, but also reports the exit status.
  inline Captured capture_ex(const std::string &root, const std::string &args)
  {
    const std::string cmd = "git -C " + shell_quote(root) + " " + args + " 2>/dev/null";
    Captured result;
    std::FILE *pipe = GIT_RUN_POPEN(cmd.c_str(), "r");
    if (!pipe)
    {
      return result;
    }
    std::ostringstream out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr)
    {
      out << buf;
    }
    const int status = GIT_RUN_PCLOSE(pipe);
    result.output = out.str();
    while (!result.output.empty()
           && (result.output.back() == '\n' || result.output.back() == '\r'))
    {
      result.output.pop_back();
    }
#ifdef _WIN32
    result.exit_code = status;
#else
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    return result;
  }

  // Runs `git -C <root> <args...>` and returns captured stdout (trimmed of
  // a trailing newline). Returns empty on spawn failure.
  inline std::string capture(const std::string &root, const std::string &args)
  {
    const std::string cmd = "git -C " + shell_quote(root) + " " + args + " 2>/dev/null";
    std::unique_ptr<std::FILE, decltype(&GIT_RUN_PCLOSE)> pipe(
        GIT_RUN_POPEN(cmd.c_str(), "r"), GIT_RUN_PCLOSE);
    if (!pipe)
    {
      return {};
    }
    std::ostringstream out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe.get()) != nullptr)
    {
      out << buf;
    }
    std::string text = out.str();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
      text.pop_back();
    }
    return text;
  }

  // Runs `git -C <root> <args...>`; true when the command exited 0.
  inline bool run_ok(const std::string &root, const std::string &args)
  {
    const std::string cmd =
        "git -C " + shell_quote(root) + " " + args + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    return rc == 0;
  }
} // namespace jot_git

#endif // GIT_RUN_H