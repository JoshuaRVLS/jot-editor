#ifndef JOT_PROCESS_JOB_H
#define JOT_PROCESS_JOB_H

#include <string>

// Silent background shell jobs for install flows.
//
// LSP and tree-sitter installers used to run their shell commands inside an
// integrated terminal so the output could be polled for progress markers.
// That made an install steal the terminal panel and feel heavy. These helpers
// run the same shell command as a detached background process whose stdout and
// stderr stream into a log file; the editor polls appended lines for the
// installer's lifecycle markers exactly as it polled the terminal before.
namespace process_job
{
  // Runs `shell_command` (POSIX shell syntax) in the background with stdout and
  // stderr appended to output_path (created / truncated up front). Returns the
  // child pid, or -1 when spawning is unsupported or failed - callers then fall
  // back to their previous terminal-based flow.
  int spawn_background_shell(const std::string &shell_command, const std::string &output_path);

  // WNOHANG reaps a finished child. Returns -1 while the child is still
  // running, otherwise its exit code. Safe to call every poll.
  int reap_child(int pid);

  // Reads bytes appended to `path` since `offset` into `out` and returns the
  // new offset. Cheap to call every poll; missing files read nothing.
  size_t read_appended(const std::string &path, size_t offset, std::string &out);

  // Directory that holds background install logs (created on demand), matching
  // the app's logs convention (JOT_CONFIG_HOME/logs, config-home logs, temp).
  std::string install_logs_dir();

  // A fresh unique log path inside the install logs directory. `kind` is a
  // short label used in the file name (e.g. "lsp-python", "ts-cpp").
  std::string make_install_log_path(const std::string &kind);
} // namespace process_job

#endif
