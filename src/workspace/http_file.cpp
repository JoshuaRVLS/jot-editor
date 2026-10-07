// The `.http` request-file engine (see http_file.h for what each piece is for).
//
// The parser is a single line-oriented pass with one bit of care worth naming:
// a `#` starts a comment only *outside* a body. Inside a body every byte is
// literal -- a JSON body with a `#fragment` in a URL string, or a shell script
// body full of `#` lines, must survive untouched. The body therefore begins at
// the first blank line after the request line (or at the first line that cannot
// be a header) and runs to the next `###`.
#include "workspace/http_file.h"

#include "base/shell_util.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <sstream>

namespace HttpFile
{
  namespace
  {
    std::string trim_copy(const std::string &s)
    {
      size_t b = s.find_first_not_of(" \t\r\n");
      if (b == std::string::npos)
      {
        return "";
      }
      size_t e = s.find_last_not_of(" \t\r\n");
      return s.substr(b, e - b + 1);
    }

    std::string lower_copy(std::string s)
    {
      std::transform(
          s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    }

    bool is_method(const std::string &token)
    {
      static const char *const kMethods[] = {"GET",
                                             "POST",
                                             "PUT",
                                             "DELETE",
                                             "PATCH",
                                             "HEAD",
                                             "OPTIONS",
                                             "TRACE",
                                             "CONNECT",
                                             "GRAPHQL"};
      for (const char *m : kMethods)
      {
        if (token == m)
        {
          return true;
        }
      }
      return false;
    }

    // `@name = value` (or `@name=value`); false when the line is not one.
    bool parse_var_line(const std::string &trimmed, Vars &vars)
    {
      if (trimmed.size() < 2 || trimmed[0] != '@')
      {
        return false;
      }
      const size_t eq = trimmed.find('=');
      if (eq == std::string::npos || eq < 2)
      {
        return false;
      }
      const std::string name = trim_copy(trimmed.substr(1, eq - 1));
      if (name.empty())
      {
        return false;
      }
      vars[name] = trim_copy(trimmed.substr(eq + 1));
      return true;
    }

    // `# @name foo` (or `// @name foo`) names the request below it.
    bool parse_name_comment(const std::string &trimmed, std::string &name)
    {
      size_t off = 0;
      if (trimmed.rfind("//", 0) == 0)
      {
        off = 2;
      }
      else if (trimmed.rfind("#", 0) == 0)
      {
        off = 1;
      }
      else
      {
        return false;
      }
      const std::string meta = trim_copy(trimmed.substr(off));
      if (meta.rfind("@name", 0) != 0)
      {
        return false;
      }
      if (meta.size() > 5 && !std::isspace((unsigned char)meta[5]))
      {
        return false;
      }
      name = trim_copy(meta.substr(5));
      return true;
    }

    // A header name is a bare token (`Content-Type`); anything else before a
    // colon (`{"x":`, `data-id:` spelled with quotes) is body text that just
    // happens to contain one, and must not be swallowed as a header.
    bool is_header_name(const std::string &name)
    {
      if (name.empty())
      {
        return false;
      }
      for (char c : name)
      {
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_')
        {
          return false;
        }
      }
      return true;
    }

    // `GET https://host/path` (an optional trailing `HTTP/1.1` is dropped).
    bool parse_request_line(const std::string &trimmed, std::string &method, std::string &url)
    {
      const size_t sp = trimmed.find_first_of(" \t");
      if (sp == std::string::npos)
      {
        return false;
      }
      const std::string verb = trimmed.substr(0, sp);
      if (!is_method(verb))
      {
        return false;
      }
      std::string rest = trim_copy(trimmed.substr(sp + 1));
      const size_t sp2 = rest.find_first_of(" \t");
      if (sp2 != std::string::npos)
      {
        const std::string maybe_version = trim_copy(rest.substr(sp2 + 1));
        if (maybe_version.rfind("HTTP/", 0) == 0)
        {
          rest = rest.substr(0, sp2);
        }
      }
      if (rest.empty())
      {
        return false;
      }
      method = verb;
      url = rest;
      return true;
    }

    // Substitutes `{{name}}`. Lookup order: the file's `@vars` (each expanded
    // recursively, so `@api = {{host}}/v1` composes), then the dynamic `$` names,
    // then the process environment. What resolves to nothing stays literal and
    // is reported -- silently dropping it would send a wrong request that looks
    // right.
    std::string expand(const std::string &text,
                       const Vars &file_vars,
                       const DynamicVars &dynamic,
                       int depth,
                       std::vector<std::string> &missing)
    {
      std::string out;
      out.reserve(text.size());
      size_t i = 0;
      while (i < text.size())
      {
        const size_t open = text.find("{{", i);
        if (open == std::string::npos)
        {
          out += text.substr(i);
          break;
        }
        const size_t close = text.find("}}", open + 2);
        if (close == std::string::npos)
        {
          out += text.substr(i);
          break;
        }
        out += text.substr(i, open - i);
        const std::string name = trim_copy(text.substr(open + 2, close - open - 2));
        std::string value;
        bool found = false;
        auto var = file_vars.find(name);
        if (var != file_vars.end())
        {
          // A var that references itself would spin; the depth cap stops it.
          value =
              depth < 8 ? expand(var->second, file_vars, dynamic, depth + 1, missing) : var->second;
          found = true;
        }
        else if (!name.empty() && name[0] == '$')
        {
          auto dyn = dynamic.find(name);
          if (dyn != dynamic.end())
          {
            value = dyn->second;
            found = true;
          }
        }
        else if (!name.empty())
        {
          const char *env = std::getenv(name.c_str());
          if (env)
          {
            value = env;
            found = true;
          }
        }
        if (found)
        {
          out += value;
        }
        else
        {
          out += text.substr(open, close - open + 2);
          if (std::find(missing.begin(), missing.end(), name) == missing.end())
          {
            missing.push_back(name);
          }
        }
        i = close + 2;
      }
      return out;
    }

    std::vector<std::string> split_lines(const std::string &text)
    {
      std::vector<std::string> lines;
      std::string current;
      for (char c : text)
      {
        if (c == '\n')
        {
          lines.push_back(current);
          current.clear();
        }
        else if (c != '\r')
        {
          current.push_back(c);
        }
      }
      lines.push_back(current);
      return lines;
    }

    std::string status_reason(const Response &response)
    {
      const size_t sp = response.status_line.find(' ');
      if (sp == std::string::npos)
      {
        return response.status_line;
      }
      return trim_copy(response.status_line.substr(sp));
    }
  } // namespace

