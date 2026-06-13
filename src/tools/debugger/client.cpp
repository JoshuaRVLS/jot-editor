#include "debugger_client.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
bool set_non_blocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string debug_log_path(const std::string &name) {
  const char *home = getenv("HOME");
  fs::path base = home ? fs::path(home) / ".config" / "jot" / "logs"
                       : fs::temp_directory_path() / "jot-logs";
  std::error_code ec;
  fs::create_directories(base, ec);
  std::string safe = name.empty() ? "session" : name;
  for (char &c : safe) {
    if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') {
      c = '_';
    }
  }
  return (base / ("debug_" + safe + ".log")).string();
}

void append_log(const std::string &name, const std::string &prefix,
                const std::string &line) {
  std::ofstream log(debug_log_path(name), std::ios::app);
  if (!log.is_open()) {
    return;
  }
  log << prefix << line << "\n";
}

void skip_ws(const std::string &text, size_t &pos) {
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    pos++;
  }
}

bool parse_json_string(const std::string &text, size_t &pos, std::string &out) {
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  pos++;
  out.clear();
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == '"') {
      return true;
    }
    if (c == '\\') {
      if (pos >= text.size()) {
        return false;
      }
      char esc = text[pos++];
      switch (esc) {
      case '"':
      case '\\':
      case '/':
        out.push_back(esc);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        if (pos + 4 > text.size()) {
          return false;
        }
        unsigned int codepoint = 0;
        for (int i = 0; i < 4; i++) {
          char h = text[pos++];
          codepoint <<= 4;
          if (h >= '0' && h <= '9') {
            codepoint |= (unsigned int)(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            codepoint |= (unsigned int)(10 + h - 'a');
          } else if (h >= 'A' && h <= 'F') {
            codepoint |= (unsigned int)(10 + h - 'A');
          } else {
            return false;
          }
        }
        if (codepoint <= 0x7F) {
          out.push_back((char)codepoint);
        } else if (codepoint <= 0x7FF) {
          out.push_back((char)(0xC0 | ((codepoint >> 6) & 0x1F)));
          out.push_back((char)(0x80 | (codepoint & 0x3F)));
        } else {
          out.push_back((char)(0xE0 | ((codepoint >> 12) & 0x0F)));
          out.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
          out.push_back((char)(0x80 | (codepoint & 0x3F)));
        }
        break;
      }
      default:
        return false;
      }
      continue;
    }
    out.push_back(c);
  }
  return false;
}

bool parse_json_value(const std::string &text, size_t &pos, Dap::Value &out);

bool parse_json_array(const std::string &text, size_t &pos, Dap::Value &out) {
  if (pos >= text.size() || text[pos] != '[') {
    return false;
  }
  pos++;
  out = Dap::Value{};
  out.type = Dap::Value::Array;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == ']') {
    pos++;
    return true;
  }
  while (pos < text.size()) {
    Dap::Value item;
    if (!parse_json_value(text, pos, item)) {
      return false;
    }
    out.array_value.push_back(std::move(item));
    skip_ws(text, pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == ']') {
      pos++;
      return true;
    }
    if (text[pos] != ',') {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}

bool parse_json_object(const std::string &text, size_t &pos, Dap::Value &out) {
  if (pos >= text.size() || text[pos] != '{') {
    return false;
  }
  pos++;
  out = Dap::Value{};
  out.type = Dap::Value::Object;
  skip_ws(text, pos);
  if (pos < text.size() && text[pos] == '}') {
    pos++;
    return true;
  }
  while (pos < text.size()) {
    std::string key;
    if (!parse_json_string(text, pos, key)) {
      return false;
    }
    skip_ws(text, pos);
    if (pos >= text.size() || text[pos] != ':') {
      return false;
    }
    pos++;
    Dap::Value value;
    if (!parse_json_value(text, pos, value)) {
      return false;
    }
    out.object_value[key] = std::move(value);
    skip_ws(text, pos);
    if (pos >= text.size()) {
      return false;
    }
    if (text[pos] == '}') {
      pos++;
      return true;
    }
    if (text[pos] != ',') {
      return false;
    }
    pos++;
    skip_ws(text, pos);
  }
  return false;
}

bool parse_json_number(const std::string &text, size_t &pos, Dap::Value &out) {
  size_t start = pos;
  if (pos < text.size() && text[pos] == '-') {
    pos++;
  }
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos]))) {
    pos++;
  }
  if (start == pos || (start + 1 == pos && text[start] == '-')) {
    return false;
  }
  out = Dap::Value{};
  out.type = Dap::Value::Number;
  out.number_value = std::strtoll(text.substr(start, pos - start).c_str(),
                                  nullptr, 10);
  while (pos < text.size() &&
         (std::isdigit((unsigned char)text[pos]) || text[pos] == '.' ||
          text[pos] == 'e' || text[pos] == 'E' || text[pos] == '+' ||
          text[pos] == '-')) {
    pos++;
  }
  return true;
}

