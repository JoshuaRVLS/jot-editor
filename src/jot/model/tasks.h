#ifndef JOT_MODEL_TASKS_H
#define JOT_MODEL_TASKS_H

#include <cstddef>
#include <string>
struct TerminalTask
{
  std::string name;
  std::string command;
  std::string source_path;
  std::string source_kind;
  std::string cwd;
};

struct TreeSitterInstallJob
{
  std::string language;
  // Silent background job (pid >= 0) writes its output to output_path and is
  // polled via read_appended(); terminal_index is the fallback used when a
  // background job could not be started.
  int pid = -1;
  std::string output_path;
  size_t output_offset = 0;
  int terminal_index = -1;
  bool running = true;
  bool succeeded = false;
  bool failed = false;
  std::string progress;
  int verify_attempts = 0;
  std::string install_prefix;
  bool prefix_applied = false;
};

struct LspInstallJob
{
  std::string server;
  bool removing = false;
  // Silent background job (pid >= 0) writes its output to output_path and is
  // polled via read_appended(); terminal_index is the fallback used when a
  // background job could not be started.
  int pid = -1;
  std::string output_path;
  size_t output_offset = 0;
  int terminal_index = -1;
  bool running = true;
  bool succeeded = false;
  bool failed = false;
  std::string progress;
};

#endif
