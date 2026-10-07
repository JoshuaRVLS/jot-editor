#ifndef JOT_FEATURES_CODING_TIME_H
#define JOT_FEATURES_CODING_TIME_H

// How much time was coded here, kept locally (the Extensions settings category
// holds the switch that decides whether the statusline shows WakaTime's own
// total instead -- extras/wakatime.h is that other source).
//
// This is the fallback that makes the number worth showing with the integration
// off: the totals are stored per workspace and day, so opening the editor again
// later today continues the total instead of restarting a timer nobody reads.
//
// What counts is the rule every coding-time tracker uses, and it is the
// difference between a useful number and a broken one: time is credited between
// two activity ticks, and any gap longer than the cap is dropped, so an editor
// left open overnight does not log eight hours of nothing.
//
// Split so both halves are testable without a clock: `credit_for` is the rule,
// and `Counter` is the rule plus its two readings (the committed total and the
// moment the last credit was taken).
#include <map>
#include <string>

namespace coding_time
{
  // The longest gap between two activity ticks that still counts as coding: the
  // same two minutes WakaTime's own heartbeat limit uses.
  constexpr long long kIdleCapMs = 120000;

  // The milliseconds worth crediting at `now_ms`, given when the user was last
  // seen and when the total was last brought up to date. Zero once the editor
  // has been idle past the cap, and never more than the cap, so a long absence
  // cannot be charged as work.
  long long credit_for(long long now_ms, long long last_activity_ms, long long last_credit_ms);

  // The running total for one place, which is one workspace on one day.
  struct Counter
  {
    long long total_ms = 0;
    long long last_activity_ms = 0;
    long long last_credit_ms = 0;

    // Folds the time up to `now_ms` into the total and returns it. This is the
    // frame loop's once-a-second call, which is also what keeps the label
    // moving; the caller decides when the result is written out.
    long long advance(long long now_ms);
    // A tick that says the user is here: a keystroke, a save, a file switch.
    void mark_activity(long long now_ms);
    // The total as of `now_ms`, without committing anything: what the bar
    // paints, and what a flush writes.
    long long live_ms(long long now_ms) const;
  };

  // Every stored total, keyed by `<workspace>\t<date>`. Flat rather than nested
  // because the file is then one line per place, in order.
  using Totals = std::map<std::string, long long>;

  std::string key_for(const std::string &workspace, const std::string &date);
  // The workspace part of a key, for a caller that wants to report where a
  // total came from.
  std::string workspace_of(const std::string &key);
  // The local date as YYYY-MM-DD, which is what "today" means to the user
  // looking at the chip.
  std::string local_date(long long epoch_seconds);
  long long total_for(const Totals &totals, const std::string &key);
  // Stores a total, dropping the entry when it falls to nothing rather than
  // keeping empty rows around forever.
  void set_total(Totals &totals, const std::string &key, long long ms);
  std::string serialize(const Totals &totals);
  Totals parse(const std::string &text);

  // The store's own file, and the two calls that move it: the same shape as the
  // completion table next to it (lsp/completion_rank.h).
  std::string store_path();
  bool load_totals(const std::string &path, Totals &out);
  bool save_totals(const std::string &path, const Totals &totals);
} // namespace coding_time

#endif // JOT_FEATURES_CODING_TIME_H
