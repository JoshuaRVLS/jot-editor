#ifndef JOT_MODEL_DEBUGGER_H
#define JOT_MODEL_DEBUGGER_H

#include "tools/debugger/client.h"
#include <string>
#include <vector>
struct DebuggerSessionState
{
  std::string name;
  std::string adapter;
  std::string program;
  bool running = false;
  bool stopped = false;
  int active_thread_id = 0;
  int active_frame_id = 0;
  bool supports_read_memory = false;
  bool supports_disassemble = false;
  std::vector<DebuggerThread> threads;
  std::vector<DebuggerVariable> variables;
  std::vector<DebuggerMemoryRow> memory_rows;
  std::vector<DebuggerInstruction> instructions;
  std::string output;
  // Lines scrolled up from the end of `output` (0 = pinned to the bottom,
  // i.e. newest output visible). Clamped against the visible rows at render.
  int output_scroll = 0;
  std::string last_error;
};

#endif
