// Completion label matching: byte offsets into `label` of the characters
// consumed by `query`, mirroring the ranker's priority (exact, prefix,
// substring, then greedy subsequence). nvim-cmp highlights these characters
// in the popup (its CmpItemAbbrMatch group); offsets are byte-based so the
// renderers can slice the raw label directly.
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace completion_matching
{
  // ASCII-only case fold: byte offsets stay stable (full tolower can change
  // byte length for some non-ASCII characters).
  inline std::string fold(const std::string &s)
  {
    std::string out = s;
    for (char &c : out)
    {
      if (c >= 'A' && c <= 'Z')
      {
        c = (char)(c - 'A' + 'a');
      }
    }
    return out;
  }

  inline std::vector<int> match_positions(const std::string &query, const std::string &label)
  {
    std::vector<int> out;
    if (query.empty())
    {
      return out;
    }
    const std::string q = fold(query);
    const std::string lb = fold(label);

    // Exact / prefix.
    if (lb.size() >= q.size() && lb.compare(0, q.size(), q) == 0)
    {
      for (size_t i = 0; i < q.size(); i++)
      {
        out.push_back((int)i);
      }
      return out;
    }

    // Substring.
    const size_t pos = lb.find(q);
    if (pos != std::string::npos)
    {
      for (size_t i = 0; i < q.size(); i++)
      {
        out.push_back((int)(pos + i));
      }
      return out;
    }

    // Greedy subsequence (the ranker's last resort).
    size_t j = 0;
    for (size_t i = 0; i < lb.size() && j < q.size(); i++)
    {
      if (lb[i] == q[j])
      {
        out.push_back((int)i);
        j++;
      }
    }
    if (j == q.size())
    {
      return out;
    }
    out.clear();
    return out;
  }
} // namespace completion_matching