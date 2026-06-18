#include "tools/debugger/client.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Debugger JSON Parse", "[jot]") {
  Dap::Value root;
  REQUIRE(Dap::parse_json("{\"a\":1,\"b\":\"x\",\"c\":true}", root));
  REQUIRE(root.type == Dap::Value::Object);
  REQUIRE(Dap::int_or_default(Dap::object_get(root, "a"), 0) == 1);
  REQUIRE(Dap::string_or_empty(Dap::object_get(root, "b")) == "x");
  REQUIRE(Dap::bool_or_default(Dap::object_get(root, "c"), false));
}

TEST_CASE("Debugger Content Length", "[jot]") {
  size_t len = 0;
  REQUIRE(Dap::extract_content_length("Content-Length: 42\r\n", len));
  REQUIRE(len == (size_t)42);
}

TEST_CASE("Debugger Config Parse", "[jot]") {
  const std::string text =
      "{"
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
