#include "core/app/process_job.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

  std::string make_install_log_path(const std::string &kind)
  {
    namespace fs = std::filesystem;
    const std::string dir = install_logs_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    // Unique per job: pid is not yet known, so disambiguate with time + a
    // monotonic counter instead of risking a collision on parallel installs.
    static unsigned long long counter = 0;
    counter++;
    const long long now = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
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
    return (fs::path(dir) / ("install_" + name + "_" + std::to_string(now) + "_"
                             + std::to_string(counter) + ".log"))
        .string();
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