  File parse(const std::vector<std::string> &lines)
  {
    File file;
    Request current;
    bool have_request = false;
    bool in_body = false;
    bool have_separator = false;
    std::string pending_name;
    int section_start = 0;

    auto flush = [&](int end_line)
    {
      if (have_request)
      {
        current.end_line = std::max(current.start_line, end_line);
        file.requests.push_back(current);
      }
      current = Request();
      have_request = false;
      in_body = false;
      have_separator = false;
      pending_name.clear();
    };

    for (int i = 0; i < (int)lines.size(); i++)
    {
      const std::string &raw = lines[(size_t)i];
      const std::string trimmed = trim_copy(raw);

      if (trimmed.rfind("###", 0) == 0)
      {
        flush(i - 1);
        have_separator = true;
        section_start = i;
        pending_name = trim_copy(trimmed.substr(3));
        continue;
      }

      if (!have_request)
      {
        if (trimmed.empty())
        {
          continue;
        }
        std::string comment_name;
        if (parse_name_comment(trimmed, comment_name))
        {
          pending_name = comment_name;
          continue;
        }
        if (trimmed[0] == '#' || trimmed.rfind("//", 0) == 0)
        {
          continue;
        }
        if (parse_var_line(trimmed, file.vars))
        {
          continue;
        }
        std::string method, url;
        if (!parse_request_line(trimmed, method, url))
        {
          continue; // stray prose above a request: not ours to interpret
        }
        current.name = pending_name;
        current.method = method;
        current.url = url;
        current.start_line = have_separator ? section_start : i;
        pending_name.clear();
        have_request = true;
        in_body = false;
        continue;
      }

      if (!in_body)
      {
        if (trimmed.empty())
        {
          in_body = true;
          continue;
        }
        if (trimmed[0] == '?' || trimmed[0] == '&')
        {
          current.url += trimmed; // query-param continuation line
          continue;
        }
        std::string comment_name;
        if (trimmed[0] == '#' || trimmed.rfind("//", 0) == 0)
        {
          parse_name_comment(trimmed, comment_name);
          continue;
        }
        if (parse_var_line(trimmed, file.vars))
        {
          continue;
        }
        const size_t colon = trimmed.find(':');
        if (colon != std::string::npos && colon > 0 && trimmed.find_first_of(" \t") > colon
            && is_header_name(trimmed.substr(0, colon)))
        {
          Header header;
          header.name = trim_copy(trimmed.substr(0, colon));
          header.value = trim_copy(trimmed.substr(colon + 1));
          current.headers.push_back(header);
          continue;
        }
        in_body = true; // not a header: the body starts here, no blank line
      }

      if (in_body)
      {
        // `< ./body.json` alone on the first body line takes the body from
        // that file; once anything else is present the body is literal.
        if (current.body_file.empty() && current.body.empty() && trimmed[0] == '<')
        {
          const std::string path = trim_copy(trimmed.substr(1));
          if (!path.empty() && path.find_first_of(" \t") == std::string::npos)
          {
            current.body_file = path;
            continue;
          }
        }
        if (!current.body.empty())
        {
          current.body += '\n';
        }
        current.body += raw;
      }
    }
    flush((int)lines.size() - 1);
    return file;
  }

