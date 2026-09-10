#include "jot/app/relaunch.h"
#include "jot/editor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>

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
  // --gui selects the SDL2/OpenGL frontend (a GPU window, vsync'd to the
  // monitor refresh) over the terminal backend. The flag can sit anywhere
  // in the command line; the first non-flag argument is the file or
  // workspace to open, as usual.
  bool gui_mode = false;
  const char *target = nullptr;
  for (int i = 1; i < argc; i++)
  {
    if (std::strcmp(argv[i], "--gui") == 0)
    {
      gui_mode = true;
    }
    else if (target == nullptr)
    {
      target = argv[i];
    }
  }

  // GUI mode owns no alternate screen: stderr stays on the terminal so
  // startup errors are visible. Terminal mode routes stderr to a log file
  // so nothing can corrupt the live screen (see above).
  if (!gui_mode)
  {
    route_stderr_away_from_terminal();
  }
  // Remember how jot was launched so a later :update restart can replay the
  // same executable + file/workspace arguments (self-restart support). The
  // --gui flag rides along in argv, so a GUI session restarts as a GUI.
  relaunch::capture_startup(argc, argv);

  std::unique_ptr<Editor> editor;
  try
  {
    // gui_mode: SDL2 window + OpenGL instead of a TTY. The GUI backend
    // throws std::runtime_error when the display or a font is missing.
    editor = std::make_unique<Editor>(gui_mode);
  }
  catch (const std::exception &ex)
  {
    std::fprintf(stderr, "jot: %s\n", ex.what());
    return 1;
  }

  if (target)
  {
    editor->set_home_menu_visible(false);
  }
  else
  {
    editor->resume_last_workspace_session();
  }

  if (target)
  {
    if (std::filesystem::is_directory(target))
    {
      std::error_code ec;
      std::filesystem::path workspace = std::filesystem::absolute(target, ec);
      if (!ec)
      {
        std::filesystem::current_path(workspace, ec);
      }
      editor->open_workspace(!ec ? workspace.string() : target, true);
    }
    else
    {
      editor->load_file(target);
    }
  }

  editor->run();

  return 0;
}