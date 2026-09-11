// HTTP/1.1 message parsing and response helpers (see http_message.h).
#include "markdown/http_message.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace jot_http
{
  namespace
  {
    std::string to_lower(std::string s)
    {
      for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    std::string trim(const std::string &s)
    {
      size_t b = 0;
      size_t e = s.size();
      while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
      while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
      return s.substr(b, e - b);
    }

    int hex_value(char c)
    {
      if (c >= '0' && c <= '9')
        return c - '0';
      if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
      if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
      return -1;
    }
  } // namespace

  void split_target(const std::string &target, std::string *path, std::string *query)
  {
    const size_t q = target.find('?');
    if (q == std::string::npos)
    {
      *path = target;
      query->clear();
      return;
    }
    *path = target.substr(0, q);
    *query = target.substr(q + 1);
  }

  std::string url_decode(const std::string &s)
  {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
      const char c = s[i];
      if (c == '+')
      {
        out += ' ';
        continue;
      }
      if (c == '%' && i + 2 < s.size())
      {
        const int hi = hex_value(s[i + 1]);
        const int lo = hex_value(s[i + 2]);
        if (hi >= 0 && lo >= 0)
        {
          out += static_cast<char>((hi << 4) | lo);
          i += 2;
          continue;
        }
      }
      out += c;
    }
    return out;
  }

  void parse_query(const std::string &query, std::map<std::string, std::string> *out)
  {
    size_t start = 0;
    while (start <= query.size())
    {
      const size_t amp = query.find('&', start);
      const std::string pair =
          query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
      if (!pair.empty())
      {
        const size_t eq = pair.find('=');
        const std::string key = url_decode(eq == std::string::npos ? pair : pair.substr(0, eq));
        const std::string value =
            eq == std::string::npos ? std::string() : url_decode(pair.substr(eq + 1));
        (*out)[key] = value;
      }
      if (amp == std::string::npos)
        break;
      start = amp + 1;
    }
  }

  int parse_request(const std::string &raw, size_t max_body, Request *out, size_t *consumed)
  {
    const size_t head_end = raw.find("\r\n\r\n");
    size_t head_len = 4;
    size_t sep = head_end;
    if (sep == std::string::npos)
    {
      // Tolerate bare-LF clients (curl --http1.0 style) only if the header
      // block is clearly complete; browsers always send CRLF.
      sep = raw.find("\n\n");
      head_len = 2;
      if (sep == std::string::npos)
        return 0;
    }

    const std::string head = raw.substr(0, sep);
    std::istringstream stream(head);
    std::string request_line;
    if (!std::getline(stream, request_line))
      return 0;
    if (!request_line.empty() && request_line.back() == '\r')
      request_line.pop_back();

    Request req;
    {
      std::istringstream line(request_line);
      if (!(line >> req.method >> req.target >> req.version))
        return -1;
    }
    std::string query;
    split_target(req.target, &req.path, &query);
    req.path = url_decode(req.path);
    parse_query(query, &req.query);

    std::string header;
    while (std::getline(stream, header))
    {
      if (!header.empty() && header.back() == '\r')
        header.pop_back();
      if (header.empty())
        continue;
      const size_t colon = header.find(':');
      if (colon == std::string::npos)
        continue;
      req.headers[to_lower(trim(header.substr(0, colon)))] = trim(header.substr(colon + 1));
    }

    size_t content_length = 0;
    const auto it = req.headers.find("content-length");
    if (it != req.headers.end())
    {
      content_length = static_cast<size_t>(std::strtoull(it->second.c_str(), nullptr, 10));
      if (content_length > max_body)
        return -1;
    }

    const size_t body_start = sep + head_len;
    if (raw.size() < body_start + content_length)
      return 0;
    req.body = raw.substr(body_start, content_length);
    req.head_bytes = body_start;

    *out = std::move(req);
    if (consumed)
      *consumed = body_start + content_length;
    return 1;
  }

  const char *status_text(int status)
  {
    switch (status)
    {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 413:
      return "Payload Too Large";
    case 500:
      return "Internal Server Error";
    default:
      return "OK";
    }
  }

  std::string build_response(int status,
                             const std::string &content_type,
                             const std::string &body,
                             bool keep_alive)
  {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\n";
    if (!content_type.empty())
      out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Cache-Control: no-store, must-revalidate\r\n";
    out << "Access-Control-Allow-Origin: *\r\n";
    out << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n\r\n";
    out << body;
    return out.str();
  }

  std::string mime_type_for(const std::string &path)
  {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos)
      return "application/octet-stream";
    const std::string ext = to_lower(path.substr(dot + 1));
    static const std::map<std::string, std::string> kTypes = {
        {"png", "image/png"},        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},      {"gif", "image/gif"},
        {"webp", "image/webp"},      {"bmp", "image/bmp"},
        {"ico", "image/x-icon"},     {"svg", "image/svg+xml"},
        {"avif", "image/avif"},      {"tiff", "image/tiff"},
        {"tif", "image/tiff"},       {"mp4", "video/mp4"},
        {"webm", "video/webm"},      {"pdf", "application/pdf"},
        {"html", "text/html"},       {"htm", "text/html"},
        {"css", "text/css"},         {"js", "text/javascript"},
        {"json", "application/json"},{"txt", "text/plain"},
        {"woff", "font/woff"},       {"woff2", "font/woff2"},
    };
    const auto it = kTypes.find(ext);
    return it == kTypes.end() ? "application/octet-stream" : it->second;
  }

  std::string sse_frame(const std::string &event, const std::string &data)
  {
    std::string out;
    if (!event.empty())
      out += "event: " + event + "\n";
    size_t start = 0;
    for (;;)
    {
      const size_t nl = data.find('\n', start);
      const std::string line =
          data.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
      out += "data: " + line + "\n";
      if (nl == std::string::npos)
        break;
      start = nl + 1;
    }
    out += "\n";
    return out;
  }

  std::string json_escape(const std::string &s)
  {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
      switch (c)
      {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20)
        {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        }
        else
        {
          out += static_cast<char>(c);
        }
      }
    }
    return out;
  }
} // namespace jot_http
