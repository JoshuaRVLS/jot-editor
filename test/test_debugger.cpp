#include "tools/debugger/client.h"
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace
{
#ifndef _WIN32
  bool tool_available(const char *tool)
  {
    return std::system((std::string("command -v ") + tool + " >/dev/null 2>&1").c_str()) == 0;
  }

  // Polls the client until `pred` sees an event or the timeout elapses.
  template <typename Pred> bool wait_for_event(DebuggerClient &client, Pred pred, int timeout_ms)
  {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while (clock::now() < deadline)
    {
      client.poll();
      for (auto &e : client.consume_events())
      {
        if (pred(e))
        {
          return true;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }
#endif
} // namespace

TEST_CASE("Debugger JSON Parse", "[jot]")
{
  Dap::Value root;
  REQUIRE(Dap::parse_json("{\"a\":1,\"b\":\"x\",\"c\":true}", root));
  REQUIRE(root.type == Dap::Value::Object);
  REQUIRE(Dap::int_or_default(Dap::object_get(root, "a"), 0) == 1);
  REQUIRE(Dap::string_or_empty(Dap::object_get(root, "b")) == "x");
  REQUIRE(Dap::bool_or_default(Dap::object_get(root, "c"), false));
}

TEST_CASE("Debugger Content Length", "[jot]")
{
  size_t len = 0;
  REQUIRE(Dap::extract_content_length("Content-Length: 42\r\n", len));
  REQUIRE(len == (size_t)42);
}

TEST_CASE("Debugger Base64 Decode", "[jot]")
{
  std::string out;
  // Empty input decodes to empty output.
  REQUIRE(Dap::decode_base64("", out));
  REQUIRE(out.empty());
  // "hello"
  REQUIRE(Dap::decode_base64("aGVsbG8=", out));
  REQUIRE(out == "hello");
  // 0x00 0x01 0x02 0x03 0x04 (binary-safe round trip)
  REQUIRE(Dap::decode_base64("AAECAwQ=", out));
  REQUIRE(out == std::string("\x00\x01\x02\x03\x04", 5));
  // Whitespace inside the stream is tolerated (DAP adapters sometimes wrap).
  REQUIRE(Dap::decode_base64("aGVs\nbG8=", out));
  REQUIRE(out == "hello");
  // Unpadded final group decodes too.
  REQUIRE(Dap::decode_base64("aGVsbG8", out));
  REQUIRE(out == "hello");
  // Junk characters are rejected.
  REQUIRE(!Dap::decode_base64("aGVs!bG8=", out));
}

#ifndef _WIN32
TEST_CASE("Debugger Live GDB Session", "[jot]")
{
  if (!tool_available("gdb") || !tool_available("gcc"))
  {
    SUCCEED("gdb/gcc not installed; live session test skipped");
    return;
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  const std::string unique =
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  fs::path dir = fs::temp_directory_path(ec) / ("jot_dbg_live_" + unique);
  fs::create_directories(dir, ec);
  const fs::path source = dir / "prog.c";
  const fs::path program = dir / "prog";
  {
    std::ofstream out(source.string());
    out << "int global_var = 0x12345678;\n"   // line 1
        << "int main(void) {\n"               // line 2
        << "    return global_var & 1;\n"     // line 3 (breakpoint, 0-based 2)
        << "}\n";
  }
  const bool compiled =
      std::system(("gcc -g -O0 -o " + program.string() + " " + source.string() + " 2>/dev/null")
                      .c_str())
      == 0;
  REQUIRE(compiled);

  DebuggerSessionConfig config;
  config.name = "live";
  config.adapter = "gdb";
  config.program = program.string();
  config.cwd = dir.string();
  DebuggerClient client(config, {"gdb", "--interpreter=dap"});
  REQUIRE(client.start());

  // initialize -> capabilities
  REQUIRE(wait_for_event(
      client, [](const DebuggerEvent &e) { return e.type == DebuggerEvent::Capabilities; }, 15000));

  // Breakpoint on line 3, then run: the program must stop there.
  client.set_breakpoints(source.string(), {2});
  client.launch_or_attach();
  client.configuration_done();
  REQUIRE(wait_for_event(
      client, [](const DebuggerEvent &e) { return e.type == DebuggerEvent::Stopped; }, 15000));

  // The memory viewer path: evaluate "&global_var" and hexdump 8 bytes.
  // global_var is 0x12345678 -> little-endian bytes 78 56 34 12.
  client.evaluate_and_read_memory("&global_var", 8);
  std::vector<DebuggerMemoryRow> rows;
  const bool got_memory = wait_for_event(
      client,
      [&](const DebuggerEvent &e)
      {
        if (e.type == DebuggerEvent::Memory)
        {
          rows = e.memory_rows;
          return true;
        }
        return false;
      },
      15000);
  REQUIRE(got_memory);
  REQUIRE_FALSE(rows.empty());
  REQUIRE(rows.front().bytes.rfind("78 56 34 12", 0) == 0);
  REQUIRE_FALSE(rows.front().address.empty());
  REQUIRE(rows.front().address.rfind("0x", 0) == 0);

  client.stop();
  fs::remove_all(dir, ec);
}
#endif

TEST_CASE("Debugger Address Literal Detection", "[jot]")
{
  REQUIRE(Dap::looks_like_address("0x404028"));
  REQUIRE(Dap::looks_like_address("0X7FFF1000"));
  REQUIRE(Dap::looks_like_address("404028"));
  REQUIRE(!Dap::looks_like_address(""));
  REQUIRE(!Dap::looks_like_address("0x"));
  REQUIRE(!Dap::looks_like_address("0x12zz"));
  REQUIRE(!Dap::looks_like_address("&global_var"));
  REQUIRE(!Dap::looks_like_address("$pc"));
  REQUIRE(!Dap::looks_like_address("buf"));
}

TEST_CASE("Debugger ReadMemory Response Parse", "[jot]")
{
  // The DAP readMemory body carries base64 data; parsing must surface the
  // raw payload so the memory view can hex-dump real bytes.
  const std::string text =
      "{\"address\":\"0x7fffffffe000\",\"data\":\"aGVsbG8gd29ybGQ=\"}";
  Dap::Value root;
  REQUIRE(Dap::parse_json(text, root));
  REQUIRE(Dap::string_or_empty(Dap::object_get(root, "address")) == "0x7fffffffe000");
  std::string bytes;
  REQUIRE(Dap::decode_base64(Dap::string_or_empty(Dap::object_get(root, "data")), bytes));
  REQUIRE(bytes == "hello world");
}

TEST_CASE("Debugger Config Parse", "[jot]")
{
  const std::string text = "{"
                           "\"sessions\":{"
                           "\"app\":{"
                           "\"adapter\":\"lldb\","
                           "\"program\":\"./app\","
                           "\"args\":[\"--one\",\"two\"],"
                           "\"cwd\":\".\","
                           "\"env\":{\"A\":\"B\"}"
                           "}"
                           "}"
                           "}";
  auto configs = parse_debugger_config_text(text);
  REQUIRE(configs.size() == (size_t)1);
  REQUIRE(configs[0].name == "app");
  REQUIRE(configs[0].adapter == "lldb");
  REQUIRE(configs[0].program == "./app");
  REQUIRE(configs[0].args.size() == (size_t)2);
  REQUIRE(configs[0].args[0] == "--one");
  REQUIRE(configs[0].env["A"] == "B");
}
