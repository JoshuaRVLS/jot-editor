#ifndef JOT_STATE_ENGINE_STATE_H
#define JOT_STATE_ENGINE_STATE_H

#include "config.h"             // Config
#include "event_loop.h"         // EventLoop
#include "imageviewer.h"        // ImageViewer
#include "jot/model/debugger.h" // DebuggerSessionState, DebuggerBreakpoint, DebuggerSessionConfig
#include "jot/model/tasks.h"    // TerminalTask
#include "syntax_highlighter.h" // SyntaxHighlighter
#include "task_queue.h"         // TaskQueue
#include "terminal.h"           // Terminal
#include "tools/terminal/integrated.h" // IntegratedTerminal
#include "tree_sitter/manager.h"       // TreeSitterManager
#include "ui.h"                        // UI

#include <map>
#include <memory>
#include <string>
#include <vector>

class DebuggerClient;
class EditorHostAPI;
class LuaAPI;

// The long-lived subsystem objects the editor drives: the syntax highlighter,
// the config overlay, the image viewer, the integrated terminals and their
// task list, the debugger sessions and their breakpoints, the tree-sitter
// manager, the event loop and its task queue, the UI backend (terminal or GUI)
// and the Lua host. Also the flag that says the main loop is running.
//
// Split out of editor_state.h (which is now the umbrella over src/jot/state/);
// the members and their comments moved verbatim.
struct EngineState
{
  bool running = false;

  SyntaxHighlighter highlighter;
  Config config;
  ImageViewer image_viewer;

  Terminal terminal;
  std::vector<std::unique_ptr<IntegratedTerminal>> integrated_terminals;
  int current_integrated_terminal = 0;
  std::vector<TerminalTask> terminal_tasks;
  std::string last_terminal_task_name;

  std::vector<std::unique_ptr<DebuggerClient>> debugger_sessions;
  int current_debugger_session = 0;
  std::vector<DebuggerSessionState> debugger_session_state;
  std::map<std::string, std::vector<DebuggerBreakpoint>> debugger_breakpoints;
  std::vector<DebuggerSessionConfig> debugger_configs;
  bool debugger_breakpoint_hover_visible = false;
  int debugger_breakpoint_hover_pane = 0;
  int debugger_breakpoint_hover_buffer = 0;
  int debugger_breakpoint_hover_line = 0;

  TreeSitterManager ts_manager_;

  EventLoop event_loop_;
  std::unique_ptr<TaskQueue> task_queue_;

  UI *ui = nullptr;

  LuaAPI *lua_api;
  std::unique_ptr<EditorHostAPI> host_api;
};

#endif
