#ifndef JOT_FEATURES_HTTP_FILE_H
#define JOT_FEATURES_HTTP_FILE_H

// The `.http` / `.rest` request files -- the HTTP-client syntax IntelliJ
// invented and rest.nvim brought to editors -- parsed, resolved and executed
// here, with the editor side (editor/app/rest_client.cpp) only feeding a buffer in
// and painting a buffer out.
//
// Everything in this header is pure text work so it can be pinned by tests:
//
//   * parse() reads one file into its `@var = value` declarations and its
//     requests. Requests are separated by `###` lines (the trailing text names
//     the request below it) or named by a `# @name foo` comment. A request is
//     `METHOD URL`, optional `Key: value` headers (query lines starting with
//     `?` or `&` keep appending to the URL), a blank line, then the body -- or
//     a single `< ./file.json` line, which takes the body from that file.
//   * resolve() substitutes `{{name}}` in the URL, headers and body: the
//     file's own `@vars` first (each value may itself reference other vars),
//     then the dynamic `{{$uuid}}` / `{{$timestamp}}` / `{{$datetime}}` /
//     `{{$randomInt}}`, then the process environment. A name that resolves to
//     nothing is left as written and reported, never silently blanked.
//   * build_curl_command() turns a resolved request into one shell command;
//   * parse_response() reads curl's output back (headers, body and the
//     `--write-out` trailer that carries status, time and size);
//   * format_response() renders the result as the response tab's lines, with
//     the body pretty-printed when it is JSON (the common case) and the
//     metadata prefixed `//` so the JSON ruleset colours it as a comment.
//
// Redirects are deliberately *not* followed (no `--location`): the response
// shown is the one the server actually returned, with its `Location` header
// visible, instead of a chain curl walked behind the user's back.

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace HttpFile
{
  // `@name = value` declarations and resolved substitutions share a shape.
  using Vars = std::map<std::string, std::string>;

  // The dynamic `{{$...}}` names, pre-generated per run so a request is a
  // pure function of its inputs (and a test can pin every byte). Keys keep
  // their `$`: `"$uuid"`, `"$timestamp"`, `"$datetime"`, `"$randomInt"`.
  using DynamicVars = Vars;

  struct Header
  {
    std::string name;
    std::string value;
  };

  struct Request
  {
    std::string name; // `# @name foo`, else the `### title`, else empty
    std::string method;
    std::string url;
    std::vector<Header> headers;
    std::string body;
    // `< ./file.json`: the body lives in a sibling file and is read at resolve
    // time, relative to the request file's own directory.
    std::string body_file;
    // The lines this request owns: the `###` separator (or the request line
    // when the file has none) down to the line before the next separator.
    int start_line = 0;
    int end_line = 0;
  };

  struct File
  {
    Vars vars;
    std::vector<Request> requests;
  };

  struct Resolved
  {
    std::string name;
    std::string method;
    std::string url;
    std::vector<Header> headers;
    std::string body;
    // `{{names}}` that resolved to nothing (report them; the text keeps them
    // literal so the request is visibly wrong rather than quietly altered).
    std::vector<std::string> missing;
  };

  struct Response
  {
    bool ok = false; // curl produced a status line we could read
    int status = 0;
    std::string status_line; // "HTTP/1.1 200 OK"
    std::vector<Header> headers;
    std::string body;
    double seconds = 0.0;
    long long bytes = 0;
    std::string content_type;
    std::string error; // curl-level failure ("curl: (7) ...")
  };

  File parse(const std::vector<std::string> &lines);
  // The request a cursor line belongs to: its own section when there is one,
  // else the next request below (the vars/comments preamble runs the request
  // it feeds), else the last request in the file. -1 for an empty file.
  int request_index_at_line(const File &file, int line);
  int find_request(const File &file, const std::string &name); // -1 when absent

  DynamicVars make_dynamic_vars(unsigned long long now_ms, unsigned random_a, unsigned random_b);

  Resolved resolve(const Request &request, const Vars &file_vars, const DynamicVars &dynamic);

  // The sentinel `--write-out` appends after the response body.
  const char *write_out_marker();
  std::string build_curl_command(const Resolved &request, int timeout_s);

  // Reads curl's combined output (the command redirects stderr into it, so a
  // transport failure arrives as text before the trailer).
  Response parse_response(const std::string &output);

  // The body re-indented, when it is a JSON object or array; nullopt for
  // anything else (including malformed JSON, which is shown as received).
  std::optional<std::string> format_json(const std::string &text);

  // ".json" when the body is JSON (or claims to be), ".txt" otherwise. The
  // response tab takes its syntax highlighting from this extension.
  std::string response_extension(const Response &response);
  std::vector<std::string> format_response(const Resolved &request, const Response &response);
} // namespace HttpFile

#endif
