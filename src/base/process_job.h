#ifndef JOT_PROCESS_JOB_H
#define JOT_PROCESS_JOB_H

#include <string>

// Silent background shell jobs: installs, and the commands `:compile` runs.
//
// LSP and tree-sitter installers used to run their shell commands inside an
// integrated terminal so the output could be polled for progress markers.
// That made an install steal the terminal panel and feel heavy. These helpers
// run the same shell command as a detached background process whose stdout and
// stderr stream into a log file; the editor polls appended lines for the
// installer's lifecycle markers exactly as it polled the terminal before -- and
// `:compile` streams the same way into the compilation buffer, which is why it
// needs the fourth helper below (a build that hangs on a prompt has to be
// killable).
namespace process_job
{
  // Runs `shell_command` (POSIX shell syntax) in the background with stdout and
  // stderr appended to output_path (created / truncated up front). Returns the
  // child pid, or -1 when spawning is unsupported or failed - callers then fall
  // back to their previous terminal-based flow.
  int spawn_background_shell(const std::string &shell_command, const std::string &output_path);

  // The same job under a pseudo-terminal, for commands that are not builds: a
  // compiled program run through this reads its stdin and prints its own
  // colours, because it sees the tty it looks for. The log is still the whole
  // interface -- a detached drainer copies the terminal's output into
  // output_path, so poll_* and read_appended do not change -- and the child
  // still leads its own session and process group, so terminate_child reaches
  // it and its grandchildren and the job outlives the editor.
  //
  // One end-of-input byte is written to the terminal up front, so a command
  // that reads stdin (a `make` asking a question) sees the same immediate EOF
  // the silent spawn's /dev/null gave it instead of waiting forever on a
  // prompt nobody can answer. Platforms with no pty fall back to
  // spawn_background_shell and force the colour environment instead, so the
  // log still arrives coloured; only the tty itself is missing there.
  int
  spawn_background_pty_shell(const std::string &shell_command, const std::string &output_path);

  // WNOHANG reaps a finished child. Returns -1 while the child is still
  // running, otherwise its exit code. Safe to call every poll.
  int reap_child(int pid);

  // Ends a job and everything it started. The job runs in its own session and
  // its own process group (see spawn_background_shell), so signalling the
  // group reaches the grandchildren a `make` leaves behind -- signalling the
  // shell alone would leave the compiler writing into a log nobody reads.
  // Returns false when there was no such process to signal.
  bool terminate_child(int pid);

  // Reads bytes appended to `path` since `offset` into `out` and returns the
  // new offset. Cheap to call every poll; missing files read nothing.
  size_t read_appended(const std::string &path, size_t offset, std::string &out);

  // Directory that holds background install logs (created on demand), matching
  // the app's logs convention (JOT_CONFIG_HOME/logs, config-home logs, temp).
  std::string install_logs_dir();

  // A fresh unique log path inside the install logs directory. `kind` is a
  // short label used in the file name (e.g. "lsp-python", "ts-cpp").
  std::string make_install_log_path(const std::string &kind);

  // A fresh unique log path for a job whose output the editor reads back
  // itself (`kind` = "compile"): the install prefix would be a lie in the file
  // name, so this one is the bare label.
  std::string make_job_log_path(const std::string &kind);
} // namespace process_job

#endif
