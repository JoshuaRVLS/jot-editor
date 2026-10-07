// Git command runners (src/workspace/git_run.h).
//
// Header-only helpers for running `git -C <root> <args...>` and capturing
// stdout (git_capture), stdout plus exit status (git_capture_ex), or stdout
// with stderr merged in for error reporting (git_capture_errors), plus a
// fire-and-forget exit-status check (git_run_ok). Deliberately free of
// Editor dependencies so any workspace code can use them; this is the single
// source of truth for running git commands used by the git panel, the git
// status refresh, and the git ex-commands. Shell quoting and pipe primitives
// come from base/shell_util.h.

#ifndef GIT_RUN_H
#define GIT_RUN_H

#include "base/shell_util.h"
#include <cstddef>
#include <cstdio>
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
  // surface git's error message - e.g. why a commit was rejected.
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

  // The 64-bit FNV-1a constants. The hash only ever answers "is this the same
  // byte stream as last time", so it needs no cryptographic strength, just a
  // cheap pass a 5 MB porcelain output can be streamed through.
  constexpr unsigned long long kFnvOffset = 1469598103934665603ULL;
  constexpr unsigned long long kFnvPrime = 1099511628211ULL;

  inline unsigned long long fnv1a_seed(const std::string &text)
  {
    unsigned long long hash = kFnvOffset;
    for (unsigned char byte : text)
    {
      hash ^= byte;
      hash *= kFnvPrime;
    }
    return hash;
  }

  // What a command's stdout looked like, without keeping it: the running hash
  // and the byte count. `bytes` is -1 until a run has actually happened.
  struct OutputDigest
  {
    unsigned long long hash = 0;
    long long bytes = -1;
    int exit_code = 1;

    bool ok() const
    {
      return exit_code == 0;
    }
  };

  // Streams `command`'s stdout through the hash in fixed-size reads, so a
  // command whose output is megabytes costs no megabytes of memory. `seed`
  // lets a caller fold extra context (the repo root) into the digest.
  inline OutputDigest digest_command(const std::string &command,
                                     unsigned long long seed = kFnvOffset)
  {
    OutputDigest result;
    FILE *pipe = shell_util::open_command_pipe(command, "r");
    if (!pipe)
    {
      return result;
    }
    unsigned long long hash = seed;
    long long bytes = 0;
    char buf[4096];
    size_t read = 0;
    while ((read = std::fread(buf, 1, sizeof(buf), pipe)) > 0)
    {
      for (size_t i = 0; i < read; i++)
      {
        hash ^= (unsigned char)buf[i];
        hash *= kFnvPrime;
      }
      bytes += (long long)read;
    }
    result.exit_code = shell_util::command_exit_code(shell_util::close_command_pipe(pipe));
    result.hash = hash;
    result.bytes = bytes;
    return result;
  }

  // The digest of `git -C <root> <args...>`'s stdout. Seeded with the root so
  // two repositories with byte-identical status text do not digest the same.
  inline OutputDigest digest(const std::string &root, const std::string &args)
  {
    return digest_command("git -C " + shell_util::shell_quote(root) + " " + args
                              + shell_util::null_redirect(),
                          fnv1a_seed(root));
  }

  // What the last parsed `git status` was built from. `valid` is false until
  // one parse has landed, which is also what invalidates the cache when the
  // repo is cleared.
  struct StatusSnapshot
  {
    unsigned long long hash = 0;
    long long bytes = 0;
    bool valid = false;
  };

  // True when `current` is the very output the snapshot's parsed map was built
  // from, so re-parsing it into a path -> status map would only repeat the
  // same large allocation for nothing.
  inline bool status_snapshot_matches(const StatusSnapshot &snapshot, const OutputDigest &current)
  {
    return snapshot.valid && current.ok() && current.hash == snapshot.hash
           && current.bytes == snapshot.bytes;
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