  int request_index_at_line(const File &file, int line)
  {
    for (int i = 0; i < (int)file.requests.size(); i++)
    {
      const Request &request = file.requests[(size_t)i];
      if (line >= request.start_line && line <= request.end_line)
      {
        return i;
      }
    }
    for (int i = 0; i < (int)file.requests.size(); i++)
    {
      if (file.requests[(size_t)i].start_line > line)
      {
        return i;
      }
    }
    return file.requests.empty() ? -1 : (int)file.requests.size() - 1;
  }

  int find_request(const File &file, const std::string &name)
  {
    const std::string needle = lower_copy(trim_copy(name));
    for (int i = 0; i < (int)file.requests.size(); i++)
    {
      if (lower_copy(file.requests[(size_t)i].name) == needle && !needle.empty())
      {
        return i;
      }
    }
    return -1;
  }

  DynamicVars make_dynamic_vars(unsigned long long now_ms, unsigned random_a, unsigned random_b)
  {
    DynamicVars vars;

    // The two supplied random words expanded to four (a tiny avalanche mix),
    // so the identifier below is 32 hex digits without pulling in a uuid
    // library. Randomness itself is the caller's business: this is a pure
    // function of its inputs and a test can pin every character.
    const unsigned w2 = random_a * 2654435761u ^ (random_b >> 7);
    const unsigned w3 = random_b * 2246822519u ^ (random_a >> 5);
    char hex[40];
    std::snprintf(hex, sizeof(hex), "%08x%08x%08x%08x", random_a, random_b, w2, w3);
    const std::string h = hex;
    // A v4-shaped identifier (the version/variant nibbles are fixed).
    vars["$uuid"] = h.substr(0, 8) + "-" + h.substr(8, 4) + "-4" + h.substr(13, 3) + "-8"
                    + h.substr(17, 3) + "-" + h.substr(20, 12);
    vars["$timestamp"] = std::to_string(now_ms / 1000ULL);

    const std::time_t secs = (std::time_t)(now_ms / 1000ULL);
    std::tm tm_utc{};
#ifdef _WIN32
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    vars["$datetime"] = stamp;

    vars["$randomInt"] = std::to_string(random_a % 1000U);
    return vars;
  }

  Resolved resolve(const Request &request, const Vars &file_vars, const DynamicVars &dynamic)
  {
    Resolved out;
    out.name = request.name;
    out.method = request.method;
    out.url = expand(request.url, file_vars, dynamic, 0, out.missing);
    for (const Header &header : request.headers)
    {
      Header resolved;
      resolved.name = header.name;
      resolved.value = expand(header.value, file_vars, dynamic, 0, out.missing);
      out.headers.push_back(resolved);
    }
    out.body = expand(request.body, file_vars, dynamic, 0, out.missing);
    std::sort(out.missing.begin(), out.missing.end());
    return out;
  }

