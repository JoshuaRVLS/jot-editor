// Ranking the completion popup by the user's own habits and by the file's own
// words (see completion_rank.h). Every number the ranking is built from lives
// here, so the tests can pin the model instead of describing it.
#include "lsp/completion_rank.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
  // The alphabet a word is read with here: an identifier in every language the
  // popup completes for, which is all the context signal needs.
  bool is_word_char(char c)
  {
    const unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) != 0 || c == '_';
  }

  char fold_ascii(char c)
  {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }

  std::string fold(const std::string &s)
  {
    std::string out = s;
    for (char &c : out)
    {
      c = fold_ascii(c);
    }
    return out;
  }

  // 30 points per acceptance, and a habit stops paying past ten: the eleventh
  // acceptance of the same name says nothing new about the next one, and the cap
  // keeps a name accepted a thousand times from burying every other row.
  constexpr int kPointsPerUse = 30;
  constexpr int kUsesThatCount = 10;
  // A word anywhere in the window is worth 90 points, one on the caret's own line
  // 150, so being in the file already is the stronger signal and being right here
  // the strongest. The gap is measured in lines.
  constexpr int kLocalPoints = 90;
  constexpr int kLocalPointsRange = 60;
  constexpr int kPointsPerLineAway = 4;
  // Held per language, and a window of lines either side of the caret, generous
  // enough to cover the function being edited in a large file.
  constexpr int kMaxEntriesPerLanguage = 400;
  constexpr int kScanHalfWindow = 2000;
  constexpr std::size_t kMaxWords = 2000;
  constexpr std::size_t kMinWordLength = 2;
  // A name worn down below half a use has stopped being a habit, so it is
  // dropped rather than carried at a fraction that can only keep shrinking: half
  // a use is 15 points against the 90 a word of the file itself is worth.
  constexpr double kForgottenBelow = 0.5;
  // Enough decimal places to keep a decayed count from rounding itself away over
  // a run of sessions, and few enough to read.
  constexpr int kCountDecimals = 3;

  // A count written the way it reads: a whole number as an integer, a decayed one
  // with its fraction. Two saves of the same table have to come out byte-identical
  // for the file to be usable by hand, so the formatting is fixed, not `%g`.
  std::string format_count(double count)
  {
    const double whole = std::floor(count);
    if (std::fabs(count - whole) < 0.0005)
    {
      return std::to_string((long long)whole);
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(kCountDecimals) << count;
    std::string text = out.str();
    while (!text.empty() && text.back() == '0')
    {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.')
    {
      text.pop_back();
    }
    return text;
  }

  // One count, brought up to `now`: halved for each half-life since it was last
  // brought up to date. The multiplier is fractional, so the remainders of a run
  // of decays add up to the decay of the whole age instead of being rounded away
  // one session at a time.
  double decayed_count(double count,
                       std::int64_t stamp,
                       std::int64_t now_seconds,
                       std::int64_t half_life_seconds)
  {
    if (count <= 0.0 || half_life_seconds <= 0 || stamp <= 0 || now_seconds <= stamp)
    {
      return count;
    }
    const double halves = (double)(now_seconds - stamp) / (double)half_life_seconds;
    const double decayed = count * std::pow(2.0, -halves);
    return decayed < kForgottenBelow ? 0.0 : decayed;
  }

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
} // namespace

namespace CompletionRank
{
  void Usage::set_half_life(std::int64_t half_life_seconds)
  {
    half_life_seconds_ = half_life_seconds;
  }

  std::int64_t Usage::half_life_seconds() const
  {
    return half_life_seconds_;
  }

  void Usage::record(const std::string &language, const std::string &name, std::int64_t now_seconds)
  {
    const std::string lang = fold(language);
    const std::string key = fold(name);
    if (key.empty())
    {
      return;
    }
    auto &table = counts_[lang];
    auto it = table.find(key);
    if (it == table.end() && (int)table.size() >= kMaxEntriesPerLanguage)
    {
      // Full: a new name only makes room for itself when the least-used entry is
      // as weak as it is -- picked once. Evicting a real habit to admit a first
      // sighting would trade the signal away for nothing.
      auto smallest = table.begin();
      for (auto candidate = table.begin(); candidate != table.end(); ++candidate)
      {
        if (candidate->second.count < smallest->second.count)
        {
          smallest = candidate;
        }
      }
      if (smallest->second.count > 1.0)
      {
        return;
      }
      table.erase(smallest);
    }
    // The count is brought up to date before the use is added, so what was earned
    // before the gap fades by the gap and the use itself does not.
    Entry &entry = table[key];
    entry.count = decayed_count(entry.count, entry.stamp, now_seconds, half_life_seconds_) + 1.0;
    entry.stamp = now_seconds;
  }

  double Usage::count(const std::string &language, const std::string &name) const
  {
    auto table = counts_.find(fold(language));
    if (table == counts_.end())
    {
      return 0.0;
    }
    auto it = table->second.find(fold(name));
    return it == table->second.end() ? 0.0 : it->second.count;
  }

  void Usage::decay(std::int64_t now_seconds)
  {
    for (auto &table : counts_)
    {
      for (auto it = table.second.begin(); it != table.second.end();)
      {
        Entry &entry = it->second;
        entry.count = decayed_count(entry.count, entry.stamp, now_seconds, half_life_seconds_);
        // An entry the file never dated is stamped now: from here on its age is
        // known, and the count it already carries is kept.
        entry.stamp = entry.count <= 0.0 ? 0 : now_seconds;
        if (entry.count <= 0.0)
        {
          it = table.second.erase(it);
        }
        else
        {
          ++it;
        }
      }
    }
  }

  int Usage::points(const std::string &language, const std::string &name) const
  {
    return usage_points_for_count(count(language, name));
  }

  bool Usage::empty() const
  {
    return counts_.empty();
  }

  void Usage::clear()
  {
    counts_.clear();
  }

  std::size_t Usage::entries() const
  {
    std::size_t total = 0;
    for (const auto &table : counts_)
    {
      total += table.second.size();
    }
    return total;
  }

  std::string Usage::serialize() const
  {
    std::ostringstream out;
    for (const auto &table : counts_)
    {
      for (const auto &entry : table.second)
      {
        if (entry.second.count <= 0.0)
        {
          continue;
        }
        out << table.first << '\t' << entry.first << '\t' << format_count(entry.second.count)
            << '\t' << entry.second.stamp << '\n';
      }
    }
    return out.str();
  }

  Usage Usage::parse(const std::string &text)
  {
    Usage usage;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      const std::size_t first = line.find('\t');
      if (first == std::string::npos)
      {
        continue;
      }
      const std::size_t second = line.find('\t', first + 1);
      if (second == std::string::npos)
      {
        continue;
      }
      const std::string language = line.substr(0, first);
      const std::string name = line.substr(first + 1, second - first - 1);
      const std::string rest = line.substr(second + 1);
      // The stamp is the newer half of the format: a line without one is a count
      // from before it existed, and reads as undated rather than as malformed.
      const std::size_t third = rest.find('\t');
      const std::string count_text = third == std::string::npos ? rest : rest.substr(0, third);
      const std::string stamp_text = third == std::string::npos ? "" : rest.substr(third + 1);
      if (language.empty() || name.empty() || count_text.empty())
      {
        continue;
      }
      char *end = nullptr;
      const double count = std::strtod(count_text.c_str(), &end);
      if (end == count_text.c_str() || *end != '\0' || !(count > 0.0) || !std::isfinite(count))
      {
        continue;
      }
      std::int64_t stamp = 0;
      if (!stamp_text.empty())
      {
        char *stamp_end = nullptr;
        const long long parsed_stamp = std::strtoll(stamp_text.c_str(), &stamp_end, 10);
        if (stamp_end != stamp_text.c_str() && *stamp_end == '\0' && parsed_stamp > 0)
        {
          stamp = (std::int64_t)parsed_stamp;
        }
      }
      Entry entry;
      entry.count = std::min(count, 1000000.0);
      entry.stamp = stamp;
      usage.counts_[fold(language)][fold(name)] = entry;
    }
    return usage;
  }

  void Context::rebuild(const std::vector<std::string> &lines, int caret_line)
  {
    nearest_gap_.clear();
    if (lines.empty())
    {
      return;
    }
    const int caret = std::clamp(caret_line, 0, (int)lines.size() - 1);
    const int first = std::max(0, caret - kScanHalfWindow);
    const int last = std::min((int)lines.size(), caret + kScanHalfWindow + 1);
    for (int index = first; index < last; index++)
    {
      const std::string &line = lines[(std::size_t)index];
      const int gap = std::abs(index - caret);
      std::size_t i = 0;
      while (i < line.size())
      {
        if (!is_word_char(line[i]))
        {
          i++;
          continue;
        }
        const std::size_t start = i;
        while (i < line.size() && is_word_char(line[i]))
        {
          i++;
        }
        if (i - start < kMinWordLength)
        {
          continue;
        }
        const std::string word = fold(line.substr(start, i - start));
        auto it = nearest_gap_.find(word);
        if (it != nearest_gap_.end())
        {
          it->second = std::min(it->second, gap);
        }
        else if (nearest_gap_.size() < kMaxWords)
        {
          nearest_gap_.emplace(word, gap);
        }
      }
    }
  }

  int Context::points(const std::string &name) const
  {
    auto it = nearest_gap_.find(fold(name));
    return it == nearest_gap_.end() ? 0 : locality_points_for_gap(it->second);
  }

  bool Context::empty() const
  {
    return nearest_gap_.empty();
  }

  void Context::clear()
  {
    nearest_gap_.clear();
  }

  int relevance(const Usage &usage,
                const Context &context,
                const std::string &language,
                const std::string &name)
  {
    if (name.empty())
    {
      return 0;
    }
    return usage.points(language, name) + context.points(name);
  }

  int usage_points_for_count(double count)
  {
    if (!(count > 0.0))
    {
      return 0;
    }
    // Rounded down: a faded count must never be worth more than it was.
    return (int)std::floor((double)kPointsPerUse * std::min(count, (double)kUsesThatCount));
  }

  int locality_points_for_gap(int gap_lines)
  {
    if (gap_lines < 0)
    {
      return 0;
    }
    const int nearer = std::max(0, kLocalPointsRange - kPointsPerLineAway * gap_lines);
    return kLocalPoints + nearer;
  }

  std::int64_t now_seconds()
  {
    return (std::int64_t)std::time(nullptr);
  }

  std::string usage_file_path()
  {
    const fs::path root = config_home();
    if (root.empty())
    {
      return "";
    }
    return (root / "configs" / "completion_usage.tsv").string();
  }

  bool load_usage(const std::string &path, Usage &out)
  {
    // The file holds counts and their stamps, never the rate they fade at: that
    // is a setting, so a load keeps whatever the caller put there rather than
    // resetting it to the default.
    const std::int64_t half_life = out.half_life_seconds();
    out.clear();
    out.set_half_life(half_life);
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
    Usage parsed = Usage::parse(text.str());
    parsed.set_half_life(half_life);
    out = std::move(parsed);
    return true;
  }

  bool save_usage(const std::string &path, const Usage &usage)
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
    file << usage.serialize();
    return true;
  }
} // namespace CompletionRank
