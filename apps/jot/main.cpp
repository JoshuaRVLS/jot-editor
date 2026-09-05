#include "jot/editor.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
  // Resolves the logs directory used across the app (LSP logs etc.):
  // JOT_CONFIG_HOME/logs, else the platform config home logs dir.
  std::filesystem::path logs_directory()
  {
    namespace fs = std::filesystem;
    const char *override_home = std::getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return fs::path(override_home) / "logs";
    }
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return fs::path(app_data) / "jot" / "logs";
    }
#else
    const char *home = std::getenv("HOME");
    if (home && *home)
    {
      return fs::path(home) / ".config" / "jot" / "logs";
    }
#endif
    return fs::temp_directory_path() / "jot-logs";
  }

  // The editor renders into an alternate screen owned by stdout. When jot is
  // started interactively, stderr is the *same* terminal, so any stray write
  // to stderr -- e.g. the Lua runtime logging a skipped bundled tree-sitter
  // query -- is dumped raw onto the live screen at the current cursor
  // position, mid-frame. The row-diff renderer only repaints rows that
  // changed in its own model, so that text can corrupt rows the app believes
  // are up to date; the damage lingers until a full repaint (window resize).
  // Interactive sessions therefore reopen stderr onto a file in the logs
  // directory before the UI starts, so nothing outside the renderer can ever
  // write to the live screen. Piped / headless runs (stderr already not a
  // terminal) keep stderr untouched so scripts still see it.
  void route_stderr_away_from_terminal()
  {
#ifdef _WIN32
    if (!_isatty(_fileno(stdout)) || !_isatty(_fileno(stderr)))
    {
      return;
    }
#else
    if (!isatty(STDOUT_FILENO) || !isatty(STDERR_FILENO))
    {
      return;
    }
#endif
    const std::filesystem::path dir = logs_directory();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path log = dir / "jot_stderr.log";
#ifdef _WIN32
    const int fd = _open(log.string().c_str(), _O_WRONLY | _O_CREAT | _O_APPEND, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(log.string().c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
    if (fd < 0)
    {
      return;
    }
#ifdef _WIN32
    _dup2(fd, _fileno(stderr));
    if (fd != _fileno(stderr))
    {
      _close(fd);
    }
#else
    dup2(fd, STDERR_FILENO);
    if (fd != STDERR_FILENO)
    {
      ::close(fd);
    }
#endif
  }
} // namespace

int main(int argc, char *argv[])
{
  route_stderr_away_from_terminal();
  Editor editor;
  if (argc > 1)
  {
    editor.set_home_menu_visible(false);
  }
  else
  {
    editor.resume_last_workspace_session();
  }

  if (argc > 1)
  {
    if (std::filesystem::is_directory(argv[1]))
    {
      std::error_code ec;
      std::filesystem::path workspace = std::filesystem::absolute(argv[1], ec);
      if (!ec)
      {
        std::filesystem::current_path(workspace, ec);
      }
      editor.open_workspace(!ec ? workspace.string() : argv[1], true);
    }
    else
    {
      editor.load_file(argv[1]);
    }
  }

  editor.run();

  return 0;
}
