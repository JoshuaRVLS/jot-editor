#include "tools/debugger/client.h"
#include "tools/debugger/internal.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
using ssize_t = intptr_t;
#define read _read
#define write _write
#define close _close
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;


using namespace dbg_internal;

DebuggerClient::DebuggerClient(const DebuggerSessionConfig &session_config,
                               const std::vector<std::string> &adapter_argv)
    : config(session_config), command(adapter_argv)
{
}

DebuggerClient::~DebuggerClient()
{
  stop();
}

bool DebuggerClient::start()
{
  if (running)
  {
    return true;
  }
  if (command.empty())
  {
    last_error = "empty debugger adapter command";
    return false;
  }

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;

  auto close_handle = [](HANDLE &handle)
  {
    if (handle)
    {
      CloseHandle(handle);
      handle = nullptr;
    }
  };

  if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)
      || !CreatePipe(&stdout_read, &stdout_write, &sa, 0)
      || !CreatePipe(&stderr_read, &stderr_write, &sa, 0))
  {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_read);
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stdout_write);
    close_handle(stderr_read);
    close_handle(stderr_write);
    return false;
  }

  SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = stdin_read;
  startup.hStdOutput = stdout_write;
  startup.hStdError = stderr_write;

  PROCESS_INFORMATION process{};
  std::string command_line = windows_command_line(command);
  std::string cwd = config.cwd.empty() ? std::string() : config.cwd;
  BOOL created = CreateProcessA(nullptr,
                                command_line.data(),
                                nullptr,
                                nullptr,
                                TRUE,
                                CREATE_NO_WINDOW,
                                nullptr,
                                cwd.empty() ? nullptr : cwd.c_str(),
                                &startup,
                                &process);

  close_handle(stdin_read);
  close_handle(stdout_write);
  close_handle(stderr_write);

  if (!created)
  {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stderr_read);
    return false;
  }

  CloseHandle(process.hThread);
  child_process_handle = process.hProcess;
  child_pid = (int)process.dwProcessId;
  stdin_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdin_write), _O_WRONLY | _O_BINARY);
  stdout_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdout_read), _O_RDONLY | _O_BINARY);
  stderr_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stderr_read), _O_RDONLY | _O_BINARY);
  if (stdin_fd < 0 || stdout_fd < 0 || stderr_fd < 0)
  {
    last_error = "failed to convert debugger pipes to file descriptors";
    stop();
    return false;
  }
#else
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    last_error = strerror(errno);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    last_error = strerror(errno);
    return false;
  }

  if (pid == 0)
  {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    if (!config.cwd.empty())
    {
      if (chdir(config.cwd.c_str()) != 0)
      {
        _exit(127);
      }
    }
    for (const auto &entry : config.env)
    {
      setenv(entry.first.c_str(), entry.second.c_str(), 1);
    }

    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (const auto &arg : command)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);
  stdin_fd = stdin_pipe[1];
  stdout_fd = stdout_pipe[0];
  stderr_fd = stderr_pipe[0];
  child_pid = pid;
#endif
  running = true;
  initialized = false;
  launched = false;
  next_request_id = 1;
  pending_requests.clear();
  pending_events.clear();
  outbound_buffer.clear();
  stdout_buffer.clear();
  stderr_buffer.clear();
  last_error.clear();

  set_non_blocking(stdin_fd);
  set_non_blocking(stdout_fd);
  set_non_blocking(stderr_fd);
  append_log(config.name, "INFO ", "Started " + describe());
  return initialize();
}

void DebuggerClient::stop()
{
  if (!running && stdin_fd < 0 && stdout_fd < 0 && stderr_fd < 0 && child_pid <= 0)
  {
    return;
  }
  running = false;
  initialized = false;
  launched = false;
  if (child_pid > 0)
  {
#ifdef _WIN32
    if (child_process_handle)
    {
      TerminateProcess(reinterpret_cast<HANDLE>(child_process_handle), 1);
      CloseHandle(reinterpret_cast<HANDLE>(child_process_handle));
      child_process_handle = nullptr;
    }
#else
    kill(child_pid, SIGTERM);
    waitpid(child_pid, nullptr, WNOHANG);
#endif
  }
  if (stdin_fd >= 0)
  {
    close(stdin_fd);
  }
  if (stdout_fd >= 0)
  {
    close(stdout_fd);
  }
  if (stderr_fd >= 0)
  {
    close(stderr_fd);
  }
  stdin_fd = stdout_fd = stderr_fd = -1;
  child_pid = -1;
#ifdef _WIN32
  child_process_handle = nullptr;
#endif
  outbound_buffer.clear();
  pending_requests.clear();
}

