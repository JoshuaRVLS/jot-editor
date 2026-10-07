#ifndef JOT_FEATURES_LEETCODE_HTTP_H
#define JOT_FEATURES_LEETCODE_HTTP_H

#include <string>
#include <vector>

namespace LeetCodeHttp
{
  struct Header
  {
    std::string name;
    std::string value;
  };

  struct Request
  {
    std::string method;
    std::string url;
    std::string body;
    std::vector<Header> headers;
    int timeout_seconds = 20;
  };

  struct Response
  {
    bool ok = false;
    int status = 0;
    std::string status_line;
    std::vector<Header> headers;
    std::string body;
    std::string error;
  };

  Response perform(const Request &request);
  bool valid_request(const Request &request, std::string &error);
} // namespace LeetCodeHttp

#endif
