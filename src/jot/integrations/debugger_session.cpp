// Debugger session lifecycle: start/attach, fd watching, stepping, thread/frame cycling, output polling.
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
std::string compact_output(std::string text, size_t max_size)
{
  if (text.size() <= max_size)
  {
    return text;
  }
  return text.substr(text.size() - max_size);
}
} // namespace debugger_internal

DebuggerClient *Editor::get_debugger_session(int index)
{
  if (debugger_sessions.empty())
  {
    return nullptr;
  }
  int resolved = index < 0 ? current_debugger_session : index;
  if (resolved < 0 || resolved >= (int)debugger_sessions.size())
  {
    return nullptr;
  }
  return debugger_sessions[resolved].get();
}

void Editor::toggle_debugger_panel()
{
  if (show_right_panel && active_right_panel_tab == RIGHT_PANEL_DEBUG)
  {
    close_right_panel_tab(RIGHT_PANEL_DEBUG);
    show_debugger_panel = false;
    update_pane_layout();
    needs_redraw = true;
    return;
  }
  open_right_panel_tab(RIGHT_PANEL_DEBUG);
  show_debugger_panel = true;
  show_home_menu = false;
  load_debugger_configs();
  update_pane_layout();
  needs_redraw = true;
}

bool Editor::handle_debugger_mouse(int x,
                                   int y,
                                   bool activate,
                                   bool wheel_up,
                                   bool wheel_down)
{
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_DEBUG || !ui)
  {
    return false;
  }
  int panel_w = effective_right_panel_width();
  int panel_x = std::max(0, ui->get_render_width() - panel_w);
  int panel_y = topbar_height();
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  if (panel_w <= 0 || x < panel_x || x >= panel_x + panel_w || y < panel_y
      || y >= panel_y + panel_h)
  {
    return false;
  }
  // Wheel over the panel scrolls the output history (the only scrollable
  // section); the render clamps the offset to the visible window.
  if (wheel_up || wheel_down)
  {
    debugger_scroll_output(wheel_up ? 3 : -3);
    needs_redraw = true;
    return true;
  }
  if (!activate)
  {
    return true;
  }
  if (y == panel_y + 2)
  {
    int tab_x = panel_x + 1;
    for (int i = 0; i < (int)debugger_session_state.size(); i++)
    {
      const auto &state = debugger_session_state[i];
      std::string status = state.stopped ? " paused" : (state.running ? " run" : " done");
      std::string label = " " + state.name + status + " ";
      if (x >= tab_x && x < tab_x + (int)label.size())
      {
        current_debugger_session = i;
        needs_redraw = true;
        return true;
      }
      tab_x += (int)label.size() + 1;
    }
  }
  needs_redraw = true;
  return true;
}

void Editor::debugger_scroll_output(int delta_lines)
{
  if (current_debugger_session < 0
      || current_debugger_session >= (int)debugger_session_state.size())
  {
    return;
  }
  auto &state = debugger_session_state[current_debugger_session];
  // Rough clamp here; the renderer re-clamps against the visible rows.
  state.output_scroll = std::max(0, state.output_scroll + delta_lines);
  needs_redraw = true;
}

void Editor::debugger_cycle_thread(int delta)
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  auto &state = debugger_session_state[current_debugger_session];
  if (!state.stopped)
  {
    set_message("Debugger: not stopped");
    return;
  }
  if (state.threads.empty())
  {
    set_message("Debugger: no threads");
    return;
  }
  int index = 0;
  for (size_t i = 0; i < state.threads.size(); i++)
  {
    if (state.threads[i].id == state.active_thread_id)
    {
      index = (int)i;
      break;
    }
  }
  index = (index + delta + (int)state.threads.size()) % (int)state.threads.size();
  const int new_thread = state.threads[(size_t)index].id;
  state.active_thread_id = new_thread;
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
  update_pane_layout();
  // The stack-trace response refreshes frames, jumps to the new top frame
  // and re-requests its variables.
  client->stack_trace(new_thread);
  set_message("Thread " + std::to_string(new_thread) + " "
              + state.threads[(size_t)index].name);
  needs_redraw = true;
}