bool DebuggerClient::poll()
{
  if (!running)
  {
    return false;
  }
  bool changed = false;
  if (!outbound_buffer.empty())
  {
    changed = true;
    flush_pending_writes();
  }
  char buf[4096];
  while (stdout_fd >= 0)
  {
    if (!fd_has_data(stdout_fd))
    {
      break;
    }
    ssize_t n = read(stdout_fd, buf, sizeof(buf));
    if (n <= 0)
    {
      break;
    }
    handle_stdout_data(std::string(buf, buf + n));
    changed = true;
  }
  while (stderr_fd >= 0)
  {
    if (!fd_has_data(stderr_fd))
    {
      break;
    }
    ssize_t n = read(stderr_fd, buf, sizeof(buf));
    if (n <= 0)
    {
      break;
    }
    handle_stderr_data(std::string(buf, buf + n));
    changed = true;
  }
  int status = 0;
  if (child_pid > 0)
  {
#ifdef _WIN32
    HANDLE process = reinterpret_cast<HANDLE>(child_process_handle);
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
    {
      DWORD exit_code = 0;
      GetExitCodeProcess(process, &exit_code);
      running = false;
      initialized = false;
      launched = false;
      last_error = "adapter exited with status " + std::to_string(exit_code);
      push_error(last_error);
      CloseHandle(process);
      child_process_handle = nullptr;
      child_pid = -1;
      changed = true;
    }
#else
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid)
    {
      running = false;
      initialized = false;
      launched = false;
      last_error = WIFEXITED(status)
                       ? "adapter exited with status " + std::to_string(WEXITSTATUS(status))
                       : "adapter exited unexpectedly";
      push_error(last_error);
      changed = true;
    }
#endif
  }
  return changed;
}

bool DebuggerClient::initialize()
{
  std::string args = "{\"adapterID\":\"jot\",\"clientID\":\"jot\",\"clientName\":\"jot\","
                     "\"locale\":\"en-us\",\"pathFormat\":\"path\",\"linesStartAt1\":true,"
                     "\"columnsStartAt1\":true,\"supportsVariableType\":true,"
                     "\"supportsVariablePaging\":false,\"supportsRunInTerminalRequest\":false,"
                     "\"supportsMemoryReferences\":true}";
  return send_request("initialize", args);
}

bool DebuggerClient::launch_or_attach()
{
  if (config.attach)
  {
    std::ostringstream args;
    args << "{\"pid\":" << config.pid;
    if (!config.cwd.empty())
    {
      args << ",\"cwd\":\"" << Dap::json_escape(config.cwd) << "\"";
    }
    args << "}";
    launched = send_request("attach", args.str());
    return launched;
  }

  std::ostringstream args;
  args << "{\"program\":\"" << Dap::json_escape(config.program) << "\""
       << ",\"args\":" << json_array_strings(config.args) << ",\"cwd\":\""
       << Dap::json_escape(config.cwd.empty() ? "." : config.cwd) << "\""
       << ",\"env\":" << json_object_strings(config.env)
       << ",\"stopAtBeginningOfMainSubprogram\":false"
       << ",\"stopOnEntry\":false}";
  launched = send_request("launch", args.str());
  return launched;
}

bool DebuggerClient::disconnect(bool terminate_debuggee)
{
  return send_request("disconnect",
                      std::string("{\"terminateDebuggee\":")
                          + (terminate_debuggee ? "true" : "false") + "}");
}

bool DebuggerClient::configuration_done()
{
  return send_request("configurationDone");
}

bool DebuggerClient::continue_thread(int thread_id)
{
  return send_request("continue", "{\"threadId\":" + std::to_string(std::max(0, thread_id)) + "}");
}

bool DebuggerClient::pause_thread(int thread_id)
{
  return send_request("pause", "{\"threadId\":" + std::to_string(std::max(0, thread_id)) + "}");
}

bool DebuggerClient::next(int thread_id)
{
  return send_request("next", "{\"threadId\":" + std::to_string(std::max(0, thread_id)) + "}");
}

