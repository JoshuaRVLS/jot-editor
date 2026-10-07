// The editor side of the HTTP client (see features/http_file.h for the request
// format): what `:rest` does with a parsed request and where its answer lands.
//
// The run itself is a curl on the worker queue, for the same reason the git
// status and the workspace scans are: an endpoint that takes three seconds to
// answer must not take three seconds of keystrokes with it. What comes back is
// landed on the main thread into one `[Response]` scratch tab every later run
// re-uses -- re-running replaces the content where it is instead of stacking a
// new tab per request.
//
// Every report below is a toast (`set_message(..., true)`). The status line is
// owned by the Lua UI kit in a real session, so a plain message would be
// written and then never shown -- which is exactly how a refused run used to
// look like a run that silently did nothing.
#include "base/shell_util.h"
#include "base/string_util.h"
#include "editor.h"
#include "features/http_file.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

namespace
{
  // A shell command's whole stdout, for the curl run below. Its stderr joins
  // the same stream at the command line (build_curl_command appends 2>&1), so
  // a transport error arrives as text in the response tab instead of scrolling
  // over the editor.
  std::string capture_command(const std::string &command)
  {
    std::string out;
    FILE *pipe = shell_util::open_command_pipe(command, "r");
    if (!pipe)
    {
      return out;
    }
    char chunk[8192];
    size_t read = 0;
    while ((read = fread(chunk, 1, sizeof(chunk), pipe)) > 0)
    {
      out.append(chunk, read);
    }
    shell_util::close_command_pipe(pipe);
    return out;
  }
} // namespace

void Editor::rest_run(const std::string &arg)
{
  if (string_util::lower_copy(arg) == "last")
  {
    if (!rest_has_last)
    {
      set_message("REST: nothing to re-run yet", true);
      return;
    }
    rest_send(rest_last_request);
    return;
  }

  HttpFile::Resolved resolved;
  if (!rest_prepare(arg, resolved))
  {
    return;
  }
  rest_send(std::move(resolved));
}

bool Editor::rest_prepare(const std::string &name, HttpFile::Resolved &out)
{
  const FileBuffer &buf = get_buffer();
  std::vector<std::string> lines;
  lines.reserve(buf.line_count());
  for (int i = 0; i < (int)buf.line_count(); i++)
  {
    lines.push_back(buf.line(i));
  }
  const HttpFile::File file = HttpFile::parse(lines);
  if (file.requests.empty())
  {
    set_message("REST: no request in this buffer", true);
    return false;
  }

  const int index = name.empty() ? HttpFile::request_index_at_line(file, buf.cursor.y)
                                 : HttpFile::find_request(file, name);
  if (index < 0)
  {
    set_message("REST: no request named \"" + name + "\"", true);
    return false;
  }

  HttpFile::Request request = file.requests[(size_t)index];
  // A `< ./body.json` body is read here, relative to the request file itself,
  // and becomes the request's literal body from here on.
  std::string base_dir = ".";
  if (!buf.filepath.empty())
  {
    base_dir = std::filesystem::path(buf.filepath).parent_path().string();
    if (base_dir.empty())
    {
      base_dir = ".";
    }
  }
  if (!request.body_file.empty())
  {
    std::ifstream body_in(std::filesystem::path(base_dir) / request.body_file);
    if (!body_in)
    {
      set_message("REST: cannot read body file " + request.body_file, true);
      return false;
    }
    std::ostringstream body_text;
    body_text << body_in.rdbuf();
    request.body = body_text.str();
    request.body_file.clear();
  }

  const auto now_ms =
      (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
          .count();
  std::random_device random;
  out = HttpFile::resolve(
      request, file.vars, HttpFile::make_dynamic_vars(now_ms, random(), random()));

  if (!out.missing.empty())
  {
    std::string names;
    for (const std::string &missing : out.missing)
    {
      if (!names.empty())
      {
        names += ", ";
      }
      names += "{{" + missing + "}}";
    }
    set_message("REST: unresolved " + names + " -- define @" + out.missing.front() + " = ...",
                true);
    return false;
  }
  return true;
}

void Editor::rest_send(HttpFile::Resolved resolved)
{
  if (rest_request_running)
  {
    set_message("REST: still waiting on the previous request", true);
    return;
  }
  if (!task_queue_)
  {
    set_message("REST: no worker queue in this session", true);
    return;
  }

  const std::string command = HttpFile::build_curl_command(resolved, kRestTimeoutSeconds);
  rest_last_request = resolved;
  rest_has_last = true;
  rest_request_running = true;
  set_transient_message("REST: " + resolved.method + " " + resolved.url + " ...", 4000);

  task_queue_->submit_val<std::string>(
      [command]() { return capture_command(command); },
      [this, resolved](std::string output)
      {
        rest_request_running = false;
        if (!running)
        {
          return;
        }
        rest_show_response(resolved, output);
        needs_redraw = true;
      });
}

void Editor::rest_show_response(const HttpFile::Resolved &resolved, const std::string &output)
{
  const HttpFile::Response response = HttpFile::parse_response(output);
  const int index = rest_response_buffer(HttpFile::response_extension(response));
  if (index < 0 || index >= (int)buffers.size())
  {
    return;
  }

  // Show it first, then fill it: the pane switch restores a remembered view
  // position, and a response always starts at the top.
  pane_show_buffer(index);
  reveal_tab_for_buffer(index);
  FileBuffer &buf = get_buffer();
  buf.replace_lines(0, (int)buf.line_count(), HttpFile::format_response(resolved, response));
  buf.undo_stack = std::stack<State>();
  buf.redo_stack = std::stack<State>();
  buf.cursor = {0, 0};
  buf.preferred_x = 0;
  buf.selection = {{0, 0}, {0, 0}, false};
  buf.scroll_offset = 0;
  buf.scroll_x = 0;
  buf.modified = false;

  if (response.ok)
  {
    const long ms = (long)(response.seconds * 1000.0 + 0.5);
    set_message("REST: " + std::to_string(response.status) + " in " + std::to_string(ms)
                + " ms (" + std::to_string(response.bytes) + " B)",
                true);
  }
  else
  {
    set_message("REST: "
                + (response.error.empty() ? std::string("request failed") : response.error),
                true);
  }
  needs_redraw = true;
}

int Editor::rest_response_buffer(const std::string &extension)
{
  const std::string name = "[Response]" + extension;
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (buffers[(size_t)i].filepath.rfind("[Response]", 0) == 0)
    {
      // The payload type can change between runs (JSON one request, HTML the
      // next); the name carries the extension so the right ruleset colours it.
      buffers[(size_t)i].filepath = name;
      return i;
    }
  }
  create_new_buffer();
  get_buffer().filepath = name;
  return current_buffer;
}
