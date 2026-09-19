#ifndef LUA_VIEW_PLUGINS_H
#define LUA_VIEW_PLUGINS_H

// // ---------------------------------------------------------------------------
// Float windows and plugin records
// // ---------------------------------------------------------------------------
//
// The floating window a plugin draws into, its spans and rows, plus the
// records the plugin runtime keeps: commands, keymaps, autocmds, panels,
// status segments and event-bus subscribers.
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

struct LuaFloatWindow
{
  int handle = 0;
  int buffer = 0;
  // jot.ui.handler surface name that opened this float ("sidebar",
  // "command_palette", ...); empty for floats opened outside a surface emit
  // (e.g. hover popups, user floats). render_floats() uses it to keep the
  // modal surface's own float out of the modal scrim.
  std::string surface;
  int x = 0, y = 0, w = 1, h = 1;
  int row = 0, col = 0;
  // Paint order: higher wins. 50 is editor chrome (sidebar, side panel, status
  // line); a modal surface's own float opens at LuaAPI::kModalFloatZindex so it
  // stays above that chrome even though the chrome is recreated every frame.
  int zindex = 50;
  bool valid = true;
  bool enter = false;
  bool focusable = true;
  bool mouse = false;
  bool hide = false;
  bool style_minimal = false;
  bool strip = false; // strips into the status area (bottom rows) instead of
                      // being clamped above it — used by the status line UI
  int fg = 7, bg = 0;
  int border_fg = -1; // -1 = fall back to fg
  int title_fg = -1;  // -1 = fall back to fg
  int footer_fg = -1; // -1 = fall back to fg
  // Background for the footer row (-1 = the float's own bg). The footer shares
  // the bottom border row, so a surface whose bar takes the region below it
  // gives the footer the same background.
  int footer_bg = -1;
  std::string relative = "editor";
  std::string anchor = "NW";
  std::string border = "none";
  // Which sides of that border to ink. Defaults to all four, which is what a
  // float over buffer content wants. A surface that is pinned against another
  // region (the right dock) turns the others off so the separator between them
  // is one line, drawn by the region on its left -- see render/pane_edges.h.
  // The border still reserves its cell, so content geometry is unchanged.
  bool border_top = true;
  bool border_right = true;
  bool border_bottom = true;
  bool border_left = true;
  // Background for the bottom border row (-1 = the float's own bg). Used when the
  // bar sits on top of another region and should take its colour.
  int border_bottom_bg = -1;
  std::array<std::string, 8> custom_border = {"", "", "", "", "", "", "", ""};
  std::string title;
  std::string footer;
  // Per-line inline spans (1-based line index). Used for syntax highlighting
  // inside float content; lines without spans render in fg/bg as before.
  std::map<int, std::vector<FloatSpan>> spans;
  int key_callback = -1;
  int mouse_callback = -1;
  int creation_order = 0;
};

struct PluginCommand
{
  std::string name;
  std::string callback;
  std::string detail;
};

struct PluginKeymap
{
  std::string key;
  std::string callback;
  std::string command;
  std::string detail;
  std::string mode;
};

// One row of the which-key helper: a possible next chord under the current
// prefix. `group` is true when pressing the key descends into another level
// instead of running an action.
struct PluginKeymapChild
{
  std::string key;
  std::string detail;
  bool group;
};

struct PluginAutocmd
{
  std::string event;
  std::string callback;
};

struct PluginPanel
{
  std::string name;
  std::string callback;
  std::string title;
};

struct PluginStatusSegment
{
  std::string name;
  std::string callback; // registry key into lua_callbacks
  std::string side;     // "left" or "right"
  int priority = 50;
  int fg = -1; // optional xterm color override; -1 = theme status color
};

struct RenderedStatusSegment
{
  std::string text;
  std::string side;
  int priority = 50;
  int fg = -1;
};

struct PluginLoadStatus
{
  std::string name;
  std::string path;
  bool loaded;
  std::string error;
};

struct EventBusSubscriber
{
  std::string key;
  int ref;
};

#endif
