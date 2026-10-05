// The sidebar timeline's model (src/jot/workspace/timeline.h).
//
// Header-only and Editor-free: the commit list a file's `git log` yields, and
// the compact relative ages its rows show. Keeping the parsing and the age
// arithmetic here lets unit tests exercise them without a repo or an editor.
#ifndef JOT_TIMELINE_H
#define JOT_TIMELINE_H

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace jot_timeline
{
  struct Commit
  {
    std::string hash;
    std::string subject;
    long long epoch = 0; // author time, seconds since the epoch
  };

  // `git log --pretty=format:%h%x09%at%x09%s` output: one commit per line,
  // tab-separated, newest first. A malformed line is skipped rather than
  // aborting the list.
  inline std::vector<Commit> parse_log(const std::string &output)
  {
    std::vector<Commit> commits;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      std::istringstream ls(line);
      Commit commit;
      if (!std::getline(ls, commit.hash, '\t') || commit.hash.empty())
      {
        continue;
      }
      std::string at;
      if (!std::getline(ls, at, '\t'))
      {
        continue;
      }
      std::getline(ls, commit.subject);
      commit.epoch = at.empty() ? 0 : std::strtoll(at.c_str(), nullptr, 10);
      commits.push_back(std::move(commit));
    }
    return commits;
  }

  // A plural age with its unit: "2 wks" and not "2 wks ago" -- the column is
  // narrow, so git's own %ar wording does not fit.
  inline std::string age_unit(long long count, const char *singular, const char *plural)
  {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
  }

  // The compact age a timeline row shows: "now", "5m", "3h", "2d", "2 wks",
  // "3 mos", "4 yrs".
  inline std::string relative_age(long long epoch, long long now_epoch)
  {
    if (epoch <= 0)
    {
      return "";
    }
    long long seconds = now_epoch - epoch;
    if (seconds < 0)
    {
      seconds = 0;
    }
    if (seconds < 60)
    {
      return "now";
    }
    const long long minutes = seconds / 60;
    if (minutes < 60)
    {
      return std::to_string(minutes) + "m";
    }
    const long long hours = minutes / 60;
    if (hours < 24)
    {
      return std::to_string(hours) + "h";
    }
    const long long days = hours / 24;
    if (days < 7)
    {
      return std::to_string(days) + "d";
    }
    if (days < 30)
    {
      return age_unit(days / 7, "wk", "wks");
    }
    if (days < 365)
    {
      return age_unit(days / 30, "mo", "mos");
    }
    return age_unit(days / 365, "yr", "yrs");
  }
} // namespace jot_timeline

#endif // JOT_TIMELINE_H