void Editor::debugger_cycle_frame(int delta)
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  auto &state = debugger_session_state[current_debugger_session];
  if (!state.stopped)
  {
    set_message("Debugger: not stopped");
    return;
  }
  DebuggerThread *thread = nullptr;
  for (auto &t : state.threads)
  {
    if (t.id == state.active_thread_id)
    {
      thread = &t;
      break;
    }
  }
  if (!thread || thread->frames.empty())
  {
    set_message("Debugger: no stack frames");
    return;
  }
  int index = 0;
  for (size_t i = 0; i < thread->frames.size(); i++)
  {
    if (thread->frames[i].id == state.active_frame_id)
    {
      index = (int)i;
      break;
    }
  }
  index = std::clamp(index + delta, 0, (int)thread->frames.size() - 1);
  const DebuggerFrame &frame = thread->frames[(size_t)index];
  state.active_frame_id = frame.id;
  jump_to_debugger_frame(frame);
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
  update_pane_layout();
  // Refresh the variable list for the frame we landed on.
  client->scopes(frame.id);
  std::string loc = frame.filepath.empty()
                        ? frame.name
                        : get_filename(frame.filepath) + ":" + std::to_string(frame.line + 1);
  set_message("Frame " + std::to_string(index + 1) + "/"
              + std::to_string(thread->frames.size()) + "  " + loc);
  needs_redraw = true;
}

void Editor::jump_to_debugger_frame(const DebuggerFrame &frame)
{
  if (frame.filepath.empty())
  {
    return;
  }
  open_file(frame.filepath, true);
  if (current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    auto &buf = get_buffer();
    buf.cursor.y = std::clamp(frame.line, 0, std::max(0, (int)buf.line_count() - 1));
    buf.cursor.x = std::clamp(frame.column, 0, (int)buf.line(buf.cursor.y).size());
    ensure_cursor_visible();
  }
}

bool Editor::start_debugger_session(DebuggerSessionConfig config)
{
  if (config.name.empty())
  {
    config.name =
        config.attach ? "attach " + std::to_string(config.pid) : get_filename(config.program);
  }
  if (config.cwd.empty())
  {
    config.cwd = root_dir.empty() ? "." : root_dir;
  }
  if (!config.attach && config.program.empty())
  {
        show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
    update_pane_layout();
    set_message("Usage: :debug <program> [args...]");
    return false;
  }

  std::string binary = adapter_binary_for(config.adapter);
  if (!command_exists(binary))
  {
        show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
    update_pane_layout();
    set_message("Debugger adapter missing: " + binary);
    return false;
  }

  auto client = std::make_unique<DebuggerClient>(config, adapter_command_for(config.adapter));
  if (!client->start())
  {
        show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
    update_pane_layout();
    set_message("Debugger start failed: " + client->get_last_error());
    return false;
  }

  DebuggerSessionState state;
  state.name = config.name;
  state.adapter = config.adapter;
  state.program = config.program;
  state.running = true;

  debugger_sessions.push_back(std::move(client));
  watch_debugger_client_fds(debugger_sessions.back().get());
  debugger_session_state.push_back(std::move(state));
  current_debugger_session = (int)debugger_sessions.size() - 1;
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
  show_integrated_terminal = false;
  for (auto &term : integrated_terminals)
  {
    if (term)
    {
      term->set_focused(false);
    }
  }
  set_message("Debugger started: " + config.name);
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::watch_debugger_client_fds(DebuggerClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else

  auto watch_read = [this](int fd)
  {
    if (fd < 0 || event_loop_.is_watching_fd(fd))
    {
      return;
    }
    event_loop_.watch_fd(fd,
                         true,
                         false,
                         [this, fd]
                         {
                           bool found = false;
                           for (auto &client : debugger_sessions)
                           {
                             if (!client)
                             {
                               continue;
                             }
                             if (client->get_stdout_fd() == fd || client->get_stderr_fd() == fd)
                             {
                               found = true;
                               break;
                             }
                           }
                           if (!found)
                           {
                             event_loop_.unwatch_fd(fd);
                             return;
                           }
                           poll_debugger_sessions();
                         });
  };

  watch_read(client->get_stdout_fd());
  watch_read(client->get_stderr_fd());
#endif
}

void Editor::unwatch_debugger_client_fds(DebuggerClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else
  if (client->get_stdout_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stdout_fd());
  }
  if (client->get_stderr_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stderr_fd());
  }
#endif
}

bool Editor::start_debugger_command(const std::string &adapter, const std::string &command_line)
{
  auto parts = split_shell_words(command_line);
  if (parts.empty())
  {
        show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
    update_pane_layout();
    set_message("Usage: :debug <program> [args...]");
    return false;
  }
  DebuggerSessionConfig config;
  config.adapter = adapter.empty() ? "gdb" : adapter;
  config.program = parts.front();
  config.args.assign(parts.begin() + 1, parts.end());
  config.cwd = root_dir.empty() ? "." : root_dir;
  config.name = get_filename(config.program);
  return start_debugger_session(config);
}

