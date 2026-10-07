// The `.http` request-file engine (src/features/http_file.*) and the editor's
// use of it (`:rest`).
//
// Two halves, for the two ways this can go wrong. The engine cases pin the
// request-file grammar -- separators, `# @name`, headers vs body text (a `#`
// inside a body is body text), query continuations, `< ./file` bodies -- and
// the variable resolution that quietly decides whether the request that goes
// out is the one that was written. The editor cases then pin what `:rest`
// actually does: which request the cursor picks, that an unresolved variable
// refuses the run instead of sending it wrong, and that an answer lands in one
// re-usable `[Response]` tab that `:w` cannot litter the workspace with.
#include "base/shell_util.h"
#include "editor.h"
#include "workspace/http_file.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  int occurrences(const std::string &text, const std::string &needle)
  {
    int count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
    {
      count++;
    }
    return count;
  }

  bool has_line(const std::vector<std::string> &lines, const std::string &line)
  {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
  }

  bool has_text(const std::vector<std::string> &lines, const std::string &text)
  {
    for (const std::string &line : lines)
    {
      if (line.find(text) != std::string::npos)
      {
        return true;
      }
    }
    return false;
  }

  void seed_config_home()
  {
    char home[] = "/tmp/jot_http_file_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  // The request file the editor cases drive. Its line numbers matter (the
  // cases put the caret on specific rows):
  //
  //    0  @base = http://api.test      6  ### create
  //    1                              7  POST {{base}}/items
  //    2  ### ping                    8  Content-Type: application/json
  //    3  GET {{base}}/ping           9
  //    4  Accept: application/json  10  {"title": "hi"}
  //    5                           11
  //                                12  ### broken
  //                                13  GET {{nope}}/x
  const char *kRequests =
      "@base = http://api.test\n"
      "\n"
      "### ping\n"
      "GET {{base}}/ping\n"
      "Accept: application/json\n"
      "\n"
      "### create\n"
      "POST {{base}}/items\n"
      "Content-Type: application/json\n"
      "\n"
      "{\"title\": \"hi\"}\n"
      "\n"
      "### broken\n"
      "GET {{nope}}/x\n";

  void open_requests(Editor &e, int line, int col)
  {
    static int counter = 0;
    const std::string path =
        "/tmp/jot_http_file_" + std::to_string(::getpid()) + "_" + std::to_string(counter++)
        + ".http";
    std::ofstream out(path);
    out << kRequests;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
  }

  std::string trailer(int status, const char *time, long long bytes, const char *content_type)
  {
    return std::string(HttpFile::write_out_marker()) + " " + std::to_string(status) + " " + time
           + " " + std::to_string(bytes) + " " + content_type + "\n";
  }
} // namespace

TEST_CASE("REST file: a request file parses into vars and named requests", "[jot][http]")
{
  // The `# @name` comment names the request below it; a `### title` does too,
  // and the explicit comment wins when both are present. Line 11's blank line
  // is body text, not a separator -- inside a body every byte is literal.
  const std::vector<std::string> lines = {
      "@base = http://host",      //
      "",                        //
      "### ping",                //
      "GET {{base}}/ping",       //
      "Accept: application/json", //
      "",                        //
      "{\"a\": 1}",              //
      "### second",              //
      "# @name create",          //
      "POST {{base}}/items",     //
      "",                        //
      "{\"b\": 2}",              //
  };
  const HttpFile::File file = HttpFile::parse(lines);

  REQUIRE(file.vars.at("base") == "http://host");
  REQUIRE(file.requests.size() == 2);

  const HttpFile::Request &first = file.requests[0];
  REQUIRE(first.name == "ping");
  REQUIRE(first.method == "GET");
  REQUIRE(first.url == "{{base}}/ping");
  REQUIRE(first.headers.size() == 1);
  REQUIRE(first.headers[0].name == "Accept");
  REQUIRE(first.headers[0].value == "application/json");
  REQUIRE(first.body == "{\"a\": 1}");
  REQUIRE(first.start_line == 2); // owns its `###` row
  REQUIRE(first.end_line == 6);

  const HttpFile::Request &second = file.requests[1];
  REQUIRE(second.name == "create"); // `# @name` beats the `###` title
  REQUIRE(second.method == "POST");
  REQUIRE(second.body == "{\"b\": 2}");
  REQUIRE(second.start_line == 7);

  REQUIRE(HttpFile::find_request(file, "PING") == 0); // names match loosely
  REQUIRE(HttpFile::find_request(file, " create ") == 1);
  REQUIRE(HttpFile::find_request(file, "nope") == -1);

  // The preamble runs the request it feeds; inside a section the cursor picks
  // its own request; below everything, the last one.
  REQUIRE(HttpFile::request_index_at_line(file, 0) == 0);
  REQUIRE(HttpFile::request_index_at_line(file, 3) == 0);
  REQUIRE(HttpFile::request_index_at_line(file, 10) == 1);
  REQUIRE(HttpFile::request_index_at_line(file, 99) == 1);
  REQUIRE(HttpFile::request_index_at_line(HttpFile::parse({}), 0) == -1);
}

