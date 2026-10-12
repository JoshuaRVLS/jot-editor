#include "base/process_job.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{
  // The variables a tool that is not sure it should colourise looks at. Under
  // a pty most of them work that out from the tty on their own; these cover the
  // ones that read the environment instead (and the no-pty fallback below), and
  // the worst a wrong guess can do is put colour in a log.
  constexpr const char *kColorEnv[][2] = {
      {"CLICOLOR_FORCE", "1"},
      {"FORCE_COLOR", "1"},
      {"CARGO_TERM_COLOR", "always"},
  };
  constexpr const char *kTermFallback = "xterm-256color";

  // Rows and columns for the job's terminal. Nothing here draws it, so the size
  // only decides where a program's own wrapping and progress bars break; 40x120
  // is wide enough that a build tool does not wrap a diagnostic for no reason.
  constexpr int kJobTerminalRows = 40;
  constexpr int kJobTerminalCols = 120;
} // namespace

namespace process_job
{

  std::string install_logs_dir()
  {
    namespace fs = std::filesystem;
    const char *override_home = std::getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      return (fs::path(override_home) / "logs").string();
    }
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return (fs::path(app_data) / "jot" / "logs").string();
    }
#else
    const char *home = std::getenv("HOME");
    if (home && *home)
    {
      return (fs::path(home) / ".config" / "jot" / "logs").string();
    }
#endif
    return (fs::temp_directory_path() / "jot-logs").string();
  }

  std::string make_job_log_path(const std::string &kind)
  {
    namespace fs = std::filesystem;
    const std::string dir = install_logs_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    // Unique per job: pid is not yet known, so disambiguate with time + a
    // monotonic counter instead of risking a collision on parallel installs.
    static unsigned long long counter = 0;
    counter++;
    const long long now =
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
    std::string name = kind;
    for (char &ch : name)
    {
      if (ch == '/' || ch == '\\' || ch == ':')
      {
        ch = '-';
      }
    }
    return (fs::path(dir)
            / (name + "_" + std::to_string(now) + "_" + std::to_string(counter) + ".log"))
        .string();
  }

  std::string make_install_log_path(const std::string &kind)
  {
    // The install prefix keeps the filenames the install flows have always
    // written; only the sharing with the compile job is new.
    return make_job_log_path("install_" + kind);
  }

#ifdef _WIN32
  int spawn_background_shell(const std::string &shell_command, const std::string &output_path)
  {
    // Open the log file for append with inheritable handle.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE out = CreateFileA(output_path.c_str(),
                             FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa,
                             OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (out == INVALID_HANDLE_VALUE)
    {
      return -1;
    }
    std::string cmdline = "cmd /c " + shell_command;
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = out;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(nullptr,
                                   cmdline.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   CREATE_NO_WINDOW,
                                   nullptr,
                                   nullptr,
                                   &si,
                                   &pi);
    CloseHandle(out);
    if (!ok)
    {
      return -1;
    }
    CloseHandle(pi.hThread);
    const int pid = static_cast<int>(pi.dwProcessId);
    CloseHandle(pi.hProcess);
    return pid;
  }

  int spawn_background_pty_shell(const std::string &shell_command, const std::string &output_path)
  {
    // There is no pty here (a ConPTY job is a project of its own), so the job
    // keeps the silent pipe to the log and only the colour environment is
    // added -- inside the command line, so it reaches the job's shell and
    // nothing else. A command that reads stdin still sees /dev/null equivalent
    // (cmd gives it the editor's console, which this process must not block on,
    // so the pipe's EOF is what it gets).
    std::string forced;
    for (const auto &entry : kColorEnv)
    {
      forced += "set " + std::string(entry[0]) + "=" + entry[1] + "&& ";
    }
    return spawn_background_shell(forced + shell_command, output_path);
  }

  bool terminate_child(int pid)
  {
    if (pid <= 0)
    {
      return false;
    }
    HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!proc)
    {
      return false;
    }
    const bool killed = TerminateProcess(proc, 1) != 0;
    CloseHandle(proc);
    return killed;
  }

  int reap_child(int pid)
  {
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!proc)
    {
      return -1;
    }
    const DWORD wait = WaitForSingleObject(proc, 0);
    if (wait != WAIT_OBJECT_0)
    {
      CloseHandle(proc);
      return -1;
    }
    DWORD code = 0;
    const BOOL got = GetExitCodeProcess(proc, &code);
    CloseHandle(proc);
    return got ? static_cast<int>(code) : -1;
  }
