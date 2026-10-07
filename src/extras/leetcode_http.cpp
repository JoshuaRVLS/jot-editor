#include "extras/leetcode_http.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace LeetCodeHttp
{
  namespace
  {
    struct CurlResult
    {
      std::string body;
      std::vector<Header> headers;
      std::string status_line;
    };

    size_t write_body(char *data, size_t size, size_t count, void *context)
    {
      const size_t bytes = size * count;
      static_cast<CurlResult *>(context)->body.append(data, bytes);
      return bytes;
    }

    std::string trim(std::string value)
    {
      const size_t first = value.find_first_not_of(" \t\r\n");
      if (first == std::string::npos)
        return {};
      const size_t last = value.find_last_not_of(" \t\r\n");
      return value.substr(first, last - first + 1);
    }

    size_t write_header(char *data, size_t size, size_t count, void *context)
    {
      const size_t bytes = size * count;
      CurlResult &result = *static_cast<CurlResult *>(context);
      std::string line(data, bytes);
      if (line.rfind("HTTP/", 0) == 0)
      {
        result.status_line = trim(line);
        result.headers.clear();
      }
      else
      {
        const size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
          result.headers.push_back({trim(line.substr(0, colon)), trim(line.substr(colon + 1))});
        }
      }
      return bytes;
    }

    bool safe_header(const Header &header)
    {
      return !header.name.empty() && header.name.find_first_of("\r\n:") == std::string::npos
             && header.value.find_first_of("\r\n") == std::string::npos;
    }
  } // namespace

  bool valid_request(const Request &request, std::string &error)
  {
    if (request.url.rfind("https://", 0) != 0 && request.url.rfind("http://", 0) != 0)
    {
      error = "URL must use HTTP or HTTPS";
      return false;
    }
    if (request.method.empty()
        || !std::all_of(request.method.begin(),
                        request.method.end(),
                        [](unsigned char c) { return std::isalpha(c) != 0; }))
    {
      error = "invalid HTTP method";
      return false;
    }
    if (request.timeout_seconds < 1 || request.timeout_seconds > 120)
    {
      error = "timeout must be between 1 and 120 seconds";
      return false;
    }
    for (const Header &header : request.headers)
    {
      if (!safe_header(header))
      {
        error = "invalid HTTP header";
        return false;
      }
    }
    return true;
  }

  Response perform(const Request &request)
  {
    Response response;
    if (!valid_request(request, response.error))
      return response;

    static std::once_flag curl_init;
    static CURLcode init_result = CURLE_FAILED_INIT;
    std::call_once(curl_init, [] { init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (init_result != CURLE_OK)
    {
      response.error = "HTTP client initialization failed";
      return response;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
      response.error = "HTTP request initialization failed";
      return response;
    }

    CurlResult result;
    curl_slist *headers = nullptr;
    for (const Header &header : request.headers)
    {
      headers = curl_slist_append(headers, (header.name + ": " + header.value).c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &result);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(request.timeout_seconds, 15));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    if (!request.body.empty())
    {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request.body.size());
    }

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = (int)status;
    response.status_line = std::move(result.status_line);
    response.headers = std::move(result.headers);
    response.body = std::move(result.body);
    response.ok = code == CURLE_OK && status > 0;
    if (code != CURLE_OK)
    {
      response.error = "HTTP request failed: ";
      response.error += curl_easy_strerror(code);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
  }
} // namespace LeetCodeHttp
