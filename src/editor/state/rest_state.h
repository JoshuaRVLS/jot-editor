#ifndef JOT_STATE_REST_STATE_H
#define JOT_STATE_REST_STATE_H

#include "workspace/http_file.h"
#include <string>

// The HTTP client's session (see workspace/http_file.h for the file format and
// editor/app/rest_client.cpp for the runs): what `:rest last` would send again and
// whether one curl is still in flight. The response tab itself is an ordinary
// buffer named `[Response].json` / `[Response].txt` -- nothing about it lives
// here, so closing it needs no cleanup.
struct RestState
{
  HttpFile::Resolved rest_last_request;
  bool rest_has_last = false;
  bool rest_request_running = false;
};

#endif