bool Editor::attach_debugger_command(const std::string &adapter, const std::string &pid_text)
{
  std::string trimmed = trim_copy(pid_text);
  if (trimmed.empty())
  {
        show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
    update_pane_layout();
    set_message("Usage: :debugattach <pid>");
    return false;
  }
  int pid = 0;
  try
  {
    pid = std::stoi(trimmed);
  }
  catch (...)
  {
    set_message("Invalid pid: " + trimmed);
    return false;
  }
  if (pid <= 0)
  {
    set_message("Invalid pid: " + trimmed);
    return false;
  }
  DebuggerSessionConfig config;
  config.adapter = adapter.empty() ? "gdb" : adapter;
  config.attach = true;
  config.pid = pid;
  config.cwd = root_dir.empty() ? "." : root_dir;
  config.name = "pid " + std::to_string(pid);
  return start_debugger_session(config);
}

void Editor::stop_debugger_session()
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  client->disconnect(true);
  unwatch_debugger_client_fds(client);
  client->stop();
  if (current_debugger_session >= 0
      && current_debugger_session < (int)debugger_session_state.size())
  {
    debugger_session_state[current_debugger_session].running = false;
    debugger_session_state[current_debugger_session].stopped = false;
  }
  set_message("Debugger stopped");
  needs_redraw = true;
}

void Editor::restart_debugger_session()
{
  if (current_debugger_session < 0 || current_debugger_session >= (int)debugger_sessions.size())
  {
    set_message("Debugger: no active session");
    return;
  }
  DebuggerSessionConfig config = debugger_sessions[current_debugger_session]->get_config();
  unwatch_debugger_client_fds(debugger_sessions[current_debugger_session].get());
  debugger_sessions[current_debugger_session]->stop();
  debugger_sessions.erase(debugger_sessions.begin() + current_debugger_session);
  debugger_session_state.erase(debugger_session_state.begin() + current_debugger_session);
  current_debugger_session =
      debugger_sessions.empty()
          ? -1
          : std::min(current_debugger_session, (int)debugger_sessions.size() - 1);
  start_debugger_session(config);
}

void Editor::continue_debugger_session()
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  int thread_id = current_debugger_session >= 0
                      ? debugger_session_state[current_debugger_session].active_thread_id
                      : 0;
  client->continue_thread(thread_id);
}

void Editor::pause_debugger_session()
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  int thread_id = current_debugger_session >= 0
                      ? debugger_session_state[current_debugger_session].active_thread_id
                      : 0;
  client->pause_thread(thread_id);
}

