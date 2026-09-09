// Git command runners (src/jot/workspace/git_run.h).
//
// Header-only helpers for running `git -C <root> <args...>` and capturing
// stdout (git_capture), stdout plus exit status (git_capture_ex), or stdout
// with stderr merged in for error reporting (git_capture_errors), plus a
// fire-and-forget exit-status check (git_run_ok). Deliberately free of
// Editor dependencies so any workspace code can use them; this is the single
// source of truth for running git commands used by the git panel, the git
// status refresh, and the git ex-commands. Shell quoting and pipe primitives
// come from tools/shell_util.h.

#ifndef GIT_RUN_H
#define GIT_RUN_H

#include "tools/shell_util.h"
#include <cstdlib>
#include <sstream>
#include <string>

namespace jot_git
{
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

  namespace detail
  {
    // Runs `cmd` through the shell, capturing stdout (stderr merged in when
    // `merge_stderr`), trimming trailing newlines, and decoding the exit
    // status. Shared by the public capture helpers.
    inline Captured run_pipe(const std::string &cmd, bool merge_stderr)
    {
      Captured result;
      std::FILE *pipe =
          shell_util::open_command_pipe(cmd + (merge_stderr ? " 2>&1" : shell_util::null_redirect()),
                                        "r");
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
      const int status = shell_util::close_command_pipe(pipe);
      result.exit_code = shell_util::command_exit_code(status);
      result.output = out.str();
      while (!result.output.empty()
             && (result.output.back() == '\n' || result.output.back() == '\r'))
      {
        result.output.pop_back();
      }
      return result;
    }
  } // namespace detail

  // Runs `git -C <root> <args...>` and returns captured stdout (trimmed of
  // trailing newlines) plus the exit status. Stderr is silenced.
  inline Captured capture_ex(const std::string &root, const std::string &args)
  {
    return detail::run_pipe("git -C " + shell_util::shell_quote(root) + " " + args, false);
  }

  // Like capture_ex, but merges stderr into the output (2>&1) so callers can
  // surface git's error message — e.g. why a commit was rejected.
  inline Captured capture_errors(const std::string &root, const std::string &args)
  {
    return detail::run_pipe("git -C " + shell_util::shell_quote(root) + " " + args, true);
  }

  // Runs `git -C <root> <args...>` and returns captured stdout (trimmed of a
  // trailing newline). Returns empty on spawn failure. Stderr is silenced.
  inline std::string capture(const std::string &root, const std::string &args)
  {
    return capture_ex(root, args).output;
  }

  // Runs `git -C <root> <args...>`; true when the command exited 0.
  inline bool run_ok(const std::string &root, const std::string &args)
  {
    const std::string cmd =
        "git -C " + shell_util::shell_quote(root) + " " + args + " >/dev/null 2>&1";
    const int rc = std::system(cmd.c_str());
    return rc == 0;
  }
} // namespace jot_git

#endif // GIT_RUN_H