bool parse_json_value(const std::string &text, size_t &pos, Dap::Value &out) {
  skip_ws(text, pos);
  if (pos >= text.size()) {
    return false;
  }
  char c = text[pos];
  if (c == '"') {
    out = Dap::Value{};
    out.type = Dap::Value::String;
    return parse_json_string(text, pos, out.string_value);
  }
  if (c == '{') {
    return parse_json_object(text, pos, out);
  }
  if (c == '[') {
    return parse_json_array(text, pos, out);
  }
  if (c == '-' || std::isdigit((unsigned char)c)) {
    return parse_json_number(text, pos, out);
  }
  if (text.compare(pos, 4, "null") == 0) {
    out = Dap::Value{};
    out.type = Dap::Value::Null;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 4, "true") == 0) {
    out = Dap::Value{};
    out.type = Dap::Value::Bool;
    out.bool_value = true;
    pos += 4;
    return true;
  }
  if (text.compare(pos, 5, "false") == 0) {
    out = Dap::Value{};
    out.type = Dap::Value::Bool;
    out.bool_value = false;
    pos += 5;
    return true;
  }
  return false;
}

std::string json_array_strings(const std::vector<std::string> &items) {
  std::string out = "[";
  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0) {
      out += ",";
    }
    out += "\"" + Dap::json_escape(items[i]) + "\"";
  }
  out += "]";
  return out;
}

std::string json_object_strings(const std::map<std::string, std::string> &items) {
  std::string out = "{";
  size_t i = 0;
  for (const auto &entry : items) {
    if (i++ > 0) {
      out += ",";
    }
    out += "\"" + Dap::json_escape(entry.first) + "\":\"" +
           Dap::json_escape(entry.second) + "\"";
  }
  out += "}";
  return out;
}

std::string adapter_type(const std::string &adapter) {
  std::string lower = adapter;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return lower == "lldb" || lower == "lldb-dap" ? "lldb" : "gdb";
}

std::string source_path_from(const Dap::Value *source) {
  if (!source || source->type != Dap::Value::Object) {
    return "";
  }
  return Dap::string_or_empty(Dap::object_get(*source, "path"));
}

DebuggerFrame frame_from_json(const Dap::Value &value) {
  DebuggerFrame frame;
  if (value.type != Dap::Value::Object) {
    return frame;
  }
  frame.id = Dap::int_or_default(Dap::object_get(value, "id"), 0);
  frame.name = Dap::string_or_empty(Dap::object_get(value, "name"));
  frame.line = std::max(0, Dap::int_or_default(Dap::object_get(value, "line"), 1) - 1);
  frame.column =
      std::max(0, Dap::int_or_default(Dap::object_get(value, "column"), 1) - 1);
  frame.filepath = source_path_from(Dap::object_get(value, "source"));
  return frame;
}

std::vector<DebuggerFrame> frames_from_body(const Dap::Value &body) {
  std::vector<DebuggerFrame> frames;
  const Dap::Value *stack = Dap::object_get(body, "stackFrames");
  if (!stack || stack->type != Dap::Value::Array) {
    return frames;
  }
  for (const auto &item : stack->array_value) {
    frames.push_back(frame_from_json(item));
  }
  return frames;
}

