#ifndef COMPLETION_RANK_H
#define COMPLETION_RANK_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Ranking the completion popup by what this user actually picks and by what the
// file being edited is already about.
//
// The editor cannot learn from a corpus the way IntelliCode does, but the two
// signals that make a suggestion feel smart are local and cheap: the names the
// user accepts often (a habit, kept per language and persisted) and the names
// already written near the caret (the context of the edit). Both fold into one
// number here, in points the popup adds to its own score.
//
// Everything in this namespace is pure: text in, numbers out, no buffer and no
// server, so a case can rank a list without an editor. The load and save at the
// bottom are the only I/O, and they take their path as an argument.
namespace CompletionRank
{
  // The half-life the editor uses when it is left to choose: two weeks. A name
  // reached for today still leads tomorrow, and one from last month does not.
  constexpr std::int64_t kDefaultHalfLifeSeconds = 14 * 24 * 60 * 60;

  // How much each name has been accepted, counted per language so a Python habit
  // ranks nothing in C++. Names are folded to lower case: the popup matches a
  // typed word case-insensitively, so learning has to as well.
  //
  // A count fades. What the user reached for every day last month should not
  // still outrank what they are reaching for now, so every use is stamped and a
  // count is halved for each half-life that passes (`decay`). The stamp is the
  // moment the count was last brought up to date rather than the moment of the
  // use, which is what makes a run of decays equal to one decay of the whole age:
  // the count keeps its fraction, so no time is lost rounding it down.
  class Usage
  {
  public:
    // How fast a count fades. A half-life of 0 or less is a table that never
    // forgets, which is also the setting that leaves it untouched.
    void set_half_life(std::int64_t half_life_seconds);
    std::int64_t half_life_seconds() const;
    // One acceptance at `now_seconds` (unix time). The entry is brought up to
    // date first, so the use counts from the moment it happened and not from
    // whenever the table was last read. The table is capped per language, and a
    // name never seen before is the one dropped when it is full: the user's real
    // habits are what the cap is protecting.
    void record(const std::string &language, const std::string &name, std::int64_t now_seconds);
    // The decayed count of one name: a habit worth 0.5 is half a use.
    double count(const std::string &language, const std::string &name) const;
    // The points the popup adds for this name being a habit of the user's.
    int points(const std::string &language, const std::string &name) const;
    // Brings every entry up to `now_seconds`: each count is halved for every
    // half-life that has passed, and a name worn down below half a use is
    // forgotten. An entry with no stamp -- a file written before stamps existed
    // -- is taken as fresh, because its age is unknown and guessing one would
    // throw a count the user earned away.
    void decay(std::int64_t now_seconds);
    bool empty() const;
    void clear();
    // Accepted names held, across every language.
    std::size_t entries() const;
    // One tab-separated line per name (count, then the stamp), in key order, so
    // the file is readable and two saves of the same table are byte-identical.
    std::string serialize() const;
    // A line the parser cannot read is skipped whole: a file edited by hand, or
    // truncated by a crash, loses that line and not the table. A line with no
    // stamp is the older format and is read as an unstamped count.
    static Usage parse(const std::string &text);

  private:
    // A count and the moment it was last brought up to date. The stamp is 0 for
    // an entry the file never dated.
    struct Entry
    {
      double count = 0.0;
      std::int64_t stamp = 0;
    };
    std::map<std::string, std::map<std::string, Entry>> counts_;
    std::int64_t half_life_seconds_ = kDefaultHalfLifeSeconds;
  };

  // The words already in the buffer, and how near the caret each one is. A word
  // in the same file is likely the one being typed again, and the nearer it is
  // the likelier still: a local beats a name used once in a distant function.
  class Context
  {
  public:
    // Reads a window of lines either side of the caret, so the scan stays bounded
    // on a huge file. One-character words are ignored: `i` and `j` are loop
    // noise, not names worth ranking.
    void rebuild(const std::vector<std::string> &lines, int caret_line);
    int points(const std::string &name) const;
    bool empty() const;
    void clear();

  private:
    // Word -> |line - caret line| of its nearest occurrence.
    std::map<std::string, int> nearest_gap_;
  };

  // The popup's ranking term for one row: the habit points plus the context
  // points. Zero when the row is neither, which is also the fast path -- the
  // ranker asks this of every row on every frame.
  int relevance(const Usage &usage,
                const Context &context,
                const std::string &language,
                const std::string &name);

  // The two halves separately, so a case can pin them: 30 points per acceptance
  // up to 300, and 90 points for a word anywhere in the window up to 150 for one
  // on the caret's own line. A decayed count is rounded down, so fading can never
  // pay back more than it took.
  int usage_points_for_count(double count);
  int locality_points_for_gap(int gap_lines);

  // Wall-clock seconds since the epoch, the unit the stamps are in. One function
  // so the editor and the tests read the same clock.
  std::int64_t now_seconds();

  // `<config home>/configs/completion_usage.tsv`, or "" when no home is known
  // (the editor then learns nothing rather than guessing a path).
  std::string usage_file_path();
  // Both return false instead of throwing: a missing or unwritable file is not an
  // error the editor has a use for.
  bool load_usage(const std::string &path, Usage &out);
  bool save_usage(const std::string &path, const Usage &usage);
} // namespace CompletionRank

#endif
