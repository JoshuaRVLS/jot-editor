#ifndef TERMINAL_SESSION_H
#define TERMINAL_SESSION_H

#include <cstddef>
#include <memory>
#include <string>

class TerminalSession
{
public:
  virtual ~TerminalSession() = default;

  virtual bool open(const std::string &cwd, int rows, int cols) = 0;
  virtual void close() = 0;
  virtual void close_after_exit() = 0;
  virtual bool read_available(std::string &out, size_t max_bytes) = 0;
  virtual size_t write(const char *data, size_t size) = 0;
  virtual bool resize(int rows, int cols) = 0;
  virtual bool process_exited() const = 0;
  virtual bool active() const = 0;

  // POSIX returns the PTY master fd for event-loop polling. Windows returns -1
  // because ConPTY uses HANDLEs and is polled by the editor timer.
  virtual int input_fd() const = 0;
};

std::unique_ptr<TerminalSession> make_terminal_session();

#endif