std::vector<DebuggerThread> threads_from_body(const Dap::Value &body) {
  std::vector<DebuggerThread> threads;
  const Dap::Value *items = Dap::object_get(body, "threads");
  if (!items || items->type != Dap::Value::Array) {
    return threads;
  }
  for (const auto &item : items->array_value) {
    if (item.type != Dap::Value::Object) {
      continue;
    }
    DebuggerThread thread;
    thread.id = Dap::int_or_default(Dap::object_get(item, "id"), 0);
    thread.name = Dap::string_or_empty(Dap::object_get(item, "name"));
    threads.push_back(std::move(thread));
  }
  return threads;
}

std::vector<DebuggerVariable> variables_from_body(const Dap::Value &body) {
  std::vector<DebuggerVariable> variables;
  const Dap::Value *items = Dap::object_get(body, "variables");
  if (!items || items->type != Dap::Value::Array) {
    return variables;
  }
  for (const auto &item : items->array_value) {
    if (item.type != Dap::Value::Object) {
      continue;
    }
    DebuggerVariable var;
    var.name = Dap::string_or_empty(Dap::object_get(item, "name"));
    var.value = Dap::string_or_empty(Dap::object_get(item, "value"));
    var.type = Dap::string_or_empty(Dap::object_get(item, "type"));
    var.variables_reference =
        Dap::int_or_default(Dap::object_get(item, "variablesReference"), 0);
    variables.push_back(std::move(var));
  }
  return variables;
}

std::vector<DebuggerInstruction> instructions_from_body(const Dap::Value &body) {
  std::vector<DebuggerInstruction> instructions;
  const Dap::Value *items = Dap::object_get(body, "instructions");
  if (!items || items->type != Dap::Value::Array) {
    return instructions;
  }
  for (const auto &item : items->array_value) {
    if (item.type != Dap::Value::Object) {
      continue;
    }
    DebuggerInstruction inst;
    inst.address = Dap::string_or_empty(Dap::object_get(item, "address"));
    inst.instruction = Dap::string_or_empty(Dap::object_get(item, "instruction"));
    instructions.push_back(std::move(inst));
  }
  return instructions;
}

std::vector<DebuggerMemoryRow> memory_from_body(const Dap::Value &body) {
  std::vector<DebuggerMemoryRow> rows;
  std::string address = Dap::string_or_empty(Dap::object_get(body, "address"));
  std::string data = Dap::string_or_empty(Dap::object_get(body, "data"));
  if (data.empty()) {
    return rows;
  }
  int addr = 0;
  try {
    addr = address.empty() ? 0 : std::stoi(address, nullptr, 0);
  } catch (...) {
    addr = 0;
  }
  for (size_t i = 0; i < data.size(); i += 16) {
    std::string chunk = data.substr(i, 16);
    DebuggerMemoryRow row;
    std::ostringstream addr_stream;
    addr_stream << "0x" << std::hex << std::setw(8) << std::setfill('0')
                << (addr + (int)i);
    row.address = addr_stream.str();
    for (unsigned char c : chunk) {
      std::ostringstream byte;
      byte << std::hex << std::setw(2) << std::setfill('0') << (int)c;
      if (!row.bytes.empty()) {
        row.bytes.push_back(' ');
      }
      row.bytes += byte.str();
      row.ascii.push_back(std::isprint(c) ? (char)c : '.');
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::vector<DebuggerBreakpoint> breakpoints_from_body(const Dap::Value &body,
                                                      const std::string &path) {
  std::vector<DebuggerBreakpoint> out;
  const Dap::Value *items = Dap::object_get(body, "breakpoints");
  if (!items || items->type != Dap::Value::Array) {
    return out;
  }
  for (const auto &item : items->array_value) {
    if (item.type != Dap::Value::Object) {
      continue;
    }
    DebuggerBreakpoint bp;
    bp.filepath = path;
    bp.line = std::max(0, Dap::int_or_default(Dap::object_get(item, "line"), 1) - 1);
    bp.verified = Dap::bool_or_default(Dap::object_get(item, "verified"), false);
    out.push_back(std::move(bp));
  }
  return out;
}
} // namespace

namespace Dap {
bool parse_json(const std::string &text, Value &out) {
  size_t pos = 0;
  if (!parse_json_value(text, pos, out)) {
    return false;
  }
  skip_ws(text, pos);
  return pos == text.size();
}

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (unsigned char c : value) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back((char)c);
      break;
    }
  }
  return out;
}