TEST_CASE("REST file: query continuations join the URL, `< file` takes the body",
          "[jot][http]")
{
  const std::vector<std::string> lines = {
      "GET http://host/search", //
      "?q=one",                 //
      "&page=2",                //
      "### posted",             //
      "POST http://host/x",     //
      "< ./body.json",          //
  };
  const HttpFile::File file = HttpFile::parse(lines);
  REQUIRE(file.requests.size() == 2);
  REQUIRE(file.requests[0].url == "http://host/search?q=one&page=2");
  REQUIRE(file.requests[1].body_file == "./body.json");
  REQUIRE(file.requests[1].body.empty());
}

TEST_CASE("REST file: variables resolve file-first, with dynamic and env fallback",
          "[jot][http]")
{
  setenv("JOT_REST_TEST_ID", "42", 1);

  HttpFile::Request request;
  request.method = "GET";
  request.url = "{{api}}/items/{{JOT_REST_TEST_ID}}/{{$uuid}}";
  request.headers.push_back({"X-Trace", "{{missing_one}}"});

  HttpFile::Vars file_vars;
  file_vars["host"] = "example.test";
  file_vars["api"] = "{{host}}/v1"; // a var may itself reference vars

  HttpFile::DynamicVars dynamic;
  dynamic["$uuid"] = "the-uuid";

  HttpFile::Resolved resolved = HttpFile::resolve(request, file_vars, dynamic);
  REQUIRE(resolved.url == "example.test/v1/items/42/the-uuid");
  REQUIRE(resolved.headers[0].value == "{{missing_one}}"); // left as written
  REQUIRE(resolved.missing == std::vector<std::string>{"missing_one"});

  // The file's own vars shadow the environment of the same name.
  file_vars["JOT_REST_TEST_ID"] = "file-wins";
  REQUIRE(HttpFile::resolve(request, file_vars, dynamic).url
          == "example.test/v1/items/file-wins/the-uuid");
}

TEST_CASE("REST file: the dynamic variables are deterministic", "[jot][http]")
{
  const HttpFile::DynamicVars vars =
      HttpFile::make_dynamic_vars(1700000000123ULL, 0x12345678u, 0x9abcdef0u);
  REQUIRE(vars.at("$timestamp") == "1700000000");
  REQUIRE(vars.at("$datetime") == "2023-11-14T22:13:20Z");
  REQUIRE(vars.at("$randomInt") == std::to_string(0x12345678u % 1000U));

  // A v4-shaped identifier: 36 characters with the fixed nibbles in place.
  const std::string &uuid = vars.at("$uuid");
  REQUIRE(uuid.size() == 36);
  REQUIRE(uuid[14] == '4');
  REQUIRE(uuid[19] == '8');
  REQUIRE(occurrences(uuid, "-") == 4);
}

TEST_CASE("REST file: the curl command quotes what it sends", "[jot][http]")
{
  HttpFile::Resolved request;
  request.method = "POST";
  request.url = "http://host/x?a=1";
  request.headers.push_back({"Content-Type", "application/json"});
  request.body = "{\"q\": \"it's\"}";

  std::string command = HttpFile::build_curl_command(request, 30);
  REQUIRE(command.find("curl -sS -g --max-time 30 -X POST") == 0);
  REQUIRE(command.find(shell_util::shell_quote(request.url)) != std::string::npos);
  REQUIRE(command.find(shell_util::shell_quote(request.body)) != std::string::npos);
  REQUIRE(command.find(shell_util::shell_quote("Content-Type: application/json"))
          != std::string::npos);
  REQUIRE(occurrences(command, "Content-Type") == 1); // no duplicate default
  REQUIRE(command.find(HttpFile::write_out_marker()) != std::string::npos);
  REQUIRE(command.find("2>&1") != std::string::npos); // errors land in the tab too

  // A JSON body with no content type of its own gets one (curl would otherwise
  // stamp form-urlencoded on it); anything else keeps curl's own answer.
  request.headers.clear();
  command = HttpFile::build_curl_command(request, 5);
  REQUIRE(occurrences(command, "Content-Type: application/json") == 1);

  // No body, no --data-binary.
  HttpFile::Resolved get;
  get.method = "GET";
  get.url = "http://host/x";
  REQUIRE(HttpFile::build_curl_command(get, 5).find("--data-binary") == std::string::npos);
}

