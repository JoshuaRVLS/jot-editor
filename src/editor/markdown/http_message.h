// Minimal HTTP/1.1 helpers for the markdown preview server.
//
// The preview server only speaks the small subset of HTTP a browser needs to
// fetch one page, open one EventSource and post scroll positions back, so the
// message layer here is deliberately tiny: parse a request head + optional
// body, build a response, decode URL escapes, guess a MIME type. It owns no
// sockets and no state, which keeps it testable without a live loop.
#ifndef JOT_MARKDOWN_HTTP_MESSAGE_H
#define JOT_MARKDOWN_HTTP_MESSAGE_H

#include <cstddef>
#include <map>
#include <string>

namespace jot_http
{
  struct Request
  {
    std::string method;
    std::string target; // raw request target (path + query)
    std::string path;   // percent-decoded path, no query
    std::string version;
    std::map<std::string, std::string> query;   // decoded key/value pairs
    std::map<std::string, std::string> headers; // keys lower-cased
    std::string body;
    size_t head_bytes = 0; // bytes consumed by the head (through CRLFCRLF)
  };

  // Attempts to parse one complete request off the front of `raw`.
  //
  // Returns:
  //   1  a complete request was parsed into `out`, `*consumed` advanced
  //   0  the data is incomplete (A complete request may still arrive)
  //  -1  the request is malformed; the caller should reply 400 and close
  //
  // `max_body` bounds Content-Length so a rogue client cannot make the editor
  // allocate without limit.
  int parse_request(const std::string &raw, size_t max_body, Request *out, size_t *consumed);

  // Splits "a=1&b=2" into decoded key/value pairs. Later duplicates win.
  void parse_query(const std::string &query, std::map<std::string, std::string> *out);

  // Percent-decodes `s` (also turning '+' into a space, per form encoding).
  std::string url_decode(const std::string &s);

  // Splits a request target into its path and query parts.
  void split_target(const std::string &target, std::string *path, std::string *query);

  // Builds a complete response. `keep_alive` keeps the connection open (used by
  // the EventSource stream); every other response asks the browser to close.
  std::string build_response(int status,
                             const std::string &content_type,
                             const std::string &body,
                             bool keep_alive = false);

  // A one-line status text for a code ("OK", "Not Found", ...).
  const char *status_text(int status);

  // Guesses a MIME type from a filename extension; defaults to
  // application/octet-stream.
  std::string mime_type_for(const std::string &path);

  // Encodes a Server-Sent Events frame ("event: x\ndata: ...\n\n"). Multi-line
  // payloads get one `data:` line each, as the SSE spec requires.
  std::string sse_frame(const std::string &event, const std::string &data);

  // Escapes a string for embedding in a JSON string literal.
  std::string json_escape(const std::string &s);
} // namespace jot_http

#endif // JOT_MARKDOWN_HTTP_MESSAGE_H
