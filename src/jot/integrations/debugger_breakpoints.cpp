// Debugger breakpoint toggling and the breakpoint hover state used by the editor panel.
#include "commands/utils.h"
#include "editor.h"
#include "jot/lua/api.h"
#include "jot/integrations/debugger_internal.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace CommandLineUtils;
using namespace debugger_internal;

namespace debugger_internal
{
std::string normalize_path_string(const std::string &path)
{
  if (path.empty())
  {
    return "";
  }
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec)
  {
    p = fs::path(path);
  }
  return p.lexically_normal().string();
}
} // namespace debugger_internal

bool Editor::toggle_debugger_breakpoint(const std::string &filepath, int line)
{
  std::string path = normalize_path_string(filepath);
  if (path.empty() || line < 0)
  {
    return false;
  }
  auto &list = debugger_breakpoints[path];
  auto it = std::find_if(
      list.begin(), list.end(), [&](const DebuggerBreakpoint &bp) { return bp.line == line; });
  if (it == list.end())
  {
    list.push_back({path, line, false});
    std::sort(
        list.begin(), list.end(), [](const auto &a, const auto &b) { return a.line < b.line; });
    set_message("Breakpoint added: " + get_filename(path) + ":" + std::to_string(line + 1));
  }
  else
  {
    list.erase(it);
    set_message("Breakpoint removed: " + get_filename(path) + ":" + std::to_string(line + 1));
  }

  std::vector<int> lines;
  for (const auto &bp : list)
  {
    lines.push_back(bp.line);
  }
  for (auto &client : debugger_sessions)
  {
    if (client)
    {
      client->set_breakpoints(path, lines);
    }
  }
  needs_redraw = true;
  return true;
}

bool Editor::has_debugger_breakpoint(const std::string &filepath, int line) const
{
  std::string path = normalize_path_string(filepath);
  auto it = debugger_breakpoints.find(path);
  if (it == debugger_breakpoints.end())
  {
    return false;
  }
  return std::any_of(it->second.begin(),
                     it->second.end(),
                     [&](const DebuggerBreakpoint &bp) { return bp.line == line; });
}

void Editor::update_debugger_breakpoint_hover(int pane_index, int buffer_id, int line)
{
  if (buffer_id < 0 || buffer_id >= (int)buffers.size() || line < 0
      || line >= (int)buffers[buffer_id].line_count() || buffers[buffer_id].filepath.empty())
  {
    clear_debugger_breakpoint_hover();
    return;
  }

  if (debugger_breakpoint_hover_visible && debugger_breakpoint_hover_pane == pane_index
      && debugger_breakpoint_hover_buffer == buffer_id && debugger_breakpoint_hover_line == line)
  {
    return;
  }

  debugger_breakpoint_hover_visible = true;
  debugger_breakpoint_hover_pane = pane_index;
  debugger_breakpoint_hover_buffer = buffer_id;
  debugger_breakpoint_hover_line = line;
  needs_redraw = true;
}

void Editor::clear_debugger_breakpoint_hover()
{
  if (!debugger_breakpoint_hover_visible)
  {
    return;
  }
  debugger_breakpoint_hover_visible = false;
  debugger_breakpoint_hover_pane = -1;
  debugger_breakpoint_hover_buffer = -1;
  debugger_breakpoint_hover_line = -1;
  needs_redraw = true;
}

bool Editor::is_debugger_breakpoint_hover(int buffer_id, int line) const
{
  return debugger_breakpoint_hover_visible && debugger_breakpoint_hover_buffer == buffer_id
         && debugger_breakpoint_hover_line == line;
}