bool DebuggerClient::step_in(int thread_id)
{
  return send_request("stepIn", "{\"threadId\":" + std::to_string(std::max(0, thread_id)) + "}");
}

bool DebuggerClient::step_out(int thread_id)
{
  return send_request("stepOut", "{\"threadId\":" + std::to_string(std::max(0, thread_id)) + "}");
}

bool DebuggerClient::threads()
{
  return send_request("threads");
}

bool DebuggerClient::stack_trace(int thread_id)
{
  return send_request("stackTrace",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id))
                          + ",\"startFrame\":0,\"levels\":20}");
}

bool DebuggerClient::scopes(int frame_id)
{
  return send_request("scopes", "{\"frameId\":" + std::to_string(std::max(0, frame_id)) + "}");
}

bool DebuggerClient::variables(int variables_reference)
{
  return send_request("variables",
                      "{\"variablesReference\":" + std::to_string(std::max(0, variables_reference))
                          + "}");
}

bool DebuggerClient::read_memory(const std::string &memory_reference, int offset, int count)
{
  return send_request("readMemory",
                      "{\"memoryReference\":\"" + Dap::json_escape(memory_reference)
                          + "\",\"offset\":" + std::to_string(offset)
                          + ",\"count\":" + std::to_string(std::max(1, count)) + "}");
}

bool DebuggerClient::evaluate_and_read_memory(const std::string &expression, int count)
{
  pending_memory_read_count = std::clamp(count, 1, 1024);
  return evaluate(expression);
}

bool DebuggerClient::evaluate(const std::string &expression)
{
  return send_request("evaluate",
                      "{\"expression\":\"" + Dap::json_escape(expression) + "\"}");
}

bool DebuggerClient::disassemble(const std::string &memory_reference,
                                 int offset,
                                 int instruction_offset,
                                 int count)
{
  return send_request("disassemble",
                      "{\"memoryReference\":\"" + Dap::json_escape(memory_reference)
                          + "\",\"offset\":" + std::to_string(offset)
                          + ",\"instructionOffset\":" + std::to_string(instruction_offset)
                          + ",\"instructionCount\":" + std::to_string(std::max(1, count)) + "}");
}

bool DebuggerClient::set_breakpoints(const std::string &source_path,
                                     const std::vector<int> &zero_based_lines)
{
  std::ostringstream args;
  args << "{\"source\":{\"path\":\"" << Dap::json_escape(source_path) << "\"},\"breakpoints\":[";
  for (size_t i = 0; i < zero_based_lines.size(); i++)
  {
    if (i > 0)
    {
      args << ",";
    }
    args << "{\"line\":" << (zero_based_lines[i] + 1) << "}";
  }
  args << "]}";
  return send_request("setBreakpoints", args.str());
}

std::vector<DebuggerEvent> DebuggerClient::consume_events()
{
  auto out = std::move(pending_events);
  pending_events.clear();
  return out;
}

bool DebuggerClient::send_request(const std::string &command_name,
                                  const std::string &arguments_json)
{
  int request_id = next_request_id++;
  pending_requests[request_id] = command_name;
  std::ostringstream json;
  json << "{\"seq\":" << request_id << ",\"type\":\"request\",\"command\":\"" << command_name
       << "\",\"arguments\":" << (arguments_json.empty() ? "{}" : arguments_json) << "}";
  if (!send_message(json.str()))
  {
    pending_requests.erase(request_id);
    return false;
  }
  return true;
}

bool DebuggerClient::send_message(const std::string &json)
{
  if (!running || stdin_fd < 0)
  {
    return false;
  }
  outbound_buffer += "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
  append_log(config.name, "SEND ", json);
  return flush_pending_writes();
}

bool DebuggerClient::flush_pending_writes()
{
  while (!outbound_buffer.empty())
  {
    ssize_t written = write(stdin_fd, outbound_buffer.data(), outbound_buffer.size());
    if (written > 0)
    {
      outbound_buffer.erase(0, (size_t)written);
      continue;
    }
    if (written < 0 && errno == EINTR)
    {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      return true;
    }
    last_error = written < 0 ? strerror(errno) : "debugger stdin closed";
    push_error(last_error);
    running = false;
    return false;
  }
  return true;
}