#else
  int spawn_background_shell(const std::string &shell_command, const std::string &output_path)
  {
    const int fd = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
    {
      return -1;
    }
    const int devnull = ::open("/dev/null", O_RDONLY);
    const pid_t pid = fork();
    if (pid < 0)
    {
      ::close(fd);
      if (devnull >= 0)
      {
        ::close(devnull);
      }
      return -1;
    }
    if (pid == 0)
    {
      // Child: detach from the editor's process group and terminal so the job
      // keeps running (and never writes to the editor screen) even if the
      // editor exits first.
      setsid();
      if (devnull >= 0)
      {
        dup2(devnull, STDIN_FILENO);
      }
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);
      ::close(fd);
      if (devnull >= 0)
      {
        ::close(devnull);
      }
      execl("/bin/sh", "sh", "-lc", shell_command.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }
    ::close(fd);
    if (devnull >= 0)
    {
      ::close(devnull);
    }
    return static_cast<int>(pid);
  }

  int spawn_background_pty_shell(const std::string &shell_command, const std::string &output_path)
  {
    // The colour environment goes into the command line rather than through
    // setenv after the fork: the editor runs threads, and the child of a fork
    // may not allocate until it execs.
    std::string prepared = "export ";
    for (const auto &entry : kColorEnv)
    {
      prepared += entry[0];
      prepared += "=";
      prepared += entry[1];
      prepared += " ";
    }
    const char *term = std::getenv("TERM");
    if (term == nullptr || *term == '\0' || std::string(term) == "dumb")
    {
      prepared += "TERM=";
      prepared += kTermFallback;
      prepared += " ";
    }
    if (std::getenv("COLORTERM") == nullptr)
    {
      prepared += "COLORTERM=truecolor ";
    }
    prepared += "; ";
    prepared += shell_command;

    winsize size{};
    size.ws_row = static_cast<unsigned short>(kJobTerminalRows);
    size.ws_col = static_cast<unsigned short>(kJobTerminalCols);
    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, &size) != 0)
    {
      return -1;
    }

    // The drainer, first so that the terminal always has a reader: it copies
    // the master side into the log and is what keeps the job's output from
    // filling the terminal's buffer and blocking the build. It forks a second
    // time and the intermediate exits at once (reaped here), so the process
    // that does the draining belongs to nobody and cannot become a zombie.
    const pid_t helper = fork();
    if (helper == 0)
    {
      const pid_t drainer = fork();
      if (drainer != 0)
      {
        _exit(0);
      }
      setsid();
      // The slave must go, or reads on the master would never see the job's
      // session close and the drainer would wait forever after the build ends.
      ::close(slave);
      int sink = ::open(output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (sink < 0)
      {
        // No log to write: keep draining anyway, so the job is never the thing
        // that blocks on output nobody is reading.
        sink = ::open("/dev/null", O_WRONLY);
        if (sink < 0)
        {
          _exit(1);
        }
      }
      // The editor's terminal is not this process's business; a stray write to
      // it would land on the screen the editor is drawing.
      ::close(STDIN_FILENO);
      ::close(STDOUT_FILENO);
      ::close(STDERR_FILENO);
      char buffer[16384];
      for (;;)
      {
        const ssize_t got = ::read(master, buffer, sizeof(buffer));
        if (got > 0)
        {
          ssize_t written = 0;
          while (written < got)
          {
            const ssize_t n = ::write(sink, buffer + written, (size_t)(got - written));
            if (n > 0)
            {
              written += n;
              continue;
            }
            if (n < 0 && errno == EINTR)
            {
              continue;
            }
            // The log went away under the job (unlinked, or a full disk): drop
            // what cannot be kept and keep the job moving.
            ::close(sink);
            sink = ::open("/dev/null", O_WRONLY);
            if (sink < 0)
            {
              _exit(0);
            }
            break;
          }
          continue;
        }
        if (got < 0 && errno == EINTR)
        {
          continue;
        }
        // 0 or EIO: every slave port is closed, so the job is over.
        _exit(0);
      }
    }
    if (helper < 0)
    {
      ::close(master);
      ::close(slave);
      return -1;
    }
    int helper_status = 0;
    while (waitpid(helper, &helper_status, 0) < 0 && errno == EINTR)
    {
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
      ::close(master);
      ::close(slave);
      return -1;
    }
    if (pid == 0)
    {
      setsid();
      // The terminal becomes the job's controlling one, so a program that
      // opens /dev/tty or asks who its foreground group is gets an answer.
      ioctl(slave, TIOCSCTTY, 0);
      ::close(master);
      dup2(slave, STDIN_FILENO);
      dup2(slave, STDOUT_FILENO);
      dup2(slave, STDERR_FILENO);
      if (slave > STDERR_FILENO)
      {
        ::close(slave);
      }
      execl("/bin/sh", "sh", "-lc", prepared.c_str(), static_cast<char *>(nullptr));
      _exit(127);
    }

    ::close(slave);
    // One end-of-input byte, so a command that reads stdin gets the immediate
    // EOF the silent spawn's /dev/null gave it rather than a prompt that waits
    // for a keyboard the log does not have.
    const char end_of_input = 4; // ^D, canonical mode's EOF
    const ssize_t wrote = ::write(master, &end_of_input, 1);
    (void)wrote;
    ::close(master);
    return static_cast<int>(pid);
  }

  bool terminate_child(int pid)
  {
    if (pid <= 0)
    {
      return false;
    }
    // The job leads its own process group (setsid in the spawn), so the
    // negative pid reaches `make`'s children too.
    return ::kill(-(pid_t)pid, SIGTERM) == 0 || ::kill((pid_t)pid, SIGTERM) == 0;
  }

  int reap_child(int pid)
  {
    if (pid <= 0)
    {
      return -1;
    }
    int status = 0;
    const pid_t result = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    if (result <= 0)
    {
      return -1;
    }
    if (WIFEXITED(status))
    {
      return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status))
    {
      return 128 + WTERMSIG(status);
    }
    return -1;
  }
#endif

  size_t read_appended(const std::string &path, size_t offset, std::string &out)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
      return offset;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= static_cast<std::streamoff>(offset))
    {
      return offset;
    }
    in.seekg(static_cast<std::streamoff>(offset));
    std::string chunk(static_cast<size_t>(size) - offset, '\0');
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    out.append(chunk);
    return static_cast<size_t>(size);
  }

} // namespace process_job
