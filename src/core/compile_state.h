#ifndef JOT_STATE_COMPILE_STATE_H
#define JOT_STATE_COMPILE_STATE_H

#include "text/ansi_spans.h"     // ansi_spans::Parser
#include "text/compile_parse.h" // CompileFinding, compile_parse::Parser

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The compile session (`:compile`, and the prompt that opens it): the shell
// command, the background job whose output streams into the `[Compile]`
// buffer, the locations parsed out of that output, and the split the buffer is
// shown in.
//
// One run at a time, which is what makes `:compile` again mean "run it again"
// rather than "start a second build": starting a job that is already running
// stops the first one in place. The output lives in the run rather than in a
// terminal so a build that takes a minute cannot take the shell panel with it.
struct CompileRun
{
  std::string command;    // the last command line, offered as the next prompt's default
  std::string cwd;        // the directory it runs in (the project root)
  std::string log_path;   // the job's output file (base/process_job.h streams into it)
  std::size_t log_offset = 0;
  int pid = -1;
  bool running = false;
  bool has_run = false;   // a command has been started at least once
  int exit_code = 0;
  long long started_ms = 0;
  long long finished_ms = 0;
  // Bytes read that are not a whole line yet, so a chunk boundary in the middle
  // of an error message cannot lose it.
  std::string pending;
  // The buffer the output lands in, and the pane that shows it. -1 while one of
  // them does not exist (before the first run, after the pane was closed).
  int buffer_id = -1;
  int pane_index = -1;
  int output_rows = 0; // rows the command's own output occupies, for the tally
  std::vector<CompileFinding> findings;
  int selected = -1; // the finding the last jump landed on
  // Keep the view at the end while the job writes, until the user scrolls.
  bool follow_tail = true;
  compile_parse::Parser parser;
  // The colours in effect in the job's output. Stateful across rows, because a
  // command that sets a colour once keeps it for everything it prints after.
  ansi_spans::Parser ansi;
  // The row in the compilation buffer each finding came from, so Enter on a
  // row can jump (parallel to `findings`).
  std::vector<int> finding_rows;
  // Decoration ids for the row tints, monotonic per session so a fresh run's
  // tints cannot collide with the ones the buffer is still carrying.
  std::uint64_t decoration_id = 0;
};

struct CompileSessionState
{
  CompileRun compile;
};

#endif
