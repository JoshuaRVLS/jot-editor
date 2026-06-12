#include "debugger_client.h"
#include "test_framework.h"

TEST(TestDebuggerJsonParse) {
  Dap::Value root;
  ASSERT_TRUE(Dap::parse_json("{\"a\":1,\"b\":\"x\",\"c\":true}", root));
  ASSERT_EQ(root.type, Dap::Value::Object);
  ASSERT_EQ(Dap::int_or_default(Dap::object_get(root, "a"), 0), 1);
  ASSERT_EQ(Dap::string_or_empty(Dap::object_get(root, "b")), "x");
  ASSERT_TRUE(Dap::bool_or_default(Dap::object_get(root, "c"), false));
}

TEST(TestDebuggerContentLength) {
  size_t len = 0;
  ASSERT_TRUE(Dap::extract_content_length("Content-Length: 42\r\n", len));
  ASSERT_EQ(len, (size_t)42);
}

TEST(TestDebuggerConfigParse) {
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
  ASSERT_EQ(configs.size(), (size_t)1);
  ASSERT_EQ(configs[0].name, "app");
  ASSERT_EQ(configs[0].adapter, "lldb");
  ASSERT_EQ(configs[0].program, "./app");
  ASSERT_EQ(configs[0].args.size(), (size_t)2);
  ASSERT_EQ(configs[0].args[0], "--one");
  ASSERT_EQ(configs[0].env["A"], "B");
}
