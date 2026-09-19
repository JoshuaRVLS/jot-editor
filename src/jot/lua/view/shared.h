#ifndef LUA_VIEW_SHARED_H
#define LUA_VIEW_SHARED_H

// // ---------------------------------------------------------------------------
// Scratch buffers and float spans
// // ---------------------------------------------------------------------------
//
// The two small helpers the Lua view structs share: LuaScratchBuffer (the
// in-memory buffer the snippet/preview APIs edit) and FloatSpan (one styled
// run inside a LuaFloatWindow's line).
//
// Split out of jot/lua/api.h, which keeps the LuaAPI class itself and
// includes these headers in order; the structs are unchanged.

#include "text_features.h"
#include "tools/debugger/client.h"
#include "tools/lsp/client.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct LuaScratchBuffer
{
  int handle = 0;
  bool listed = false;
  bool scratch = true;
  bool valid = true;
  std::vector<std::string> lines = {""};
};

struct FloatSpan
{
  int start = 0; // byte offset into the line
  int len = 0;   // byte length
  int fg = 7;    // foreground for the span
  int bg = -1;   // background for the span (-1 = the float's bg). A span that
                 // covers the whole line (start 0, len >= line length) sets
                 // the background for the whole line, like a selected row.
};

#endif
