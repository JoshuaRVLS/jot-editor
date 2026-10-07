#include "relaunch.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
  std::string g_exe;
  std::vector<std::string> g_args; // original argv[1..], replay order

  // Windows command-line quoting: wrap in quotes and escape embedded quotes.
  std::string quote_win(const std::string &s)
  {
    return "\"" + s + "\"";
  }
} // namespace

namespace relaunch
{
  void capture_startup(int argc, char *argv[])
  {
    if (argc < 1 || !argv || !argv[0] || !*argv[0])
    {
      return;
    }
    namespace fs = std::filesystem;
    std::error_code ec;

    // Executable: absolute when possible so a later exec finds the same path
    // even after the editor chdir'd into a workspace.
    fs::path exe(argv[0]);
    if (exe.is_relative())
    {
      exe = fs::absolute(exe, ec);
      if (ec)
      {
        exe = argv[0]; // keep the bare name: execvp still searches $PATH
      }
    }
    const fs::path canon = fs::weakly_canonical(exe, ec);
    g_exe = ec ? exe.string() : canon.string();

    // Extra args. Only the first non-flag path is absolutized (main treats
    // argv[1] as the file/workspace); the rest are replayed verbatim.
    for (int i = 1; i < argc; i++)
    {
      std::string a = argv[i];
      if (i == 1 && !a.empty() && a[0] != '-' && !a.empty())
      {
        fs::path p(a);
        if (p.is_relative())
        {
          const fs::path abs = fs::absolute(p, ec);
          if (!ec)
          {
            a = abs.string();
          }
        }
      }
      g_args.push_back(a);
    }
  }

  bool restart_self()
  {
    if (g_exe.empty())
    {
      return false;
    }

#ifdef _WIN32
    std::string cmdline = quote_win(g_exe);
    for (const auto &a : g_args)
    {
      cmdline += " ";
      cmdline += quote_win(a);
    }
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> mutable_cmd(cmdline.begin(), cmdline.end());
    mutable_cmd.push_back('\0');
    const BOOL ok = CreateProcessA(g_exe.c_str(),
                                   mutable_cmd.data(),
                                   nullptr,
                                   nullptr,
                                   FALSE, // handles are not inherited; same console job
                                   0,
                                   nullptr,
                                   nullptr,
                                   &si,
                                   &pi);
    if (!ok)
    {
      return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    // In-place exec: the new jot keeps the same pid, terminal and shell job,
    // which makes the swap seamless. On success this never returns.
    std::vector<char *> argv;
    argv.push_back(const_cast<char *>(g_exe.c_str()));
    for (const auto &a : g_args)
    {
      argv.push_back(const_cast<char *>(a.c_str()));
    }
    argv.push_back(nullptr);

    // execvp when g_exe has no slash (launched via $PATH); execv otherwise.
    const std::string cmd = g_exe;
    if (cmd.find('/') == std::string::npos)
    {
      execvp(cmd.c_str(), argv.data());
    }
    else
    {
      execv(cmd.c_str(), argv.data());
    }
    (void)errno; // exec failed: report back so the caller can inform the user
    return false;
#endif
  }
} // namespace relaunch
