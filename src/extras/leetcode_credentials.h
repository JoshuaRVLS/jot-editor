#ifndef JOT_TOOLS_LEETCODE_CREDENTIALS_H
#define JOT_TOOLS_LEETCODE_CREDENTIALS_H

#include <string>

namespace LeetCodeCredentials
{
  struct Result
  {
    bool ok = false;
    bool available = false;
    std::string value;
    std::string error;
  };

  Result get();
  Result set(const std::string &value);
  Result erase();
} // namespace LeetCodeCredentials

#endif
