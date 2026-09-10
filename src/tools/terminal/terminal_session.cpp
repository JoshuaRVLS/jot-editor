#include "tools/terminal/terminal_session.h"

#include <memory>

#ifdef _WIN32
#include "tools/terminal/win32_terminal_session.h"
#else
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

namespace
{
  constexpr int kDefaultRows = 24;
  constexpr int kDefaultCols = 80;

  termios shell_termios()
  {
    termios result{};
    result.c_iflag = BRKINT | ICRNL | IXON | IMAXBEL;
    result.c_oflag = OPOST | ONLCR;
    result.c_cflag = CREAD | CS8;
    result.c_lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK;
#ifdef ECHOCTL
    result.c_lflag |= ECHOCTL;
#endif
#ifdef ECHOKE
    result.c_lflag |= ECHOKE;
#endif
    result.c_cc[VINTR] = 3;
    result.c_cc[VQUIT] = 28;
    result.c_cc[VERASE] = 127;
    result.c_cc[VKILL] = 21;
    result.c_cc[VEOF] = 4;
    result.c_cc[VMIN] = 1;
    result.c_cc[VTIME] = 0;
    return result;
  }

  class PosixTerminalSession final : public TerminalSession
  {
  public:
    ~PosixTerminalSession() override
    {
      close();
    }

    bool open(const std::string &cwd, int rows, int cols) override
    {
      close();
      termios settings = shell_termios();
      winsize size{};
      size.ws_row = static_cast<unsigned short>(rows > 0 ? rows : kDefaultRows);
      size.ws_col = static_cast<unsigned short>(cols > 0 ? cols : kDefaultCols);
      if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_row == 0 || size.ws_col == 0)
      {
        size.ws_row = static_cast<unsigned short>(rows > 0 ? rows : kDefaultRows);
        size.ws_col = static_cast<unsigned short>(cols > 0 ? cols : kDefaultCols);
      }

      int fd = -1;
      pid_t pid = forkpty(&fd, nullptr, &settings, &size);
      if (pid < 0)
      {
        return false;
      }
      if (pid == 0)
      {
        if (!cwd.empty())
        {
          chdir(cwd.c_str());
        }
        setenv("TERM", "xterm-256color", 1);
        const char *shell = std::getenv("SHELL");
        if (shell && *shell)
        {
          execlp(shell, shell, "-i", nullptr);
        }
        execlp("/bin/bash", "bash", "-i", nullptr);
        execlp("/bin/sh", "sh", "-i", nullptr);
        _exit(127);
      }

      master_fd_ = fd;
      child_pid_ = pid;
      int flags = fcntl(master_fd_, F_GETFL, 0);
      if (flags >= 0)
      {
        fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
      }
      return true;
    }

    void close() override
    {
      if (child_pid_ > 0)
      {
        kill(child_pid_, SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        int status = 0;
        while (waitpid(child_pid_, &status, WNOHANG) == 0
               && std::chrono::steady_clock::now() < deadline)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (waitpid(child_pid_, &status, WNOHANG) == 0)
        {
          kill(child_pid_, SIGKILL);
          waitpid(child_pid_, &status, 0);
        }
      }
      close_master();
      child_pid_ = -1;
    }

    void close_after_exit() override
    {
      // A session that never spawned a child (or already reaped one) must
      // not reach waitpid: waitpid(-1, ...) would block waiting on *any*
      // child of the process instead of the terminal's shell.
      if (child_pid_ <= 0 || !process_exited())
      {
        return;
      }
      int status = 0;
      waitpid(child_pid_, &status, 0);
      close_master();
      child_pid_ = -1;
    }

    bool read_available(std::string &out, size_t max_bytes) override
    {
      out.clear();
      if (!active() || max_bytes == 0)
      {
        return false;
      }
      std::string buffer(max_bytes, '\0');
      ssize_t count = 0;
      do
      {
        count = read(master_fd_, buffer.data(), buffer.size());
      } while (count < 0 && errno == EINTR);
      if (count <= 0)
      {
        return false;
      }
      buffer.resize(static_cast<size_t>(count));
      out = std::move(buffer);
      return true;
    }

    size_t write(const char *data, size_t size) override
    {
      if (!active() || !data || size == 0)
      {
        return 0;
      }
      ssize_t count = 0;
      do
      {
        count = ::write(master_fd_, data, size);
      } while (count < 0 && errno == EINTR);
      return count > 0 ? static_cast<size_t>(count) : 0;
    }

    bool resize(int rows, int cols) override
    {
      if (!active())
      {
        return false;
      }
      winsize size{};
      size.ws_row = static_cast<unsigned short>(std::max(1, rows));
      size.ws_col = static_cast<unsigned short>(std::max(1, cols));
      return ioctl(master_fd_, TIOCSWINSZ, &size) == 0;
    }

    bool process_exited() const override
    {
      if (child_pid_ <= 0)
      {
        return true;
      }
      int status = 0;
      pid_t result = waitpid(child_pid_, &status, WNOHANG);
      return result == child_pid_ || (result < 0 && errno == ECHILD);
    }

    bool active() const override
    {
      return master_fd_ >= 0 && child_pid_ > 0;
    }

    int input_fd() const override
    {
      return master_fd_;
    }

  private:
    void close_master()
    {
      if (master_fd_ >= 0)
      {
        ::close(master_fd_);
        master_fd_ = -1;
      }
    }

    int master_fd_ = -1;
    pid_t child_pid_ = -1;
  };
}
#endif

std::unique_ptr<TerminalSession> make_terminal_session()
{
#ifdef _WIN32
  return std::make_unique<Win32TerminalSession>();
#else
  return std::make_unique<PosixTerminalSession>();
#endif
}
