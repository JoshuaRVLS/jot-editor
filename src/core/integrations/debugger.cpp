#include "editor.h"
#include "commands/utils.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace CommandLineUtils;

namespace {
bool command_exists(const std::string &name) {
  if (name.empty()) {
    return false;
  }
#ifdef _WIN32
  return std::system(("where " + name + " >NUL 2>NUL").c_str()) == 0;
#else
  return std::system(("command -v " + name + " >/dev/null 2>&1").c_str()) == 0;
#endif
}

std::vector<std::string> adapter_command_for(const std::string &adapter) {
  std::string lower = to_lower_copy(adapter);
  if (lower == "lldb" || lower == "lldb-dap") {
    return {"lldb-dap"};
  }
  return {"gdb", "--interpreter=dap"};
}

std::string adapter_binary_for(const std::string &adapter) {
  auto command = adapter_command_for(adapter);
  return command.empty() ? "" : command.front();
}

std::vector<std::string> split_shell_words(const std::string &text) {
  std::vector<std::string> out;
  std::string cur;
  bool single = false;
  bool dbl = false;
  bool esc = false;
  for (char c : text) {
    if (esc) {
      cur.push_back(c);
      esc = false;
      continue;
    }
    if (c == '\\' && !single) {
      esc = true;
      continue;
    }
    if (c == '\'' && !dbl) {
      single = !single;
      continue;
    }
    if (c == '"' && !single) {
      dbl = !dbl;
      continue;
    }
    if (std::isspace((unsigned char)c) && !single && !dbl) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
      continue;
    }
    cur.push_back(c);
  }
  if (!cur.empty()) {
    out.push_back(cur);
  }
  return out;
}

std::string default_debug_config_path(const std::string &root_dir) {
  if (!root_dir.empty()) {
    fs::path local = fs::path(root_dir) / ".jot" / "debug.json";
    std::error_code ec;
    if (fs::exists(local, ec) && !ec) {
      return local.string();
    }
  }
  const char *override_home = std::getenv("JOT_CONFIG_HOME");
  if (override_home && *override_home) {
    return (fs::path(override_home) / "configs" / "debug.json").string();
  }
#ifdef _WIN32
  const char *app_data = std::getenv("APPDATA");
  if (app_data && *app_data) {
    return (fs::path(app_data) / "jot" / "configs" / "debug.json").string();
  }
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home || !*home) {
    return "";
  }
  return (fs::path(home) / ".config" / "jot" / "configs" / "debug.json").string();
}

std::string normalize_path_string(const std::string &path) {
  if (path.empty()) {
    return "";
  }
  std::error_code ec;
  fs::path p = fs::absolute(path, ec);
  if (ec) {
    p = fs::path(path);
  }
  return p.lexically_normal().string();
}

std::string compact_output(std::string text, size_t max_size = 4000) {
  if (text.size() <= max_size) {
    return text;
  }
  return text.substr(text.size() - max_size);
}
} // namespace

DebuggerClient *Editor::get_debugger_session(int index) {
  if (debugger_sessions.empty()) {
    return nullptr;
  }
  int resolved = index < 0 ? current_debugger_session : index;
  if (resolved < 0 || resolved >= (int)debugger_sessions.size()) {
    return nullptr;
  }
  return debugger_sessions[resolved].get();
}

void Editor::toggle_debugger_panel() {
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
  show_right_panel = !show_right_panel;
  show_debugger_panel = show_right_panel;
  if (show_right_panel) {
    show_home_menu = false;
    load_debugger_configs();
  }
  update_pane_layout();
  needs_redraw = true;
}