const Value *object_get(const Value &value, const std::string &key) {
  if (value.type != Value::Object) {
    return nullptr;
  }
  auto it = value.object_value.find(key);
  return it == value.object_value.end() ? nullptr : &it->second;
}

std::string string_or_empty(const Value *value) {
  return value && value->type == Value::String ? value->string_value : "";
}

int int_or_default(const Value *value, int fallback) {
  return value && value->type == Value::Number ? (int)value->number_value
                                               : fallback;
}

bool bool_or_default(const Value *value, bool fallback) {
  return value && value->type == Value::Bool ? value->bool_value : fallback;
}

bool extract_content_length(const std::string &headers, size_t &length_out) {
  std::istringstream stream(headers);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const std::string prefix = "Content-Length:";
    if (line.rfind(prefix, 0) != 0) {
      continue;
    }
    std::string number = line.substr(prefix.size());
    size_t pos = 0;
    while (pos < number.size() &&
           std::isspace(static_cast<unsigned char>(number[pos]))) {
      pos++;
    }
    if (pos >= number.size()) {
      return false;
    }
    length_out = (size_t)std::strtoull(number.c_str() + pos, nullptr, 10);
    return true;
  }
  return false;
}
} // namespace Dap

DebuggerClient::DebuggerClient(const DebuggerSessionConfig &session_config,
                               const std::vector<std::string> &adapter_argv)
    : config(session_config), command(adapter_argv) {}

DebuggerClient::~DebuggerClient() { stop(); }