void Editor::step_debugger_in()
{
  if (DebuggerClient *client = get_debugger_session())
  {
    client->step_in(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::step_debugger_next()
{
  if (DebuggerClient *client = get_debugger_session())
  {
    client->next(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::step_debugger_out()
{
  if (DebuggerClient *client = get_debugger_session())
  {
    client->step_out(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::show_debugger_threads()
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  client->threads();
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
  needs_redraw = true;
}

void Editor::request_debugger_memory(const std::string &expression, int bytes)
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  if (!client->supports_read_memory())
  {
    set_message("Debugger does not support memory view");
    return;
  }
  std::string ref = trim_copy(expression);
  if (ref.empty())
  {
    ref = "$pc";
  }
  // Literal addresses go straight to readMemory; anything else (a variable,
  // $register, &expr, ...) is evaluated first so the adapter can hand back
  // an address (GDB only accepts literals in readMemory).
  if (Dap::looks_like_address(ref))
  {
    client->read_memory(ref, 0, std::clamp(bytes, 1, 1024));
  }
  else
  {
    client->evaluate_and_read_memory(ref, std::clamp(bytes, 1, 1024));
  }
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
}

void Editor::poll_debugger_sessions()
{
  for (int i = 0; i < (int)debugger_sessions.size(); i++)
  {
    auto &client = debugger_sessions[i];
    if (!client)
    {
      continue;
    }
    if (client->poll())
    {
      needs_redraw = true;
    }
    auto events = client->consume_events();
    for (const auto &event : events)
    {
      auto &state = debugger_session_state[i];
      switch (event.type)
      {
      case DebuggerEvent::Capabilities:
        state.supports_read_memory = event.supports_read_memory;
        state.supports_disassemble = event.supports_disassemble;
        break;
      case DebuggerEvent::Initialized:
        for (const auto &entry : debugger_breakpoints)
        {
          std::vector<int> lines;
          for (const auto &bp : entry.second)
          {
            lines.push_back(bp.line);
          }
          client->set_breakpoints(entry.first, lines);
        }
        client->launch_or_attach();
        client->configuration_done();
        break;
      case DebuggerEvent::Stopped:
        state.running = true;
        state.stopped = true;
        if (event.thread_id > 0)
        {
          state.active_thread_id = event.thread_id;
        }
        client->threads();
        if (state.active_thread_id > 0)
        {
          client->stack_trace(state.active_thread_id);
        }
        // Surface the panel so the paused state is actually visible.
                show_debugger_panel = true;
        open_right_panel_tab(RIGHT_PANEL_DEBUG);
        update_pane_layout();
        set_message("Debugger stopped: " + event.message);
        break;
      case DebuggerEvent::Continued:
        state.running = true;
        state.stopped = false;
        if (event.thread_id > 0)
        {
          state.active_thread_id = event.thread_id;
        }
        break;
      case DebuggerEvent::Terminated:
      case DebuggerEvent::Exited:
        state.running = false;
        state.stopped = false;
        set_message("Debugger exited");
        break;
      case DebuggerEvent::Output:
      {
        // Keep the scroll position anchored when the user is scrolled up;
        // pinned-to-bottom output just stays pinned.
        const int old_lines =
            (int)std::count(state.output.begin(), state.output.end(), '\n');
        state.output = compact_output(state.output + event.message);
        const int new_lines =
            (int)std::count(state.output.begin(), state.output.end(), '\n');
        if (state.output_scroll > 0)
        {
          state.output_scroll += new_lines - old_lines;
        }
        break;
      }
      case DebuggerEvent::Threads:
        state.threads = event.threads;
        if (lua_api)
        {
          lua_api->try_deliver_debugger_threads(i, event.threads);
        }
        if (state.active_thread_id <= 0 && !state.threads.empty())
        {
          state.active_thread_id = state.threads.front().id;
          client->stack_trace(state.active_thread_id);
        }
        break;
      case DebuggerEvent::StackTrace:
      {
        if (state.threads.empty())
        {
          DebuggerThread thread;
          thread.id = state.active_thread_id;
          thread.name = "thread " + std::to_string(thread.id);
          state.threads.push_back(thread);
        }
        for (auto &thread : state.threads)
        {
          if (thread.id == state.active_thread_id)
          {
            thread.frames = event.frames;
            const bool lua_took = lua_api && lua_api->try_deliver_debugger_stack(i, event.frames);
            if (!event.frames.empty())
            {
              state.active_frame_id = event.frames.front().id;
              if (!lua_took)
              {
                jump_to_debugger_frame(event.frames.front());
                client->scopes(state.active_frame_id);
              }
            }
            break;
          }
        }
        break;
      }
      case DebuggerEvent::Scopes:
      case DebuggerEvent::Variables:
        state.variables = event.variables;
        if (lua_api)
        {
          if (event.type == DebuggerEvent::Scopes)
          {
            // request_variables chain: ask the adapter for the first
            // expandable scope's variables; the sink stays pending for the
            // Variables event that follows.
            lua_api->debugger_chain_variables(i, event.variables);
          }
          else
          {
            lua_api->try_deliver_debugger_variables(i, event.variables);
          }
        }
        break;
      case DebuggerEvent::Memory:
        state.memory_rows = event.memory_rows;
        break;
      case DebuggerEvent::Disassembly:
        state.instructions = event.instructions;
        break;
      case DebuggerEvent::Error:
        state.last_error = event.message;
        set_message("Debugger: " + event.message);
        break;
      default:
        break;
      }
    }
  }
  if (lua_api)
  {
    lua_api->emit_debugger_state_changed();
  }
}

void Editor::request_debugger_disassembly(const std::string &expression)
{
  DebuggerClient *client = get_debugger_session();
  if (!client)
  {
    set_message("Debugger: no active session");
    return;
  }
  if (!client->supports_disassemble())
  {
    set_message("Debugger does not support disassembly");
    return;
  }
  std::string ref = trim_copy(expression);
  if (ref.empty())
  {
    ref = "$pc";
  }
  client->disassemble(ref, 0, 0, 24);
  show_debugger_panel = true;
    open_right_panel_tab(RIGHT_PANEL_DEBUG);
}
