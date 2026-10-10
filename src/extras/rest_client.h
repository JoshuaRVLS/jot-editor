#ifndef JOT_EDITOR_REST_CLIENT_H
#define JOT_EDITOR_REST_CLIENT_H

#include "workspace/http_file.h" // HttpFile::Resolved
#include <string>

class Editor;

// The HTTP client behind `:rest`: the request at the cursor out of an
// IntelliJ-style request file (rest.nvim's domain), the curl that sends it, and
// the `[Response]` scratch tab its answer lands in.
//
//   `:rest`          the request the cursor is in
//   `:rest <name>`   a named request in the same file
//   `:rest last`     the request that was sent last, again
//
// The curl runs on the worker queue, so an endpoint that takes three seconds to
// answer does not take three seconds of keystrokes with it; what comes back
// lands on the main thread, into one scratch tab every later run re-uses --
// re-running replaces the content where it is instead of stacking a new tab per
// request (see extras/rest_client.cpp). The request file's format and its
// pure half are workspace/http_file.h.
//
// This used to be a RestState block plus five Editor methods; the session and
// the behaviour live together here now, and Editor keeps one of these. It is the
// third of the editor's collaborators, after SearchController and
// DiscordController: it reaches the shared editor state (the buffers, the
// message line, the worker queue) through the Editor it was built with.
class RestClient
{
public:
  explicit RestClient(Editor &editor) : editor_(editor)
  {
  }

  // `:rest [last|<name>]`, which is the whole command.
  void run(const std::string &arg);

  // Whether one curl is still in flight.
  bool running() const
  {
    return request_running_;
  }

private:
  // The suite's hooks drive a run's two halves directly, the way the command
  // does (api/test_hooks.h), so they need the surface below.
  friend class Editor;

  // Everything a run does before the network: parse the buffer, pick the
  // request (`name` empty = the one at the cursor), read an external body file
  // and resolve the variables. False reports why on the message line -- an
  // unresolved `{{var}}` refuses the run rather than sending it wrong.
  bool prepare(const std::string &name, HttpFile::Resolved &out);
  void send(HttpFile::Resolved resolved);
  void show_response(const HttpFile::Resolved &resolved, const std::string &output);
  // The response tab for this run's payload type, created on first use and
  // re-used (content swapped) from then on.
  int response_buffer(const std::string &extension);

  Editor &editor_;

  // What `:rest last` sends again, and whether one curl is still in flight. The
  // response tab itself is an ordinary buffer named `[Response].json` /
  // `[Response].txt`: nothing about it lives here, so closing it needs no
  // cleanup.
  HttpFile::Resolved last_request_;
  bool has_last_ = false;
  bool request_running_ = false;
};

#endif
