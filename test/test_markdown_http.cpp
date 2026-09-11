// The markdown preview server speaks a deliberately small slice of HTTP/1.1:
// parse one request head + body, build one response, stream Server-Sent
// Events. These tests pin that slice, since the browser is the only real
// client and a parsing regression shows up as a blank preview page.
#include "markdown/http_message.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace jot_http;

TEST_CASE("HTTP request head parses method, path and query", "[markdown][http]")
{
  const std::string raw =
      "GET /image?path=%2Ftmp%2Fa%20b.png&x=1 HTTP/1.1\r\n"
      "Host: 127.0.0.1:8080\r\n"
      "User-Agent: curl/8\r\n"
      "\r\n";
  Request req;
  size_t consumed = 0;
  REQUIRE(parse_request(raw, 1024, &req, &consumed) == 1);
  REQUIRE(req.method == "GET");
  // The path is decoded separately from the query; both arrive decoded.
  REQUIRE(req.path == "/image");
  REQUIRE(req.query.at("path") == "/tmp/a b.png");
  REQUIRE(req.query.at("x") == "1");
  REQUIRE(req.headers.at("host") == "127.0.0.1:8080");
  REQUIRE(req.headers.at("user-agent") == "curl/8");
  REQUIRE(consumed == raw.size());
}

TEST_CASE("HTTP POST body is read from Content-Length", "[markdown][http]")
{
  const std::string raw =
      "POST /sync HTTP/1.1\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 13\r\n"
      "\r\n"
      "{\"line\":1234}";
  Request req;
  size_t consumed = 0;
  REQUIRE(parse_request(raw, 1024, &req, &consumed) == 1);
  REQUIRE(req.method == "POST");
  REQUIRE(req.path == "/sync");
  REQUIRE(req.body == "{\"line\":1234}");
  REQUIRE(consumed == raw.size());
}

TEST_CASE("Incomplete and malformed requests are distinguished", "[markdown][http]")
{
  Request req;
  size_t consumed = 0;
  REQUIRE(parse_request("GET / HTTP/1.1\r\nHost: x\r\n", 1024, &req, &consumed) == 0);
  REQUIRE(parse_request("POST /sync HTTP/1.1\r\nContent-Length: 5\r\n\r\nab", 1024, &req, &consumed) == 0);
  // Not even a request line.
  REQUIRE(parse_request("nonsense\r\n\r\n", 1024, &req, &consumed) == -1);
  // Body larger than the configured cap is rejected instead of allocated.
  REQUIRE(parse_request("POST /sync HTTP/1.1\r\nContent-Length: 999999\r\n\r\n", 16, &req, &consumed) == -1);
}

TEST_CASE("URL decoding and query parsing match form encoding", "[markdown][http]")
{
  REQUIRE(url_decode("a%20b%2Fc") == "a b/c");
  REQUIRE(url_decode("a+b") == "a b");
  REQUIRE(url_decode("%zz") == "%zz"); // invalid escape is left alone
  std::map<std::string, std::string> out;
  parse_query("a=1&b=hello+world&empty=&novalue", &out);
  REQUIRE(out.at("a") == "1");
  REQUIRE(out.at("b") == "hello world");
  REQUIRE(out.at("empty") == "");
  REQUIRE(out.count("novalue") == 1);
}

TEST_CASE("Responses and SSE frames are well formed", "[markdown][http]")
{
  const std::string response = build_response(200, "text/html", "hi", false);
  REQUIRE(response.find("HTTP/1.1 200 OK\r\n") == 0);
  REQUIRE(response.find("Content-Length: 2\r\n") != std::string::npos);
  REQUIRE(response.find("Connection: close\r\n") != std::string::npos);
  REQUIRE(response.size() > response.find("\r\n\r\n") + 4);

  const std::string keep = build_response(204, "", "", true);
  REQUIRE(keep.find("Connection: keep-alive\r\n") != std::string::npos);

  const std::string frame = sse_frame("content", "one\ntwo");
  REQUIRE(frame == "event: content\ndata: one\ndata: two\n\n");
  REQUIRE(sse_frame("", "x") == "data: x\n\n");

  REQUIRE(json_escape("a\"b\\c\nd") == "a\\\"b\\\\c\\nd");
}

TEST_CASE("MIME types come from the file extension", "[markdown][http]")
{
  REQUIRE(mime_type_for("/tmp/a.PNG") == "image/png");
  REQUIRE(mime_type_for("/tmp/a.jpeg") == "image/jpeg");
  REQUIRE(mime_type_for("/tmp/a.svg") == "image/svg+xml");
  REQUIRE(mime_type_for("/tmp/a.unknown") == "application/octet-stream");
  REQUIRE(mime_type_for("/tmp/noext") == "application/octet-stream");
}