  const char *write_out_marker()
  {
    return "__JOT_REST_META__";
  }

  std::string build_curl_command(const Resolved &request, int timeout_s)
  {
    std::string command =
        "curl -sS -g --max-time " + std::to_string(timeout_s) + " -X " + request.method;
    command += " " + shell_util::shell_quote(request.url);
    bool have_content_type = false;
    for (const Header &header : request.headers)
    {
      command += " -H " + shell_util::shell_quote(header.name + ": " + header.value);
      if (lower_copy(header.name) == "content-type")
      {
        have_content_type = true;
      }
    }
    if (!request.body.empty())
    {
      command += " --data-binary " + shell_util::shell_quote(request.body);
      // curl stamps `application/x-www-form-urlencoded` on any --data body.
      // For a JSON-looking body with no content type of its own that default is
      // wrong often enough to fix; anything else keeps curl's own answer.
      const std::string head = trim_copy(request.body.substr(0, 1));
      if (!have_content_type && (head == "{" || head == "["))
      {
        command += " -H " + shell_util::shell_quote("Content-Type: application/json");
      }
    }
    command += " -w ";
    command += shell_util::shell_quote(std::string("\n") + write_out_marker()
                                       + " %{http_code} %{time_total} %{size_download}"
                                         " %{content_type}\n");
    // stderr joins stdout so a transport failure ("curl: (7) Failed to ...")
    // is visible in the response tab instead of scrolling over the editor.
    command += " 2>&1";
    return command;
  }

  Response parse_response(const std::string &output)
  {
    Response response;
    const std::string marker = write_out_marker();
    const size_t meta_at = output.rfind(marker);
    if (meta_at == std::string::npos)
    {
      // No trailer: curl never got far enough to write one (or the shell could
      // not find it at all). What it said is the error -- the generic phrasing
      // is only for a run that produced literally nothing.
      response.error = output.empty() ? std::string("no response (curl produced no output)")
                                      : output.substr(0, output.find('\n'));
      response.body = output;
      return response;
    }

    std::istringstream trailer(output.substr(meta_at + marker.size()));
    std::string code, time, size;
    trailer >> code >> time >> size;
    std::string content_type;
    std::getline(trailer, content_type);
    response.content_type = trim_copy(content_type);
    response.status = code.empty() ? 0 : std::atoi(code.c_str());
    response.seconds = time.empty() ? 0.0 : std::atof(time.c_str());
    response.bytes = size.empty() ? 0 : std::atoll(size.c_str());

    std::string raw = output.substr(0, meta_at);
    // The trailer's format string opens with a newline of its own; that one is
    // not part of the body.
    if (!raw.empty() && raw.back() == '\n')
    {
      raw.pop_back();
    }

    if (response.status <= 0)
    {
      response.error = "request failed";
      response.body = raw;
      return response;
    }

    // Walk the header block(s). curl prints one per response; a `100 Continue`
    // (or any 1xx) is followed immediately by the real one. Only an *initial*
    // block is considered, so an "HTTP/..." line inside the body can never be
    // mistaken for a second response.
    size_t pos = 0;
    for (int guard = 0; guard < 4; guard++)
    {
      const size_t sep = std::min(raw.find("\r\n\r\n", pos), raw.find("\n\n", pos));
      size_t body_at = raw.size();
      size_t head_end = raw.size();
      if (raw.find("\r\n\r\n", pos) == sep && sep != std::string::npos)
      {
        body_at = sep + 4;
        head_end = sep;
      }
      else if (sep != std::string::npos)
      {
        body_at = sep + 2;
        head_end = sep;
      }

      std::vector<std::string> head = split_lines(raw.substr(pos, head_end - pos));
      if (head.empty() || head[0].rfind("HTTP/", 0) != 0)
      {
        break;
      }
      response.status_line = head[0];
      response.headers.clear();
      for (int i = 1; i < (int)head.size(); i++)
      {
        const size_t colon = head[(size_t)i].find(':');
        if (colon == std::string::npos)
        {
          continue;
        }
        Header header;
        header.name = trim_copy(head[(size_t)i].substr(0, colon));
        header.value = trim_copy(head[(size_t)i].substr(colon + 1));
        response.headers.push_back(header);
      }

      // The code sits after the version's first space ("HTTP/1.1 200 ...",
      // "HTTP/2 200 ..." -- the version width is not fixed).
      const size_t code_at = response.status_line.find(' ');
      const std::string code_str =
          code_at == std::string::npos ? "" : response.status_line.substr(code_at + 1, 3);
      const bool interim = !code_str.empty() && code_str[0] == '1';
      if (interim && raw.compare(body_at, 5, "HTTP/") == 0)
      {
        pos = body_at;
        continue;
      }
      response.body = raw.substr(body_at);
      response.ok = true;
      return response;
    }

    // No HTTP block at all: curl's own error text is the whole output.
    response.error = raw.substr(0, std::min<size_t>(raw.find('\n'), raw.size()));
    response.body = raw;
    return response;
  }

