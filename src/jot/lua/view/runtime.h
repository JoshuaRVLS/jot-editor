#ifndef LUA_VIEW_RUNTIME_H
#define LUA_VIEW_RUNTIME_H

// // ---------------------------------------------------------------------------
// Runtime records
// // ---------------------------------------------------------------------------
//
// The records the Lua runtime itself keeps: event-loop timers, pending edit
// deltas and the LSP install policy descriptions (:lspinstall reports).
//
// Split out of jot/lua/api.h, which keeps the LuaAPI class itself and
// includes these headers in order; the structs are unchanged.

#include <string>
#include <vector>


// Timer API (jot.timer): native event-loop timers with Lua callbacks. Entries
// are keyed by the native EventLoop::TimerId (uint64_t); the callback ref is
// also registered under "timer.<ref>" in lua_callbacks so cleanup paths unref
// everything through the existing callback sweep.
struct LuaTimerEntry
{
  int ref = -1;
  bool repeat = false;
};

// Rich per-edit description attached to BufChange events (see
// LuaAPI::on_buffer_change). All line/col values are 1-based; end_line /
// end_col describe the exclusive end of the inserted region in the NEW text.
struct LuaEditDelta
{
  bool valid = false;
  int start_line = 1;
  int start_col = 1;
  int end_line = 1;
  int end_col = 1;
  std::string inserted; // text added by the edit
  std::string removed;  // text replaced/removed by the edit
  bool multiline = false;
};

// A server in the Lua installer registry (runtime/lua/lsp/registry.lua). The
// native side consumes this list for completions and error messages.
struct LspServerSpec
{
  std::string id;
  std::string display;
  std::string detail;
};

// Extra server an attach-policy entry wants launched next to the primary one.
struct LspPolicyExtra
{
  std::string server; // canonical registry id (client key / status label)
  std::string bin;    // binary name under the managed install root / PATH
  std::vector<std::string> args;
};

// One item of a one-shot toolkit preset ("web").
struct LspPolicyTool
{
  std::string kind; // "lsp" or "parser"
  std::string name;
};

#endif