void DebuggerClient::handle_stdout_data(const std::string &data)
{
  stdout_buffer += data;
  append_log(config.name, "RECV ", data);
  while (true)
  {
    size_t header_end = stdout_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos)
    {
      return;
    }
    size_t content_length = 0;
    if (!Dap::extract_content_length(stdout_buffer.substr(0, header_end), content_length))
    {
      stdout_buffer.erase(0, header_end + 4);
      push_error("DAP parse error: missing Content-Length");
      continue;
    }
    size_t body_start = header_end + 4;
    if (stdout_buffer.size() < body_start + content_length)
    {
      return;
    }
    std::string message = stdout_buffer.substr(body_start, content_length);
    stdout_buffer.erase(0, body_start + content_length);
    handle_message(message);
  }
}

void DebuggerClient::handle_stderr_data(const std::string &data)
{
  stderr_buffer += data;
  append_log(config.name, "STDERR ", data);
  DebuggerEvent ev;
  ev.type = DebuggerEvent::Output;
  ev.message = data;
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::handle_message(const std::string &message)
{
  Dap::Value root;
  if (!Dap::parse_json(message, root))
  {
    push_error("DAP parse error: invalid JSON");
    return;
  }
  std::string type = Dap::string_or_empty(Dap::object_get(root, "type"));
  if (type == "response")
  {
    handle_response(root);
  }
  else if (type == "event")
  {
    handle_event(root);
  }
}

void DebuggerClient::handle_response(const Dap::Value &root)
{
  int request_id = Dap::int_or_default(Dap::object_get(root, "request_seq"), 0);
  std::string command_name = Dap::string_or_empty(Dap::object_get(root, "command"));
  auto it = pending_requests.find(request_id);
  if (it != pending_requests.end())
  {
    command_name = it->second;
    pending_requests.erase(it);
  }
  bool success = Dap::bool_or_default(Dap::object_get(root, "success"), true);
  const Dap::Value *body = Dap::object_get(root, "body");
  if (!success)
  {
    push_error(Dap::string_or_empty(Dap::object_get(root, "message")));
    return;
  }
  if (command_name == "initialize" && body && body->type == Dap::Value::Object)
  {
    supports_read_memory_ =
        Dap::bool_or_default(Dap::object_get(*body, "supportsReadMemoryRequest"), false);
    supports_disassemble_ =
        Dap::bool_or_default(Dap::object_get(*body, "supportsDisassembleRequest"), false);
    initialized = true;
    DebuggerEvent ev;
    ev.type = DebuggerEvent::Capabilities;
    ev.supports_read_memory = supports_read_memory_;
    ev.supports_disassemble = supports_disassemble_;
    pending_events.push_back(std::move(ev));
    return;
  }
  if (!body || body->type != Dap::Value::Object)
  {
    return;
  }
  DebuggerEvent ev;
  if (command_name == "threads")
  {
    ev.type = DebuggerEvent::Threads;
    ev.threads = threads_from_body(*body);
  }
  else if (command_name == "stackTrace")
  {
    ev.type = DebuggerEvent::StackTrace;
    ev.frames = frames_from_body(*body);
  }
  else if (command_name == "scopes" || command_name == "variables")
  {
    ev.type = command_name == "scopes" ? DebuggerEvent::Scopes : DebuggerEvent::Variables;
    ev.variables = variables_from_body(*body);
  }
  else if (command_name == "readMemory")
  {
    ev.type = DebuggerEvent::Memory;
    ev.memory_rows = memory_from_body(*body);
  }
  else if (command_name == "evaluate")
  {
    // Evaluate-then-read chain (evaluate_and_read_memory): once the adapter
    // resolves the expression to an address (memoryReference field, or a
    // literal address as the evaluated result — GDB reports "0x…" for $pc
    // and &var), issue the readMemory right here. Errors surface as Error
    // events via the response handler below.
    const int count = pending_memory_read_count;
    pending_memory_read_count = -1;
    if (count > 0)
    {
      std::string ref = Dap::string_or_empty(Dap::object_get(*body, "memoryReference"));
      if (ref.empty())
      {
        const std::string result = Dap::string_or_empty(Dap::object_get(*body, "result"));
        if (Dap::looks_like_address(result))
        {
          ref = result;
        }
      }
      if (ref.empty())
      {
        push_error("Memory view: expression does not resolve to an address");
      }
      else
      {
        read_memory(ref, 0, count);
      }
      return;
    }
    return;
  }
  else if (command_name == "disassemble")
  {
    ev.type = DebuggerEvent::Disassembly;
    ev.instructions = instructions_from_body(*body);
  }
  else if (command_name == "setBreakpoints")
  {
    ev.type = DebuggerEvent::Breakpoints;
    std::string path;
    ev.breakpoints = breakpoints_from_body(*body, path);
  }
  else
  {
    return;
  }
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::handle_event(const Dap::Value &root)
{
  std::string name = Dap::string_or_empty(Dap::object_get(root, "event"));
  const Dap::Value *body = Dap::object_get(root, "body");
  DebuggerEvent ev;
  if (name == "initialized")
  {
    ev.type = DebuggerEvent::Initialized;
  }
  else if (name == "stopped")
  {
    ev.type = DebuggerEvent::Stopped;
    ev.message = body ? Dap::string_or_empty(Dap::object_get(*body, "reason")) : "";
    ev.thread_id = body ? Dap::int_or_default(Dap::object_get(*body, "threadId"), 0) : 0;
  }
  else if (name == "continued")
  {
    ev.type = DebuggerEvent::Continued;
    ev.thread_id = body ? Dap::int_or_default(Dap::object_get(*body, "threadId"), 0) : 0;
  }
  else if (name == "terminated")
  {
    ev.type = DebuggerEvent::Terminated;
  }
  else if (name == "exited")
  {
    ev.type = DebuggerEvent::Exited;
    ev.exit_code = body ? Dap::int_or_default(Dap::object_get(*body, "exitCode"), 0) : 0;
  }
  else if (name == "output")
  {
    ev.type = DebuggerEvent::Output;
    ev.message = body ? Dap::string_or_empty(Dap::object_get(*body, "output")) : "";
  }
  else
  {
    return;
  }
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::push_error(const std::string &message)
{
  DebuggerEvent ev;
  ev.type = DebuggerEvent::Error;
  ev.message = message.empty() ? "Debugger error" : message;
  pending_events.push_back(std::move(ev));
}

std::string DebuggerClient::describe() const
{
  return config.name + " (" + adapter_type(config.adapter) + ")";
}

std::vector<DebuggerSessionConfig> parse_debugger_config_text(const std::string &text)
{
  Dap::Value root;
  if (!Dap::parse_json(text, root) || root.type != Dap::Value::Object)
  {
    return {};
  }
  const Dap::Value *sessions = Dap::object_get(root, "sessions");
  if (!sessions || sessions->type != Dap::Value::Object)
  {
    return {};
  }
  std::vector<DebuggerSessionConfig> out;
  for (const auto &entry : sessions->object_value)
  {
    if (entry.second.type != Dap::Value::Object)
    {
      continue;
    }
    DebuggerSessionConfig config;
    config.name = entry.first;
    config.adapter = Dap::string_or_empty(Dap::object_get(entry.second, "adapter"));
    if (config.adapter.empty())
    {
      config.adapter = "gdb";
    }
    config.program = Dap::string_or_empty(Dap::object_get(entry.second, "program"));
    config.cwd = Dap::string_or_empty(Dap::object_get(entry.second, "cwd"));
    config.attach = Dap::bool_or_default(Dap::object_get(entry.second, "attach"), false);
    config.pid = Dap::int_or_default(Dap::object_get(entry.second, "pid"), 0);
    const Dap::Value *args = Dap::object_get(entry.second, "args");
    if (args && args->type == Dap::Value::Array)
    {
      for (const auto &arg : args->array_value)
      {
        if (arg.type == Dap::Value::String)
        {
          config.args.push_back(arg.string_value);
        }
      }
    }
    const Dap::Value *env = Dap::object_get(entry.second, "env");
    if (env && env->type == Dap::Value::Object)
    {
      for (const auto &env_entry : env->object_value)
      {
        if (env_entry.second.type == Dap::Value::String)
        {
          config.env[env_entry.first] = env_entry.second.string_value;
        }
      }
    }
    out.push_back(std::move(config));
  }
  std::sort(out.begin(),
            out.end(),
            [](const DebuggerSessionConfig &a, const DebuggerSessionConfig &b)
            { return a.name < b.name; });
  return out;
}