  std::optional<std::string> format_json(const std::string &text)
  {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
      return std::nullopt;
    }
    if (text[first] != '{' && text[first] != '[')
    {
      return std::nullopt;
    }
    const std::string indent_unit = "  ";
    std::string out;
    out.reserve(text.size() + text.size() / 4);
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    bool open_pending = false; // the container just opened holds nothing (yet)
    for (char c : text)
    {
      if (in_string)
      {
        out.push_back(c);
        if (escaped)
        {
          escaped = false;
        }
        else if (c == '\\')
        {
          escaped = true;
        }
        else if (c == '"')
        {
          in_string = false;
        }
        continue;
      }
      switch (c)
      {
      case '"':
        in_string = true;
        open_pending = false;
        out.push_back(c);
        break;
      case '{':
      case '[':
        out.push_back(c);
        depth++;
        out.push_back('\n');
        out.append((size_t)depth * indent_unit.size(), ' ');
        open_pending = true;
        break;
      case '}':
      case ']':
        if (depth <= 0)
        {
          return std::nullopt;
        }
        depth--;
        if (open_pending)
        {
          // `[]` and `{}` stay on one line instead of opening a blank row.
          while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
          {
            out.pop_back();
          }
        }
        else
        {
          out.push_back('\n');
          out.append((size_t)depth * indent_unit.size(), ' ');
        }
        out.push_back(c);
        open_pending = false;
        break;
      case ',':
        out.push_back(c);
        out.push_back('\n');
        out.append((size_t)depth * indent_unit.size(), ' ');
        open_pending = false;
        break;
      case ':':
        out.append(": ");
        break;
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        break;
      default:
        open_pending = false;
        out.push_back(c);
        break;
      }
    }
    if (in_string || depth != 0)
    {
      return std::nullopt;
    }
    return out;
  }

  std::string response_extension(const Response &response)
  {
    if (lower_copy(response.content_type).find("json") != std::string::npos)
    {
      return ".json";
    }
    return format_json(response.body) ? ".json" : ".txt";
  }

  std::vector<std::string> format_response(const Resolved &request, const Response &response)
  {
    const bool as_json = response_extension(response) == ".json";
    const char *prefix = as_json ? "// " : "";
    std::vector<std::string> lines;

    lines.push_back(std::string(prefix) + request.method + " " + request.url);
    if (response.ok)
    {
      const long ms = (long)(response.seconds * 1000.0 + 0.5);
      lines.push_back(std::string(prefix) + status_reason(response) + " · " + std::to_string(ms)
                      + " ms · " + std::to_string(response.bytes) + " B");
    }
    else
    {
      lines.push_back(std::string(prefix) + "failed: "
                      + (response.error.empty() ? std::string("no response") : response.error));
    }
    for (const Header &header : response.headers)
    {
      lines.push_back(std::string(prefix) + header.name + ": " + header.value);
    }
    if (!request.name.empty())
    {
      lines.push_back(std::string(prefix) + "request: " + request.name);
    }
    lines.push_back("");

    std::string body = response.body;
    if (as_json)
    {
      if (std::optional<std::string> pretty = format_json(body))
      {
        body = *pretty;
      }
    }
    if (body.empty())
    {
      lines.push_back(std::string(prefix) + "(empty body)");
      return lines;
    }
    for (const std::string &line : split_lines(body))
    {
      lines.push_back(line);
    }
    return lines;
  }
} // namespace HttpFile