bool Editor::handle_debugger_mouse(int x, int y, bool activate) {
  if (!show_right_panel || active_right_panel_tab != RIGHT_PANEL_DEBUG || !ui) {
    return false;
  }
  int panel_w = effective_right_panel_width();
  int panel_x = std::max(0, ui->get_render_width() - panel_w);
  int panel_y = 1;
  int panel_h = std::max(1, ui->get_height() - status_height - panel_y);
  if (panel_w <= 0 || x < panel_x || x >= panel_x + panel_w || y < panel_y ||
      y >= panel_y + panel_h) {
    return false;
  }
  if (!activate) {
    return true;
  }
  if (y == panel_y + 1) {
    int tab_x = panel_x + 1;
    for (int i = 0; i < (int)debugger_session_state.size(); i++) {
      const auto &state = debugger_session_state[i];
      std::string status =
          state.stopped ? " paused" : (state.running ? " run" : " done");
      std::string label = " " + state.name + status + " ";
      if (x >= tab_x && x < tab_x + (int)label.size()) {
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

bool Editor::start_debugger_session(DebuggerSessionConfig config) {
  if (config.name.empty()) {
    config.name = config.attach ? "attach " + std::to_string(config.pid)
                                : get_filename(config.program);
  }
  if (config.cwd.empty()) {
    config.cwd = root_dir.empty() ? "." : root_dir;
  }
  if (!config.attach && config.program.empty()) {
    show_right_panel = true;
    show_debugger_panel = true;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
    update_pane_layout();
    set_message("Usage: :debug <program> [args...]");
    return false;
  }

  std::string binary = adapter_binary_for(config.adapter);
  if (!command_exists(binary)) {
    show_right_panel = true;
    show_debugger_panel = true;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
    update_pane_layout();
    set_message("Debugger adapter missing: " + binary);
    return false;
  }

  auto client =
      std::make_unique<DebuggerClient>(config, adapter_command_for(config.adapter));
  if (!client->start()) {
    show_right_panel = true;
    show_debugger_panel = true;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
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
  show_right_panel = true;
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
  show_integrated_terminal = false;
  for (auto &term : integrated_terminals) {
    if (term) {
      term->set_focused(false);
    }
  }
  set_message("Debugger started: " + config.name);
  update_pane_layout();
  needs_redraw = true;
  return true;
}

void Editor::watch_debugger_client_fds(DebuggerClient *client) {
  if (!client) {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else

  auto watch_read = [this](int fd) {
    if (fd < 0 || event_loop_.is_watching_fd(fd)) {
      return;
    }
    event_loop_.watch_fd(fd, true, false, [this, fd] {
      bool found = false;
      for (auto &client : debugger_sessions) {
        if (!client) {
          continue;
        }
        if (client->get_stdout_fd() == fd || client->get_stderr_fd() == fd) {
          found = true;
          break;
        }
      }
      if (!found) {
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

void Editor::unwatch_debugger_client_fds(DebuggerClient *client) {
  if (!client) {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else
  if (client->get_stdout_fd() >= 0) {
    event_loop_.unwatch_fd(client->get_stdout_fd());
  }
  if (client->get_stderr_fd() >= 0) {
    event_loop_.unwatch_fd(client->get_stderr_fd());
  }
#endif
}

bool Editor::start_debugger_command(const std::string &adapter,
                                    const std::string &command_line) {
  auto parts = split_shell_words(command_line);
  if (parts.empty()) {
    show_right_panel = true;
    show_debugger_panel = true;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
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

bool Editor::attach_debugger_command(const std::string &adapter,
                                     const std::string &pid_text) {
  std::string trimmed = trim_copy(pid_text);
  if (trimmed.empty()) {
    show_right_panel = true;
    show_debugger_panel = true;
    active_right_panel_tab = RIGHT_PANEL_DEBUG;
    update_pane_layout();
    set_message("Usage: :debugattach <pid>");
    return false;
  }
  int pid = 0;
  try {
    pid = std::stoi(trimmed);
  } catch (...) {
    set_message("Invalid pid: " + trimmed);
    return false;
  }
  if (pid <= 0) {
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

void Editor::stop_debugger_session() {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  client->disconnect(true);
  unwatch_debugger_client_fds(client);
  client->stop();
  if (current_debugger_session >= 0 &&
      current_debugger_session < (int)debugger_session_state.size()) {
    debugger_session_state[current_debugger_session].running = false;
    debugger_session_state[current_debugger_session].stopped = false;
  }
  set_message("Debugger stopped");
  needs_redraw = true;
}

void Editor::restart_debugger_session() {
  if (current_debugger_session < 0 ||
      current_debugger_session >= (int)debugger_sessions.size()) {
    set_message("Debugger: no active session");
    return;
  }
  DebuggerSessionConfig config =
      debugger_sessions[current_debugger_session]->get_config();
  unwatch_debugger_client_fds(debugger_sessions[current_debugger_session].get());
  debugger_sessions[current_debugger_session]->stop();
  debugger_sessions.erase(debugger_sessions.begin() + current_debugger_session);
  debugger_session_state.erase(debugger_session_state.begin() +
                               current_debugger_session);
  current_debugger_session =
      debugger_sessions.empty() ? -1 : std::min(current_debugger_session,
                                                (int)debugger_sessions.size() - 1);
  start_debugger_session(config);
}

void Editor::continue_debugger_session() {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  int thread_id = current_debugger_session >= 0
                      ? debugger_session_state[current_debugger_session]
                            .active_thread_id
                      : 0;
  client->continue_thread(thread_id);
}

void Editor::pause_debugger_session() {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  int thread_id = current_debugger_session >= 0
                      ? debugger_session_state[current_debugger_session]
                            .active_thread_id
                      : 0;
  client->pause_thread(thread_id);
}

void Editor::step_debugger_in() {
  if (DebuggerClient *client = get_debugger_session()) {
    client->step_in(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::step_debugger_next() {
  if (DebuggerClient *client = get_debugger_session()) {
    client->next(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::step_debugger_out() {
  if (DebuggerClient *client = get_debugger_session()) {
    client->step_out(debugger_session_state[current_debugger_session].active_thread_id);
  }
}

void Editor::show_debugger_threads() {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  client->threads();
  show_debugger_panel = true;
  show_right_panel = true;
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
  needs_redraw = true;
}

void Editor::request_debugger_memory(const std::string &expression, int bytes) {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  if (!client->supports_read_memory()) {
    set_message("Debugger does not support memory view");
    return;
  }
  std::string ref = trim_copy(expression);
  if (ref.empty()) {
    ref = "$pc";
  }
  client->read_memory(ref, 0, std::clamp(bytes, 1, 1024));
  show_debugger_panel = true;
  show_right_panel = true;
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
}

void Editor::request_debugger_disassembly(const std::string &expression) {
  DebuggerClient *client = get_debugger_session();
  if (!client) {
    set_message("Debugger: no active session");
    return;
  }
  if (!client->supports_disassemble()) {
    set_message("Debugger does not support disassembly");
    return;
  }
  std::string ref = trim_copy(expression);
  if (ref.empty()) {
    ref = "$pc";
  }
  client->disassemble(ref, 0, 0, 24);
  show_debugger_panel = true;
  show_right_panel = true;
  active_right_panel_tab = RIGHT_PANEL_DEBUG;
}

bool Editor::toggle_debugger_breakpoint(const std::string &filepath, int line) {
  std::string path = normalize_path_string(filepath);
  if (path.empty() || line < 0) {
    return false;
  }
  auto &list = debugger_breakpoints[path];
  auto it = std::find_if(list.begin(), list.end(),
                         [&](const DebuggerBreakpoint &bp) {
                           return bp.line == line;
                         });
  if (it == list.end()) {
    list.push_back({path, line, false});
    std::sort(list.begin(), list.end(), [](const auto &a, const auto &b) {
      return a.line < b.line;
    });
    set_message("Breakpoint added: " + get_filename(path) + ":" +
                std::to_string(line + 1));
  } else {
    list.erase(it);
    set_message("Breakpoint removed: " + get_filename(path) + ":" +
                std::to_string(line + 1));
  }

  std::vector<int> lines;
  for (const auto &bp : list) {
    lines.push_back(bp.line);
  }
  for (auto &client : debugger_sessions) {
    if (client) {
      client->set_breakpoints(path, lines);
    }
  }
  needs_redraw = true;
  return true;
}

bool Editor::has_debugger_breakpoint(const std::string &filepath, int line) const {
  std::string path = normalize_path_string(filepath);
  auto it = debugger_breakpoints.find(path);
  if (it == debugger_breakpoints.end()) {
    return false;
  }
  return std::any_of(it->second.begin(), it->second.end(),
                     [&](const DebuggerBreakpoint &bp) {
                       return bp.line == line;
                     });
}

void Editor::update_debugger_breakpoint_hover(int pane_index, int buffer_id,
                                              int line) {
  if (buffer_id < 0 || buffer_id >= (int)buffers.size() || line < 0 ||
      line >= (int)buffers[buffer_id].line_count() ||
      buffers[buffer_id].filepath.empty()) {
    clear_debugger_breakpoint_hover();
    return;
  }

  if (debugger_breakpoint_hover_visible &&
      debugger_breakpoint_hover_pane == pane_index &&
      debugger_breakpoint_hover_buffer == buffer_id &&
      debugger_breakpoint_hover_line == line) {
    return;
  }

  debugger_breakpoint_hover_visible = true;
  debugger_breakpoint_hover_pane = pane_index;
  debugger_breakpoint_hover_buffer = buffer_id;
  debugger_breakpoint_hover_line = line;
  needs_redraw = true;
}

void Editor::clear_debugger_breakpoint_hover() {
  if (!debugger_breakpoint_hover_visible) {
    return;
  }
  debugger_breakpoint_hover_visible = false;
  debugger_breakpoint_hover_pane = -1;
  debugger_breakpoint_hover_buffer = -1;
  debugger_breakpoint_hover_line = -1;
  needs_redraw = true;
}

bool Editor::is_debugger_breakpoint_hover(int buffer_id, int line) const {
  return debugger_breakpoint_hover_visible &&
         debugger_breakpoint_hover_buffer == buffer_id &&
         debugger_breakpoint_hover_line == line;
}

void Editor::load_debugger_configs() {
  debugger_configs.clear();
  std::string path = default_debug_config_path(root_dir);
  if (path.empty()) {
    return;
  }
  std::ifstream in(path);
  if (!in.is_open()) {
    return;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  debugger_configs = parse_debugger_config_text(ss.str());
  for (auto &config : debugger_configs) {
    if (config.cwd.empty()) {
      config.cwd = root_dir.empty() ? "." : root_dir;
    }
  }
}

std::vector<std::string> Editor::list_debugger_config_names() {
  load_debugger_configs();
  std::vector<std::string> names;
  for (const auto &config : debugger_configs) {
    names.push_back(config.name);
  }
  return names;
}

bool Editor::run_debugger_config(const std::string &name) {
  load_debugger_configs();
  if (debugger_configs.empty()) {
    set_message("No debug configs found");
    return false;
  }
  std::string needle = to_lower_copy(trim_copy(name));
  for (auto config : debugger_configs) {
    if (needle.empty() || to_lower_copy(config.name) == needle) {
      return start_debugger_session(config);
    }
  }
  set_message("Debug config not found: " + name);
  return false;
}

void Editor::poll_debugger_sessions() {
  for (int i = 0; i < (int)debugger_sessions.size(); i++) {
    auto &client = debugger_sessions[i];
    if (!client) {
      continue;
    }
    if (client->poll()) {
      needs_redraw = true;
    }
    auto events = client->consume_events();
    for (const auto &event : events) {
      auto &state = debugger_session_state[i];
      switch (event.type) {
      case DebuggerEvent::Capabilities:
        state.supports_read_memory = event.supports_read_memory;
        state.supports_disassemble = event.supports_disassemble;
        break;
      case DebuggerEvent::Initialized:
        for (const auto &entry : debugger_breakpoints) {
          std::vector<int> lines;
          for (const auto &bp : entry.second) {
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
        if (event.thread_id > 0) {
          state.active_thread_id = event.thread_id;
        }
        client->threads();
        if (state.active_thread_id > 0) {
          client->stack_trace(state.active_thread_id);
        }
        set_message("Debugger stopped: " + event.message);
        break;
      case DebuggerEvent::Continued:
        state.running = true;
        state.stopped = false;
        if (event.thread_id > 0) {
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
        state.output = compact_output(state.output + event.message);
        break;
      case DebuggerEvent::Threads:
        state.threads = event.threads;
        if (state.active_thread_id <= 0 && !state.threads.empty()) {
          state.active_thread_id = state.threads.front().id;
          client->stack_trace(state.active_thread_id);
        }
        break;
      case DebuggerEvent::StackTrace:
        if (state.threads.empty()) {
          DebuggerThread thread;
          thread.id = state.active_thread_id;
          thread.name = "thread " + std::to_string(thread.id);
          state.threads.push_back(thread);
        }
        for (auto &thread : state.threads) {
          if (thread.id == state.active_thread_id) {
            thread.frames = event.frames;
            if (!event.frames.empty()) {
              state.active_frame_id = event.frames.front().id;
              if (!event.frames.front().filepath.empty()) {
                open_file(event.frames.front().filepath, true);
                if (current_buffer >= 0 && current_buffer < (int)buffers.size()) {
                  auto &buf = get_buffer();
                  buf.cursor.y = std::clamp(
                      event.frames.front().line, 0,
                      std::max(0, (int)buf.line_count() - 1));
                  buf.cursor.x =
                      std::clamp(event.frames.front().column, 0,
                                 (int)buf.line(buf.cursor.y).size());
                  ensure_cursor_visible();
                }
              }
              client->scopes(state.active_frame_id);
            }
            break;
          }
        }
        break;
      case DebuggerEvent::Scopes:
      case DebuggerEvent::Variables:
        state.variables = event.variables;
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
}
