#ifndef DEBUGGER_CLIENT_H
#define DEBUGGER_CLIENT_H

#include <map>
#include <string>
#include <vector>

namespace Dap
{

  struct Value
  {
    enum Type
    {
      Null,
      Bool,
      Number,
      String,
      Array,
      Object
    } type = Null;
    bool bool_value = false;
    long long number_value = 0;
    std::string string_value;
    std::vector<Value> array_value;
    std::map<std::string, Value> object_value;
  };

  bool parse_json(const std::string &text, Value &out);
  std::string json_escape(const std::string &value);
  const Value *object_get(const Value &value, const std::string &key);
  std::string string_or_empty(const Value *value);
  int int_or_default(const Value *value, int fallback);
  bool bool_or_default(const Value *value, bool fallback);
  bool extract_content_length(const std::string &headers, size_t &length_out);
  // Decodes standard base64 (the encoding DAP uses for readMemory body.data
  // and other binary payloads) into raw bytes. Returns false on padding or
  // alphabet errors; partial output is discarded.
  bool decode_base64(const std::string &text, std::string &out);
  // True when `text` is a literal address ("0x404028" or decimal), i.e.
  // something readMemory accepts without evaluation.
  bool looks_like_address(const std::string &text);

} // namespace Dap

struct DebuggerBreakpoint
{
  std::string filepath;
  int line = 0; // zero-based
  bool verified = false;
};

struct DebuggerFrame
{
  int id = 0;
  std::string name;
  std::string filepath;
  int line = 0; // zero-based
  int column = 0;
};

struct DebuggerThread
{
  int id = 0;
  std::string name;
  std::vector<DebuggerFrame> frames;
};

struct DebuggerVariable
{
  std::string name;
  std::string value;
  std::string type;
  int variables_reference = 0;
};

struct DebuggerMemoryRow
{
  std::string address;
  std::string bytes;
  std::string ascii;
};

struct DebuggerInstruction
{
  std::string address;
  std::string instruction;
};

struct DebuggerSessionConfig
{
  std::string name;
  std::string adapter = "gdb";
  std::string program;
  std::vector<std::string> args;
  std::string cwd;
  std::map<std::string, std::string> env;
  bool attach = false;
  int pid = 0;
};

struct DebuggerEvent
{
  enum Type
  {
    None,
    Initialized,
    Stopped,
    Continued,
    Terminated,
    Exited,
    Output,
    Capabilities,
    Threads,
    StackTrace,
    Scopes,
    Variables,
    Memory,
    Disassembly,
    Breakpoints,
    Error
  } type = None;

  std::string message;
  int thread_id = 0;
  int exit_code = 0;
  std::vector<DebuggerThread> threads;
  std::vector<DebuggerFrame> frames;
  std::vector<DebuggerVariable> variables;
  std::vector<DebuggerMemoryRow> memory_rows;
  std::vector<DebuggerInstruction> instructions;
  std::vector<DebuggerBreakpoint> breakpoints;
  bool supports_read_memory = false;
  bool supports_disassemble = false;
};

class DebuggerClient
{
public:
  DebuggerClient(const DebuggerSessionConfig &config, const std::vector<std::string> &adapter_argv);
  ~DebuggerClient();

  bool start();
  void stop();
  bool poll();

  bool initialize();
  bool launch_or_attach();
  bool disconnect(bool terminate_debuggee);
  bool configuration_done();
  bool continue_thread(int thread_id);
  bool pause_thread(int thread_id);
  bool next(int thread_id);
  bool step_in(int thread_id);
  bool step_out(int thread_id);
  bool threads();
  bool stack_trace(int thread_id);
  bool scopes(int frame_id);
  bool variables(int variables_reference);
  bool read_memory(const std::string &memory_reference, int offset, int count);
  // Memory-view convenience: evaluates `expression` and, when the adapter
  // reports an address for it (memoryReference field, or a literal address
  // as the evaluated result), immediately issues readMemory for `count`
  // bytes. GDB only accepts literal addresses in readMemory, so this is the
  // path for everything that is not already an address (":debugmemory $pc",
  // ":debugmemory &buf", ...). Errors surface as DebuggerEvent::Error.
  bool evaluate_and_read_memory(const std::string &expression, int count);
  bool evaluate(const std::string &expression);
  bool
  disassemble(const std::string &memory_reference, int offset, int instruction_offset, int count);
  bool set_breakpoints(const std::string &source_path, const std::vector<int> &zero_based_lines);

  std::vector<DebuggerEvent> consume_events();

  bool is_running() const
  {
    return running;
  }
  bool is_initialized() const
  {
    return initialized;
  }
  bool is_launched() const
  {
    return launched;
  }
  int get_stdin_fd() const
  {
    return stdin_fd;
  }
  int get_stdout_fd() const
  {
    return stdout_fd;
  }
  int get_stderr_fd() const
  {
    return stderr_fd;
  }
  bool supports_read_memory() const
  {
    return supports_read_memory_;
  }
  bool supports_disassemble() const
  {
    return supports_disassemble_;
  }
  const std::string &get_last_error() const
  {
    return last_error;
  }
  const DebuggerSessionConfig &get_config() const
  {
    return config;
  }
  std::string describe() const;

private:
  DebuggerSessionConfig config;
  std::vector<std::string> command;
  int stdin_fd = -1;
  int stdout_fd = -1;
  int stderr_fd = -1;
  int child_pid = -1;
#ifdef _WIN32
  void *child_process_handle = nullptr;
#endif
  bool running = false;
  bool initialized = false;
  bool launched = false;
  int next_request_id = 1;
  std::string stdout_buffer;
  std::string stderr_buffer;
  std::string outbound_buffer;
  std::string last_error;
  std::map<int, std::string> pending_requests;
  std::vector<DebuggerEvent> pending_events;
  // When >= 0, the next evaluate response is followed by a readMemory of
  // this many bytes at the evaluated address (evaluate_and_read_memory).
  int pending_memory_read_count = -1;
  bool supports_read_memory_ = false;
  bool supports_disassemble_ = false;

  bool send_request(const std::string &command_name, const std::string &arguments_json = "{}");
  bool send_message(const std::string &json);
  bool flush_pending_writes();
  void handle_stdout_data(const std::string &data);
  void handle_stderr_data(const std::string &data);
  void handle_message(const std::string &message);
  void handle_response(const Dap::Value &root);
  void handle_event(const Dap::Value &root);
  void push_error(const std::string &message);
};

std::vector<DebuggerSessionConfig> parse_debugger_config_text(const std::string &text);

#endif
