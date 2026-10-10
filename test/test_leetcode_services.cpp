#include "extras/leetcode_http.h"
#include "extras/leetcode_credentials.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

TEST_CASE("LeetCode HTTP requests validate origin and bounds", "[leetcode][http]")
{
  LeetCodeHttp::Request request;
  request.method = "POST";
  request.url = "https://leetcode.com/graphql";
  request.body = "{}";
  request.headers.push_back({"Content-Type", "application/json"});
  std::string error;
  REQUIRE(LeetCodeHttp::valid_request(request, error));

  request.url = "file:///etc/passwd";
  REQUIRE_FALSE(LeetCodeHttp::valid_request(request, error));
  REQUIRE(error.find("HTTP") != std::string::npos);

  request.url = "https://leetcode.com/graphql";
  request.timeout_seconds = 0;
  REQUIRE_FALSE(LeetCodeHttp::valid_request(request, error));
  REQUIRE(error.find("timeout") != std::string::npos);

  request.timeout_seconds = 10;
  request.headers[0].value = "application/json\r\nInjected: yes";
  REQUIRE_FALSE(LeetCodeHttp::valid_request(request, error));
  REQUIRE(error.find("header") != std::string::npos);
}

TEST_CASE("LeetCode credential storage reports platform availability", "[leetcode][credentials]")
{
#if !defined(_WIN32) && !defined(__APPLE__)
  const char *old_bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
  const std::string saved = old_bus ? old_bus : "";
  unsetenv("DBUS_SESSION_BUS_ADDRESS");
  const LeetCodeCredentials::Result result = LeetCodeCredentials::get();
  REQUIRE_FALSE(result.available);
  REQUIRE_FALSE(result.ok);
  REQUIRE(result.error.find("unavailable") != std::string::npos);
  if (!saved.empty()) setenv("DBUS_SESSION_BUS_ADDRESS", saved.c_str(), 1);
#endif
}
