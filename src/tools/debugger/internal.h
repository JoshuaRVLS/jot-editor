// Internal helpers shared across the DAP client modules: process spawn
// utilities (defined in process.cpp) and DAP response decoding (defined in
// convert.cpp). Declared here so client.cpp can use them without seeing the
// implementation files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Dap
{
struct Value;
} // namespace Dap

struct DebuggerFrame;
struct DebuggerThread;
struct DebuggerVariable;
struct DebuggerInstruction;
struct DebuggerMemoryRow;
struct DebuggerBreakpoint;

namespace dbg_internal
{
// process.cpp
bool set_non_blocking(int fd);
std::string windows_error_message(unsigned long code);
std::string quote_windows_arg(const std::string &arg);
std::string windows_command_line(const std::vector<std::string> &argv);
bool fd_has_data(int fd);
std::string debug_log_path(const std::string &name);
void append_log(const std::string &name, const std::string &prefix, const std::string &line);

// convert.cpp
std::string json_array_strings(const std::vector<std::string> &items);
std::string json_object_strings(const std::map<std::string, std::string> &items);
std::string adapter_type(const std::string &adapter);
std::string source_path_from(const Dap::Value *source);
DebuggerFrame frame_from_json(const Dap::Value &value);
std::vector<DebuggerFrame> frames_from_body(const Dap::Value &body);
std::vector<DebuggerThread> threads_from_body(const Dap::Value &body);
std::vector<DebuggerVariable> variables_from_body(const Dap::Value &body);
std::vector<DebuggerInstruction> instructions_from_body(const Dap::Value &body);
std::vector<DebuggerMemoryRow> format_memory_rows(unsigned long long addr,
                                                 const std::vector<std::uint8_t> &bytes);
std::vector<DebuggerMemoryRow> memory_from_body(const Dap::Value &body);
std::vector<DebuggerBreakpoint> breakpoints_from_body(const Dap::Value &body,
                                                      const std::string &path);
} // namespace dbg_internal