// The local coding-time store (see extras/coding_time/probe.h for the rule and why
// the store exists at all).
#include "extras/coding_time/probe.h"
#include "base/string_util.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace coding_time
{
  namespace
  {
    // The config home, resolved the way the rest of the app does it
    // (workspace/config.cpp owns the same precedence): an override for tests,
    // then the platform's own directory.
    fs::path config_home()
    {
      const char *override_home = std::getenv("JOT_CONFIG_HOME");
      if (override_home && *override_home)
      {
        return fs::path(override_home);
      }
#ifdef _WIN32
      const char *app_data = std::getenv("APPDATA");
      if (app_data && *app_data)
      {
        return fs::path(app_data) / "jot";
      }
      const char *profile = std::getenv("USERPROFILE");
      if (profile && *profile)
      {
        return fs::path(profile) / ".config" / "jot";
      }
#else
      const char *home = std::getenv("HOME");
      if (home && *home)
      {
        return fs::path(home) / ".config" / "jot";
      }
#endif
      return {};
    }

    // One directory, one spelling. The workspace arrives as whatever the editor
    // holds at the time: an absolute path from the explorer, or the "." a
    // session with no folder open keeps, and either can carry a trailing
    // separator. Two spellings of one place have to land on one key -- otherwise
    // a project's time is split between them and neither line is the total the
    // chip reports. A relative spelling is resolved here, against the directory
    // the editor was started in, which is the one it was written from.
    std::string canonical_workspace(const std::string &workspace)
    {
      if (workspace.empty())
      {
        return workspace;
      }
      std::error_code ec;
      fs::path path = fs::absolute(fs::path(workspace), ec);
      if (ec)
      {
        path = fs::path(workspace);
      }
      path = path.lexically_normal();
      std::string out = path.string();
      // lexically_normal() keeps a trailing separator on "a/b/", and the root
      // keeps its own ("C:\\"): drop separators down to the root and no further.
      const std::string root = path.root_path().string();
      while (out.size() > root.size() && (out.back() == '/' || out.back() == '\\'))
      {
        out.pop_back();
      }
      return out;
    }
  } // namespace

  long long credit_for(long long now_ms, long long last_activity_ms, long long last_credit_ms)
  {
    // Nothing since the last look, or a clock that went backwards.
    if (now_ms <= last_credit_ms)
    {
      return 0;
    }
    // Past the cap the user has walked away: whatever the gap is, none of it
    // was work, and the cap is what keeps a machine left on overnight from
    // being charged a night of coding.
    if (now_ms - last_activity_ms > kIdleCapMs)
    {
      return 0;
    }
    return std::min(now_ms - last_credit_ms, kIdleCapMs);
  }

  long long Counter::advance(long long now_ms)
  {
    total_ms += credit_for(now_ms, last_activity_ms, last_credit_ms);
    last_credit_ms = now_ms;
    return total_ms;
  }

  void Counter::mark_activity(long long now_ms)
  {
    // Fold first: the stretch since the last look belonged to the user, so it
    // is credited before the window is restarted from here.
    advance(now_ms);
    last_activity_ms = now_ms;
  }

  long long Counter::live_ms(long long now_ms) const
  {
    return total_ms + credit_for(now_ms, last_activity_ms, last_credit_ms);
  }

  std::string key_for(const std::string &workspace, const std::string &date)
  {
    // Escaped, so a workspace path with a tab in it cannot split its own key.
    return string_util::escape_field(canonical_workspace(workspace)) + "\t" + date;
  }

  std::string workspace_of(const std::string &key)
  {
    const size_t tab = key.find('\t');
    return string_util::unescape_field(key.substr(0, tab));
  }

  std::string local_date(long long epoch_seconds)
  {
    const std::time_t wall = (std::time_t)epoch_seconds;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &wall);
#else
    localtime_r(&wall, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
  }

  long long total_for(const Totals &totals, const std::string &key)
  {
    const auto it = totals.find(key);
    return it == totals.end() ? 0 : it->second;
  }

  void set_total(Totals &totals, const std::string &key, long long ms)
  {
    if (ms <= 0)
    {
      totals.erase(key);
      return;
    }
    totals[key] = ms;
  }

  std::string serialize(const Totals &totals)
  {
    std::string out;
    for (const auto &entry : totals)
    {
      if (entry.second <= 0)
      {
        continue;
      }
      out += entry.first; // already `<workspace>\t<date>`
      out += '\t';
      out += std::to_string(entry.second);
      out += '\n';
    }
    return out;
  }

  Totals parse(const std::string &text)
  {
    Totals totals;
    for (std::string_view line : string_util::lines(text))
    {
      // Three fields, the key's own tab included: workspace, date, total. A
      // line anything else is dropped whole, so a hand edit or a truncated
      // write costs the rest of the file nothing.
      const auto parts = string_util::split(line, '\t');
      if (parts.size() != 3)
      {
        continue;
      }
      const std::string date = std::string(string_util::trim_copy(parts[1]));
      const std::string_view ms_text = string_util::trim_view(parts[2]);
      bool numeric = !ms_text.empty();
      for (char c : ms_text)
      {
        if (c < '0' || c > '9')
        {
          numeric = false;
          break;
        }
      }
      if (!numeric)
      {
        continue;
      }
      const long long ms = std::strtoll(std::string(ms_text).c_str(), nullptr, 10);
      if (ms <= 0)
      {
        continue; // a row of nothing adds nothing to the total
      }
      // The key is put back together from the two fields rather than trusting
      // the line's own spacing, so a stray blank in a hand edit is not a second
      // entry for the same place. Rows that fold onto one key -- two spellings
      // of one directory, or a hand edit that added up a day -- are summed
      // rather than the later one replacing the earlier.
      totals[key_for(string_util::unescape_field(parts[0]), date)] += ms;
    }
    return totals;
  }

  std::string store_path()
  {
    const fs::path root = config_home();
    if (root.empty())
    {
      return "";
    }
    return (root / "configs" / "coding_time.tsv").string();
  }

  bool load_totals(const std::string &path, Totals &out)
  {
    out.clear();
    if (path.empty())
    {
      return false;
    }
    std::ifstream file(path);
    if (!file.is_open())
    {
      return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    out = parse(text.str());
    return true;
  }

  bool save_totals(const std::string &path, const Totals &totals)
  {
    if (path.empty())
    {
      return false;
    }
    std::error_code ec;
    const fs::path output_path(path);
    fs::create_directories(output_path.parent_path(), ec);
    std::ofstream file(path);
    if (!file.is_open())
    {
      return false;
    }
    file << serialize(totals);
    return true;
  }
} // namespace coding_time