bool DebuggerClient::start() {
  if (running) {
    return true;
  }
  if (command.empty()) {
    last_error = "empty debugger adapter command";
    return false;
  }

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    last_error = strerror(errno);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    last_error = strerror(errno);
    return false;
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    if (!config.cwd.empty()) {
      if (chdir(config.cwd.c_str()) != 0) {
        _exit(127);
      }
    }
    for (const auto &entry : config.env) {
      setenv(entry.first.c_str(), entry.second.c_str(), 1);
    }

    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (const auto &arg : command) {
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

void DebuggerClient::stop() {
  if (!running && stdin_fd < 0 && stdout_fd < 0 && stderr_fd < 0 &&
      child_pid <= 0) {
    return;
  }
  running = false;
  initialized = false;
  launched = false;
  if (child_pid > 0) {
    kill(child_pid, SIGTERM);
    waitpid(child_pid, nullptr, WNOHANG);
  }
  if (stdin_fd >= 0) {
    close(stdin_fd);
  }
  if (stdout_fd >= 0) {
    close(stdout_fd);
  }
  if (stderr_fd >= 0) {
    close(stderr_fd);
  }
  stdin_fd = stdout_fd = stderr_fd = -1;
  child_pid = -1;
  outbound_buffer.clear();
  pending_requests.clear();
}

bool DebuggerClient::poll() {
  if (!running) {
    return false;
  }
  bool changed = false;
  if (!outbound_buffer.empty()) {
    changed = true;
    flush_pending_writes();
  }
  char buf[4096];
  while (stdout_fd >= 0) {
    ssize_t n = read(stdout_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stdout_data(std::string(buf, buf + n));
    changed = true;
  }
  while (stderr_fd >= 0) {
    ssize_t n = read(stderr_fd, buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    handle_stderr_data(std::string(buf, buf + n));
    changed = true;
  }
  int status = 0;
  if (child_pid > 0) {
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid) {
      running = false;
      initialized = false;
      launched = false;
      last_error = WIFEXITED(status)
                       ? "adapter exited with status " +
                             std::to_string(WEXITSTATUS(status))
                       : "adapter exited unexpectedly";
      push_error(last_error);
      changed = true;
    }
  }
  return changed;
}

bool DebuggerClient::initialize() {
  std::string args =
      "{\"adapterID\":\"jot\",\"clientID\":\"jot\",\"clientName\":\"jot\","
      "\"locale\":\"en-us\",\"pathFormat\":\"path\",\"linesStartAt1\":true,"
      "\"columnsStartAt1\":true,\"supportsVariableType\":true,"
      "\"supportsVariablePaging\":false,\"supportsRunInTerminalRequest\":false,"
      "\"supportsMemoryReferences\":true}";
  return send_request("initialize", args);
}

bool DebuggerClient::launch_or_attach() {
  if (config.attach) {
    std::ostringstream args;
    args << "{\"pid\":" << config.pid;
    if (!config.cwd.empty()) {
      args << ",\"cwd\":\"" << Dap::json_escape(config.cwd) << "\"";
    }
    args << "}";
    launched = send_request("attach", args.str());
    return launched;
  }

  std::ostringstream args;
  args << "{\"program\":\"" << Dap::json_escape(config.program) << "\""
       << ",\"args\":" << json_array_strings(config.args)
       << ",\"cwd\":\"" << Dap::json_escape(config.cwd.empty() ? "." : config.cwd)
       << "\""
       << ",\"env\":" << json_object_strings(config.env)
       << ",\"stopAtBeginningOfMainSubprogram\":false"
       << ",\"stopOnEntry\":false}";
  launched = send_request("launch", args.str());
  return launched;
}

bool DebuggerClient::disconnect(bool terminate_debuggee) {
  return send_request("disconnect",
                      std::string("{\"terminateDebuggee\":") +
                          (terminate_debuggee ? "true" : "false") + "}");
}

bool DebuggerClient::configuration_done() {
  return send_request("configurationDone");
}

bool DebuggerClient::continue_thread(int thread_id) {
  return send_request("continue",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          "}");
}

bool DebuggerClient::pause_thread(int thread_id) {
  return send_request("pause",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          "}");
}

bool DebuggerClient::next(int thread_id) {
  return send_request("next",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          "}");
}

bool DebuggerClient::step_in(int thread_id) {
  return send_request("stepIn",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          "}");
}

bool DebuggerClient::step_out(int thread_id) {
  return send_request("stepOut",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          "}");
}

bool DebuggerClient::threads() { return send_request("threads"); }

bool DebuggerClient::stack_trace(int thread_id) {
  return send_request("stackTrace",
                      "{\"threadId\":" + std::to_string(std::max(0, thread_id)) +
                          ",\"startFrame\":0,\"levels\":20}");
}

bool DebuggerClient::scopes(int frame_id) {
  return send_request("scopes",
                      "{\"frameId\":" + std::to_string(std::max(0, frame_id)) +
                          "}");
}

bool DebuggerClient::variables(int variables_reference) {
  return send_request("variables",
                      "{\"variablesReference\":" +
                          std::to_string(std::max(0, variables_reference)) +
                          "}");
}

bool DebuggerClient::read_memory(const std::string &memory_reference, int offset,
                                 int count) {
  return send_request("readMemory",
                      "{\"memoryReference\":\"" +
                          Dap::json_escape(memory_reference) +
                          "\",\"offset\":" + std::to_string(offset) +
                          ",\"count\":" + std::to_string(std::max(1, count)) +
                          "}");
}

bool DebuggerClient::disassemble(const std::string &memory_reference, int offset,
                                 int instruction_offset, int count) {
  return send_request("disassemble",
                      "{\"memoryReference\":\"" +
                          Dap::json_escape(memory_reference) +
                          "\",\"offset\":" + std::to_string(offset) +
                          ",\"instructionOffset\":" +
                          std::to_string(instruction_offset) +
                          ",\"instructionCount\":" +
                          std::to_string(std::max(1, count)) + "}");
}

bool DebuggerClient::set_breakpoints(
    const std::string &source_path, const std::vector<int> &zero_based_lines) {
  std::ostringstream args;
  args << "{\"source\":{\"path\":\"" << Dap::json_escape(source_path)
       << "\"},\"breakpoints\":[";
  for (size_t i = 0; i < zero_based_lines.size(); i++) {
    if (i > 0) {
      args << ",";
    }
    args << "{\"line\":" << (zero_based_lines[i] + 1) << "}";
  }
  args << "]}";
  return send_request("setBreakpoints", args.str());
}

std::vector<DebuggerEvent> DebuggerClient::consume_events() {
  auto out = std::move(pending_events);
  pending_events.clear();
  return out;
}

bool DebuggerClient::send_request(const std::string &command_name,
                                  const std::string &arguments_json) {
  int request_id = next_request_id++;
  pending_requests[request_id] = command_name;
  std::ostringstream json;
  json << "{\"seq\":" << request_id
       << ",\"type\":\"request\",\"command\":\"" << command_name
       << "\",\"arguments\":" << (arguments_json.empty() ? "{}" : arguments_json)
       << "}";
  if (!send_message(json.str())) {
    pending_requests.erase(request_id);
    return false;
  }
  return true;
}

bool DebuggerClient::send_message(const std::string &json) {
  if (!running || stdin_fd < 0) {
    return false;
  }
  outbound_buffer +=
      "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
  append_log(config.name, "SEND ", json);
  return flush_pending_writes();
}

bool DebuggerClient::flush_pending_writes() {
  while (!outbound_buffer.empty()) {
    ssize_t written =
        write(stdin_fd, outbound_buffer.data(), outbound_buffer.size());
    if (written > 0) {
      outbound_buffer.erase(0, (size_t)written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return true;
    }
    last_error = written < 0 ? strerror(errno) : "debugger stdin closed";
    push_error(last_error);
    running = false;
    return false;
  }
  return true;
}

void DebuggerClient::handle_stdout_data(const std::string &data) {
  stdout_buffer += data;
  append_log(config.name, "RECV ", data);
  while (true) {
    size_t header_end = stdout_buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
      return;
    }
    size_t content_length = 0;
    if (!Dap::extract_content_length(stdout_buffer.substr(0, header_end),
                                     content_length)) {
      stdout_buffer.erase(0, header_end + 4);
      push_error("DAP parse error: missing Content-Length");
      continue;
    }
    size_t body_start = header_end + 4;
    if (stdout_buffer.size() < body_start + content_length) {
      return;
    }
    std::string message = stdout_buffer.substr(body_start, content_length);
    stdout_buffer.erase(0, body_start + content_length);
    handle_message(message);
  }
}

void DebuggerClient::handle_stderr_data(const std::string &data) {
  stderr_buffer += data;
  append_log(config.name, "STDERR ", data);
  DebuggerEvent ev;
  ev.type = DebuggerEvent::Output;
  ev.message = data;
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::handle_message(const std::string &message) {
  Dap::Value root;
  if (!Dap::parse_json(message, root)) {
    push_error("DAP parse error: invalid JSON");
    return;
  }
  std::string type = Dap::string_or_empty(Dap::object_get(root, "type"));
  if (type == "response") {
    handle_response(root);
  } else if (type == "event") {
    handle_event(root);
  }
}

void DebuggerClient::handle_response(const Dap::Value &root) {
  int request_id = Dap::int_or_default(Dap::object_get(root, "request_seq"), 0);
  std::string command_name = Dap::string_or_empty(Dap::object_get(root, "command"));
  auto it = pending_requests.find(request_id);
  if (it != pending_requests.end()) {
    command_name = it->second;
    pending_requests.erase(it);
  }
  bool success = Dap::bool_or_default(Dap::object_get(root, "success"), true);
  const Dap::Value *body = Dap::object_get(root, "body");
  if (!success) {
    push_error(Dap::string_or_empty(Dap::object_get(root, "message")));
    return;
  }
  if (command_name == "initialize" && body && body->type == Dap::Value::Object) {
    supports_read_memory_ =
        Dap::bool_or_default(Dap::object_get(*body, "supportsReadMemoryRequest"),
                             false);
    supports_disassemble_ =
        Dap::bool_or_default(Dap::object_get(*body, "supportsDisassembleRequest"),
                             false);
    initialized = true;
    DebuggerEvent ev;
    ev.type = DebuggerEvent::Capabilities;
    ev.supports_read_memory = supports_read_memory_;
    ev.supports_disassemble = supports_disassemble_;
    pending_events.push_back(std::move(ev));
    return;
  }
  if (!body || body->type != Dap::Value::Object) {
    return;
  }
  DebuggerEvent ev;
  if (command_name == "threads") {
    ev.type = DebuggerEvent::Threads;
    ev.threads = threads_from_body(*body);
  } else if (command_name == "stackTrace") {
    ev.type = DebuggerEvent::StackTrace;
    ev.frames = frames_from_body(*body);
  } else if (command_name == "scopes" || command_name == "variables") {
    ev.type = command_name == "scopes" ? DebuggerEvent::Scopes
                                       : DebuggerEvent::Variables;
    ev.variables = variables_from_body(*body);
  } else if (command_name == "readMemory") {
    ev.type = DebuggerEvent::Memory;
    ev.memory_rows = memory_from_body(*body);
  } else if (command_name == "disassemble") {
    ev.type = DebuggerEvent::Disassembly;
    ev.instructions = instructions_from_body(*body);
  } else if (command_name == "setBreakpoints") {
    ev.type = DebuggerEvent::Breakpoints;
    std::string path;
    ev.breakpoints = breakpoints_from_body(*body, path);
  } else {
    return;
  }
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::handle_event(const Dap::Value &root) {
  std::string name = Dap::string_or_empty(Dap::object_get(root, "event"));
  const Dap::Value *body = Dap::object_get(root, "body");
  DebuggerEvent ev;
  if (name == "initialized") {
    ev.type = DebuggerEvent::Initialized;
  } else if (name == "stopped") {
    ev.type = DebuggerEvent::Stopped;
    ev.message = body ? Dap::string_or_empty(Dap::object_get(*body, "reason")) : "";
    ev.thread_id = body ? Dap::int_or_default(Dap::object_get(*body, "threadId"), 0)
                        : 0;
  } else if (name == "continued") {
    ev.type = DebuggerEvent::Continued;
    ev.thread_id = body ? Dap::int_or_default(Dap::object_get(*body, "threadId"), 0)
                        : 0;
  } else if (name == "terminated") {
    ev.type = DebuggerEvent::Terminated;
  } else if (name == "exited") {
    ev.type = DebuggerEvent::Exited;
    ev.exit_code = body ? Dap::int_or_default(Dap::object_get(*body, "exitCode"), 0)
                        : 0;
  } else if (name == "output") {
    ev.type = DebuggerEvent::Output;
    ev.message = body ? Dap::string_or_empty(Dap::object_get(*body, "output")) : "";
  } else {
    return;
  }
  pending_events.push_back(std::move(ev));
}

void DebuggerClient::push_error(const std::string &message) {
  DebuggerEvent ev;
  ev.type = DebuggerEvent::Error;
  ev.message = message.empty() ? "Debugger error" : message;
  pending_events.push_back(std::move(ev));
}

std::string DebuggerClient::describe() const {
  return config.name + " (" + adapter_type(config.adapter) + ")";
}

std::vector<DebuggerSessionConfig>
parse_debugger_config_text(const std::string &text) {
  Dap::Value root;
  if (!Dap::parse_json(text, root) || root.type != Dap::Value::Object) {
    return {};
  }
  const Dap::Value *sessions = Dap::object_get(root, "sessions");
  if (!sessions || sessions->type != Dap::Value::Object) {
    return {};
  }
  std::vector<DebuggerSessionConfig> out;
  for (const auto &entry : sessions->object_value) {
    if (entry.second.type != Dap::Value::Object) {
      continue;
    }
    DebuggerSessionConfig config;
    config.name = entry.first;
    config.adapter =
        Dap::string_or_empty(Dap::object_get(entry.second, "adapter"));
    if (config.adapter.empty()) {
      config.adapter = "gdb";
    }
    config.program =
        Dap::string_or_empty(Dap::object_get(entry.second, "program"));
    config.cwd = Dap::string_or_empty(Dap::object_get(entry.second, "cwd"));
    config.attach =
        Dap::bool_or_default(Dap::object_get(entry.second, "attach"), false);
    config.pid = Dap::int_or_default(Dap::object_get(entry.second, "pid"), 0);
    const Dap::Value *args = Dap::object_get(entry.second, "args");
    if (args && args->type == Dap::Value::Array) {
      for (const auto &arg : args->array_value) {
        if (arg.type == Dap::Value::String) {
          config.args.push_back(arg.string_value);
        }
      }
    }
    const Dap::Value *env = Dap::object_get(entry.second, "env");
    if (env && env->type == Dap::Value::Object) {
      for (const auto &env_entry : env->object_value) {
        if (env_entry.second.type == Dap::Value::String) {
          config.env[env_entry.first] = env_entry.second.string_value;
        }
      }
    }
    out.push_back(std::move(config));
  }
  std::sort(out.begin(), out.end(),
            [](const DebuggerSessionConfig &a,
               const DebuggerSessionConfig &b) { return a.name < b.name; });
  return out;
}