TEST_CASE("REST file: curl output parses back into status, headers and body", "[jot][http]")
{
  // The normal case: one header block, the body, then the --write-out trailer.
  const std::string output =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nX-Trace: abc\r\n\r\n{\"a\": 1}\n"
      + trailer(200, "0.050", 9, "application/json");
  const HttpFile::Response response = HttpFile::parse_response(output);
  REQUIRE(response.ok);
  REQUIRE(response.status == 200);
  REQUIRE(response.status_line == "HTTP/1.1 200 OK");
  REQUIRE(response.headers.size() == 2);
  REQUIRE(response.headers[0].name == "Content-Type");
  REQUIRE(response.body == "{\"a\": 1}"); // the format's newline is not body
  REQUIRE(response.seconds > 0.049);
  REQUIRE(response.seconds < 0.051);
  REQUIRE(response.bytes == 9);
  REQUIRE(response.content_type == "application/json");
  REQUIRE(response.error.empty());

  // An interim `100 Continue` is skipped for the real response, whose version
  // spelling is free-form ("HTTP/2 200" has no minor version).
  const HttpFile::Response after_100 = HttpFile::parse_response(
      "HTTP/1.1 100 Continue\r\n\r\nHTTP/2 200\r\nContent-Type: text/plain\r\n\r\nhello\n"
      + trailer(200, "0.010", 5, "text/plain"));
  REQUIRE(after_100.ok);
  REQUIRE(after_100.status_line == "HTTP/2 200");
  REQUIRE(after_100.body == "hello");

  // A transport failure is text, not a response: curl's own error is shown.
  const HttpFile::Response refused = HttpFile::parse_response(
      "curl: (7) Failed to connect to host port 80: Connection refused\n");
  REQUIRE_FALSE(refused.ok);
  REQUIRE(refused.error.find("curl: (7)") == 0);
  REQUIRE(refused.body.find("Connection refused") != std::string::npos);

  // The trailer can still arrive with a 000 code (no status was ever had).
  const HttpFile::Response zero =
      HttpFile::parse_response("curl: (6) Could not resolve host\n" + trailer(0, "0.001", 0, ""));
  REQUIRE_FALSE(zero.ok);
  REQUIRE(zero.error == "request failed");
}

TEST_CASE("REST file: JSON bodies re-indent, everything else stays as received", "[jot][http]")
{
  const std::optional<std::string> pretty = HttpFile::format_json("{\"a\":1,\"b\":[1,2]}");
  REQUIRE(pretty.has_value());
  REQUIRE(*pretty == "{\n  \"a\": 1,\n  \"b\": [\n    1,\n    2\n  ]\n}");

  // Empty containers stay on one line instead of opening a blank row.
  REQUIRE(HttpFile::format_json("{\"a\":{},\"b\":[]}") == std::string("{\n  \"a\": {},\n  \"b\": []\n}"));

  // Braces and commas inside strings are string content, not structure.
  REQUIRE(HttpFile::format_json("{\"a\":\"x,y{}\"}") == std::string("{\n  \"a\": \"x,y{}\"\n}"));

  REQUIRE_FALSE(HttpFile::format_json("{\"a\":").has_value()); // malformed
  REQUIRE_FALSE(HttpFile::format_json("hello").has_value());  // not JSON
  REQUIRE_FALSE(HttpFile::format_json("").has_value());
}

TEST_CASE("REST file: the response tab formats metadata as comments for JSON", "[jot][http]")
{
  HttpFile::Resolved request;
  request.name = "ping";
  request.method = "GET";
  request.url = "http://x/ping";

  HttpFile::Response response;
  response.ok = true;
  response.status = 200;
  response.status_line = "HTTP/1.1 200 OK";
  response.headers.push_back({"Content-Type", "application/json"});
  response.body = "{\"a\":1}";
  response.seconds = 0.05;
  response.bytes = 8;
  response.content_type = "application/json";

  REQUIRE(HttpFile::response_extension(response) == ".json");
  const std::vector<std::string> lines = HttpFile::format_response(request, response);
  REQUIRE(has_line(lines, "// GET http://x/ping"));
  REQUIRE(has_line(lines, "// 200 OK · 50 ms · 8 B"));
  REQUIRE(has_line(lines, "// Content-Type: application/json"));
  REQUIRE(has_line(lines, "// request: ping"));
  REQUIRE(has_line(lines, "{"));
  REQUIRE(has_line(lines, "  \"a\": 1"));

  // A plain-text answer gets no comment prefix (the ruleset would not colour
  // one anyway) and its body exactly as received.
  response.content_type = "text/plain";
  response.body = "plain # text";
  REQUIRE(HttpFile::response_extension(response) == ".txt");
  const std::vector<std::string> text_lines = HttpFile::format_response(request, response);
  REQUIRE(has_line(text_lines, "GET http://x/ping"));
  REQUIRE(has_line(text_lines, "plain # text"));
  REQUIRE_FALSE(has_text(text_lines, "// GET"));

  // A failure says so on its own status line instead of faking one.
  HttpFile::Response failed;
  failed.error = "curl: (7) Failed to connect";
  failed.body = "curl: (7) Failed to connect";
  const std::vector<std::string> failed_lines = HttpFile::format_response(request, failed);
  REQUIRE(failed_lines[1] == "failed: curl: (7) Failed to connect");
}

TEST_CASE("REST: the cursor picks the request, names pick the rest", "[jot][http]")
{
  seed_config_home();
  Editor e;
  open_requests(e, 3, 0);

  HttpFile::Resolved resolved;
  REQUIRE(e.rest_select_for_test("", resolved));
  REQUIRE(resolved.name == "ping");
  REQUIRE(resolved.method == "GET");
  REQUIRE(resolved.url == "http://api.test/ping"); // `{{base}}` came from the file

  // The vars preamble runs the request it feeds, not nothing.
  e.scroll_cursor_to_for_test(0, 0);
  REQUIRE(e.rest_select_for_test("", resolved));
  REQUIRE(resolved.url == "http://api.test/ping");

  REQUIRE(e.rest_select_for_test("create", resolved));
  REQUIRE(resolved.method == "POST");
  REQUIRE(resolved.url == "http://api.test/items");
  REQUIRE(resolved.body.find("{\"title\": \"hi\"}") == 0);

  REQUIRE_FALSE(e.rest_select_for_test("no-such", resolved));
  REQUIRE(e.message_for_test().find("no request named") != std::string::npos);
}

TEST_CASE("REST: an unresolved variable refuses the run instead of sending it", "[jot][http]")
{
  seed_config_home();
  Editor e;
  open_requests(e, 13, 0);

  HttpFile::Resolved resolved;
  REQUIRE_FALSE(e.rest_select_for_test("", resolved));
  REQUIRE(e.message_for_test().find("unresolved") != std::string::npos);
  REQUIRE(e.message_for_test().find("{{nope}}") != std::string::npos);

  // And there is nothing to re-run before anything has run.
  e.run_ex_for_test("rest last");
  REQUIRE(e.message_for_test().find("nothing to re-run yet") != std::string::npos);
}

TEST_CASE("REST: an answer lands in one re-usable response tab", "[jot][http]")
{
  seed_config_home();
  Editor e;
  open_requests(e, 3, 0);

  HttpFile::Resolved resolved;
  REQUIRE(e.rest_select_for_test("", resolved));

  // A canned reply, shaped exactly like build_curl_command's output.
  std::string output =
      "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\": \"pong\"}\n";
  output += trailer(200, "0.050", 18, "application/json");
  e.rest_deliver_for_test(resolved, output);

  const std::vector<std::string> lines = e.rest_response_lines_for_test();
  REQUIRE(has_line(lines, "// GET http://api.test/ping"));
  REQUIRE(has_text(lines, "200 OK · 50 ms · 18 B"));
  REQUIRE(has_line(lines, "  \"status\": \"pong\"")); // JSON re-indented
  REQUIRE(e.message_for_test().find("200 in 50 ms") != std::string::npos);

  // The next answer replaces the content where it is -- one tab per session,
  // not one per run.
  std::string again = "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\n\r\n{\"n\": 2}\n";
  again += trailer(201, "0.002", 8, "application/json");
  e.rest_deliver_for_test(resolved, again);
  const std::vector<std::string> second = e.rest_response_lines_for_test();
  REQUIRE(has_line(second, "  \"n\": 2"));
  REQUIRE_FALSE(has_text(second, "pong"));

  // A scratch view is not a file: `:w` must not litter the workspace with one.
  e.run_ex_for_test("w");
  REQUIRE_FALSE(fs::exists("[Response].json"));
}
