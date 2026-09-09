// DAP response decoding: convert raw Dap::Value trees into the editor-facing
// DebuggerFrame / DebuggerThread / DebuggerVariable / memory / breakpoint
// models, plus small JSON string builders.
#include "tools/debugger/client.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string.h>

namespace dbg_internal
{
std::string json_array_strings(const std::vector<std::string> &items)
{
  std::string out = "[";
  for (size_t i = 0; i < items.size(); i++)
  {
    if (i > 0)
    {
      out += ",";
    }
    out += "\"" + Dap::json_escape(items[i]) + "\"";
  }
  out += "]";
  return out;
}
std::string json_object_strings(const std::map<std::string, std::string> &items)
{
  std::string out = "{";
  size_t i = 0;
  for (const auto &entry : items)
  {
    if (i++ > 0)
    {
      out += ",";
    }
    out += "\"" + Dap::json_escape(entry.first) + "\":\"" + Dap::json_escape(entry.second) + "\"";
  }
  out += "}";
  return out;
}
std::string adapter_type(const std::string &adapter)
{
  std::string lower = adapter;
  std::transform(lower.begin(),
                 lower.end(),
                 lower.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return lower == "lldb" || lower == "lldb-dap" ? "lldb" : "gdb";
}
std::string source_path_from(const Dap::Value *source)
{
  if (!source || source->type != Dap::Value::Object)
  {
    return "";
  }
  return Dap::string_or_empty(Dap::object_get(*source, "path"));
}
DebuggerFrame frame_from_json(const Dap::Value &value)
{
  DebuggerFrame frame;
  if (value.type != Dap::Value::Object)
  {
    return frame;
  }
  frame.id = Dap::int_or_default(Dap::object_get(value, "id"), 0);
  frame.name = Dap::string_or_empty(Dap::object_get(value, "name"));
  frame.line = std::max(0, Dap::int_or_default(Dap::object_get(value, "line"), 1) - 1);
  frame.column = std::max(0, Dap::int_or_default(Dap::object_get(value, "column"), 1) - 1);
  frame.filepath = source_path_from(Dap::object_get(value, "source"));
  return frame;
}
std::vector<DebuggerFrame> frames_from_body(const Dap::Value &body)
{
  std::vector<DebuggerFrame> frames;
  const Dap::Value *stack = Dap::object_get(body, "stackFrames");
  if (!stack || stack->type != Dap::Value::Array)
  {
    return frames;
  }
  for (const auto &item : stack->array_value)
  {
    frames.push_back(frame_from_json(item));
  }
  return frames;
}
std::vector<DebuggerThread> threads_from_body(const Dap::Value &body)
{
  std::vector<DebuggerThread> threads;
  const Dap::Value *items = Dap::object_get(body, "threads");
  if (!items || items->type != Dap::Value::Array)
  {
    return threads;
  }
  for (const auto &item : items->array_value)
  {
    if (item.type != Dap::Value::Object)
    {
      continue;
    }
    DebuggerThread thread;
    thread.id = Dap::int_or_default(Dap::object_get(item, "id"), 0);
    thread.name = Dap::string_or_empty(Dap::object_get(item, "name"));
    threads.push_back(std::move(thread));
  }
  return threads;
}
std::vector<DebuggerVariable> variables_from_body(const Dap::Value &body)
{
  std::vector<DebuggerVariable> variables;
  const Dap::Value *items = Dap::object_get(body, "variables");
  if (!items || items->type != Dap::Value::Array)
  {
    return variables;
  }
  for (const auto &item : items->array_value)
  {
    if (item.type != Dap::Value::Object)
    {
      continue;
    }
    DebuggerVariable var;
    var.name = Dap::string_or_empty(Dap::object_get(item, "name"));
    var.value = Dap::string_or_empty(Dap::object_get(item, "value"));
    var.type = Dap::string_or_empty(Dap::object_get(item, "type"));
    var.variables_reference = Dap::int_or_default(Dap::object_get(item, "variablesReference"), 0);
    variables.push_back(std::move(var));
  }
  return variables;
}
std::vector<DebuggerInstruction> instructions_from_body(const Dap::Value &body)
{
  std::vector<DebuggerInstruction> instructions;
  const Dap::Value *items = Dap::object_get(body, "instructions");
  if (!items || items->type != Dap::Value::Array)
  {
    return instructions;
  }
  for (const auto &item : items->array_value)
  {
    if (item.type != Dap::Value::Object)
    {
      continue;
    }
    DebuggerInstruction inst;
    inst.address = Dap::string_or_empty(Dap::object_get(item, "address"));
    inst.instruction = Dap::string_or_empty(Dap::object_get(item, "instruction"));
    instructions.push_back(std::move(inst));
  }
  return instructions;
}
std::vector<DebuggerMemoryRow> format_memory_rows(unsigned long long addr,
                                                  const std::string &data)
{
  std::vector<DebuggerMemoryRow> rows;
  for (size_t i = 0; i < data.size(); i += 16)
  {
    std::string chunk = data.substr(i, 16);
    DebuggerMemoryRow row;
    std::ostringstream addr_stream;
    addr_stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << (addr + i);
    row.address = addr_stream.str();
    for (unsigned char c : chunk)
    {
      std::ostringstream byte;
      byte << std::hex << std::setw(2) << std::setfill('0') << (int)c;
      if (!row.bytes.empty())
      {
        row.bytes.push_back(' ');
      }
      row.bytes += byte.str();
      row.ascii.push_back(std::isprint(c) ? (char)c : '.');
    }
    rows.push_back(std::move(row));
  }
  return rows;
}
std::vector<DebuggerMemoryRow> memory_from_body(const Dap::Value &body)
{
  std::vector<DebuggerMemoryRow> rows;
  const std::string address = Dap::string_or_empty(Dap::object_get(body, "address"));
  const std::string data = Dap::string_or_empty(Dap::object_get(body, "data"));
  if (data.empty())
  {
    return rows;
  }
  std::string bytes;
  if (!Dap::decode_base64(data, bytes))
  {
    return rows;
  }
  unsigned long long addr = 0;
  try
  {
    addr = address.empty() ? 0 : std::strtoull(address.c_str(), nullptr, 0);
  }
  catch (...)
  {
    addr = 0;
  }
  return format_memory_rows(addr, bytes);
}
std::vector<DebuggerBreakpoint> breakpoints_from_body(const Dap::Value &body,
                                                      const std::string &path)
{
  std::vector<DebuggerBreakpoint> out;
  const Dap::Value *items = Dap::object_get(body, "breakpoints");
  if (!items || items->type != Dap::Value::Array)
  {
    return out;
  }
  for (const auto &item : items->array_value)
  {
    if (item.type != Dap::Value::Object)
    {
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
} // namespace dbg_internal
