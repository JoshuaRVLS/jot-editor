// The popup's learned ranking (features/completion_rank.h): the names this user
// accepts, counted per language and kept in the config home, plus the words the
// file being edited already holds.
//
// The engine is pure, so the model is pinned directly here (the points, the
// window, the file format), and the last cases drive it through the editor the
// way a user does: a table seeded on disk, an item accepted, a list re-ranked.
#include "core/editor.h"
#include "lsp/completion_rank.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using namespace CompletionRank;

namespace
{
  // A fixed point on the clock, so a case can age a count by weeks without
  // waiting: the engine takes the time it is given.
  constexpr std::int64_t kNow = 1750000000;
  constexpr std::int64_t kHalfLife = 7 * 24 * 60 * 60;

  std::string seed_config_home()
  {
    char home[] = "/tmp/jot_completion_rank_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
    return home;
  }

  std::string usage_file_in(const std::string &home)
  {
    return (fs::path(home) / "configs" / "completion_usage.tsv").string();
  }

  void write_usage_file(const std::string &home, const std::string &text)
  {
    const fs::path dir = fs::path(home) / "configs";
    fs::create_directories(dir);
    std::ofstream out(usage_file_in(home));
    out << text;
    out.close();
  }

  std::string read_file(const std::string &path)
  {
    std::ifstream in(path);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
  }

  // A function row, the shape the ranking is about. No filterText, like most
  // clangd items: the typed word is matched against the label.
  LSPCompletionItem function_item(const std::string &label)
  {
    LSPCompletionItem it;
    it.label = label;
    it.insert_text = label;
    it.kind = 3;
    return it;
  }

  std::string open_with_caret(Editor &e, const std::string &text, int line, int col)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_completion_rank_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    e.apply_resize_for_test(110, 30);
    e.scroll_cursor_to_for_test(line, col);
    return path;
  }
} // namespace

TEST_CASE("Completion rank: the learned table round-trips through its file format",
          "[jot][completion-rank]")
{
  Usage usage;
  usage.record("cpp", "std::string", kNow);
  usage.record("Cpp", "std::String", kNow); // folded: one language, one name
  usage.record("python", "len", kNow);
  REQUIRE(usage.count("cpp", "std::string") == 2);
  REQUIRE(usage.count("python", "len") == 1);
  REQUIRE(usage.count("cpp", "len") == 0); // the language is part of the key
  REQUIRE(usage.entries() == 2);

  const Usage parsed = Usage::parse(usage.serialize());
  REQUIRE(parsed.count("cpp", "std::string") == 2);
  REQUIRE(parsed.count("python", "len") == 1);
  REQUIRE(parsed.serialize() == usage.serialize());

  // The count is written with the moment it was last brought up to date, and a
  // decayed count keeps its fraction in the file.
  Usage decayed;
  decayed.set_half_life(kHalfLife);
  for (int i = 0; i < 5; i++)
  {
    decayed.record("cpp", "aging", kNow);
  }
  decayed.decay(kNow + kHalfLife);
  const Usage reread = Usage::parse(decayed.serialize());
  REQUIRE(reread.count("cpp", "aging") == 2.5);
  REQUIRE(reread.serialize() == decayed.serialize());

  // A line the parser cannot read is dropped whole: the rest of the table, and
  // the file it came from, survive a hand edit or a truncated write. A line with
  // no stamp is the older format, read as an undated count rather than as one to
  // throw away.
  const Usage ragged = Usage::parse(
      "cpp\tgood\t3\nnonsense\ncpp\tzero\t0\ncpp\tbad\tx\n\ncpp\taged\t4\t1700000000\n");
  REQUIRE(ragged.count("cpp", "good") == 3);
  REQUIRE(ragged.count("cpp", "zero") == 0);
  REQUIRE(ragged.count("cpp", "bad") == 0);
  REQUIRE(ragged.count("cpp", "aged") == 4);
  REQUIRE(ragged.entries() == 2);
}

TEST_CASE("Completion rank: a habit is worth points and a long habit stops paying",
          "[jot][completion-rank]")
{
  REQUIRE(usage_points_for_count(0) == 0);
  REQUIRE(usage_points_for_count(1) == 30);
  REQUIRE(usage_points_for_count(5) == 150);
  REQUIRE(usage_points_for_count(10) == 300);
  // Past ten acceptances the eleventh says nothing new, and the cap is what keeps
  // one habit from burying every other row.
  REQUIRE(usage_points_for_count(11) == 300);
  REQUIRE(usage_points_for_count(500) == 300);
  // A faded count is worth its fraction, rounded down: 1.25 uses is 37 points,
  // and fading can never pay back more than it took.
  REQUIRE(usage_points_for_count(0.5) == 15);
  REQUIRE(usage_points_for_count(1.25) == 37);

  Usage usage;
  for (int i = 0; i < 5; i++)
  {
    usage.record("cpp", "quicksilver", kNow);
  }
  REQUIRE(usage.points("cpp", "quicksilver") == 150);
  REQUIRE(usage.points("cpp", "never_seen") == 0);
  REQUIRE(usage.points("html", "quicksilver") == 0); // another language, no habit
}

TEST_CASE("Completion rank: the file's own words score by how near they are",
          "[jot][completion-rank]")
{
  // A word on the caret's line is the strongest context, and every line of
  // distance costs a little until the floor: in the file at all still counts.
  REQUIRE(locality_points_for_gap(0) == 150);
  REQUIRE(locality_points_for_gap(1) == 146);
  REQUIRE(locality_points_for_gap(15) == 90);
  REQUIRE(locality_points_for_gap(400) == 90);

  const std::vector<std::string> lines = {"alpha_count", "beta_count", "  gam", "delta_count",
                                          "x"};
  Context context;
  context.rebuild(lines, 2);
  REQUIRE(context.points("gam") == 150);        // the caret's own line
  REQUIRE(context.points("beta_count") == 146); // one line away
  REQUIRE(context.points("alpha_count") == 142);
  REQUIRE(context.points("delta_count") == 146);
  REQUIRE(context.points("x") == 0);        // one-character words are loop noise
  REQUIRE(context.points("nowhere") == 0);  // not in the file at all
  REQUIRE(context.points("GAM") == 150);    // folded, like the popup's matching
}

TEST_CASE("Completion rank: the context reads a window, not the whole file",
          "[jot][completion-rank]")
{
  std::vector<std::string> lines(5000, "filler");
  lines[0] = "faraway_name";
  lines[4999] = "  near";

  Context context;
  context.rebuild(lines, 4999);
  REQUIRE(context.points("near") == 150);
  // Five thousand lines away is outside the window, so it is not a word of this
  // edit even though it is a word of this file.
  REQUIRE(context.points("faraway_name") == 0);
}

TEST_CASE("Completion rank: relevance adds the habit to the file's own words",
          "[jot][completion-rank]")
{
  Usage usage;
  for (int i = 0; i < 5; i++)
  {
    usage.record("cpp", "total_count", kNow);
  }
  Context context;
  context.rebuild({"int total_count = 0;", "  ret"}, 1);

  REQUIRE(relevance(usage, context, "cpp", "total_count") == 150 + 146);
  REQUIRE(relevance(usage, context, "cpp", "ret") == 150);
  REQUIRE(relevance(usage, context, "cpp", "stranger") == 0);

  // Learning nothing leaves every row where it was.
  Usage empty_usage;
  Context empty_context;
  REQUIRE(relevance(empty_usage, empty_context, "cpp", "total_count") == 0);
}

TEST_CASE("Completion rank: the table lives under the config home",
          "[jot][completion-rank]")
{
  const std::string home = seed_config_home();
  REQUIRE(usage_file_path() == usage_file_in(home));

  Usage usage;
  usage.record("cpp", "std::string", kNow);
  usage.record("cpp", "std::string", kNow);
  REQUIRE(save_usage(usage_file_path(), usage));
  // The file holds counts and stamps, never the rate they fade at: a table told
  // to forget slowly is not quietly reset to the default by the read.
  Usage loaded;
  loaded.set_half_life(0);
  REQUIRE(load_usage(usage_file_path(), loaded));
  REQUIRE(loaded.count("cpp", "std::string") == 2);
  REQUIRE(loaded.half_life_seconds() == 0);

  // A file that is not there is an empty table too, and still not a new rate.
  Usage absent;
  absent.set_half_life(kHalfLife);
  REQUIRE_FALSE(load_usage(usage_file_in(home) + ".missing", absent));
  REQUIRE(absent.empty());
  REQUIRE(absent.half_life_seconds() == kHalfLife);
}

TEST_CASE("Completion rank: a count halves for every half life that passes",
          "[jot][completion-rank]")
{
  Usage usage;
  usage.set_half_life(kHalfLife);
  REQUIRE(usage.half_life_seconds() == kHalfLife);
  for (int i = 0; i < 8; i++)
  {
    usage.record("cpp", "name", kNow);
  }
  REQUIRE(usage.count("cpp", "name") == 8.0);

  // Read twice in the same second: nothing has passed, nothing fades.
  usage.decay(kNow);
  REQUIRE(usage.count("cpp", "name") == 8.0);

  usage.decay(kNow + kHalfLife);
  REQUIRE(usage.count("cpp", "name") == 4.0);
  usage.decay(kNow + 3 * kHalfLife);
  REQUIRE(usage.count("cpp", "name") == 1.0);

  // The language is part of the key, and a name nothing was recorded for is not
  // invented by the decay.
  REQUIRE(usage.points("cpp", "name") == 30);
  REQUIRE(usage.points("cpp", "never_seen") == 0);
}

TEST_CASE("Completion rank: a run of decays adds up to the decay of the whole age",
          "[jot][completion-rank]")
{
  // The count keeps its fraction and the stamp moves with it, so being read once a
  // session does not round the same remainder away again and again: a fortnight
  // spent in fourteen pieces has to leave what a fortnight left in one.
  Usage stepped;
  Usage whole;
  stepped.set_half_life(kHalfLife);
  whole.set_half_life(kHalfLife);
  for (int i = 0; i < 8; i++)
  {
    stepped.record("cpp", "name", kNow);
    whole.record("cpp", "name", kNow);
  }
  for (int slice = 1; slice <= 14; slice++)
  {
    stepped.decay(kNow + kHalfLife * slice / 14);
  }
  whole.decay(kNow + kHalfLife);
  REQUIRE(std::fabs(stepped.count("cpp", "name") - whole.count("cpp", "name")) < 0.01);
  REQUIRE(std::fabs(stepped.count("cpp", "name") - 4.0) < 0.01);
}

TEST_CASE("Completion rank: a name worn below half a use is forgotten", "[jot][completion-rank]")
{
  Usage usage;
  usage.set_half_life(kHalfLife);
  usage.record("cpp", "rare", kNow);
  // Half a use is still a use; less than that is a name that has stopped being
  // one the user reaches for, so it is dropped and the file stops growing.
  usage.decay(kNow + kHalfLife);
  REQUIRE(std::fabs(usage.count("cpp", "rare") - 0.5) < 0.001);
  REQUIRE(usage.entries() == 1);
  usage.decay(kNow + kHalfLife + kHalfLife / 2);
  REQUIRE(usage.count("cpp", "rare") == 0.0);
  REQUIRE(usage.entries() == 0);
}

TEST_CASE("Completion rank: a use counts from the moment it happens", "[jot][completion-rank]")
{
  Usage usage;
  usage.set_half_life(kHalfLife);
  usage.record("cpp", "name", kNow);
  // The use a fortnight later is worth a whole one, and only what was earned
  // before the gap fades by it.
  usage.record("cpp", "name", kNow + kHalfLife);
  REQUIRE(usage.count("cpp", "name") == 1.5);
  REQUIRE(usage.points("cpp", "name") == 45);
}

TEST_CASE("Completion rank: a half-life of zero never forgets", "[jot][completion-rank]")
{
  Usage usage;
  usage.set_half_life(0);
  for (int i = 0; i < 3; i++)
  {
    usage.record("cpp", "name", kNow);
  }
  usage.decay(kNow + 100 * kHalfLife);
  REQUIRE(usage.count("cpp", "name") == 3.0);
  REQUIRE(usage.points("cpp", "name") == 90);
}

TEST_CASE("Completion rank: an undated count is kept and dated", "[jot][completion-rank]")
{
  // A table written before the counts carried a moment: its age is unknown, so the
  // first read dates it and keeps what the user earned, and it fades from there.
  Usage usage = Usage::parse("cpp\tlegacy\t4\n");
  usage.set_half_life(kHalfLife);
  REQUIRE(usage.count("cpp", "legacy") == 4.0);
  usage.decay(kNow + 5 * kHalfLife);
  REQUIRE(usage.count("cpp", "legacy") == 4.0);
  usage.decay(kNow + 6 * kHalfLife);
  REQUIRE(std::fabs(usage.count("cpp", "legacy") - 2.0) < 0.001);
  REQUIRE(usage.serialize().find("legacy\t2\t") != std::string::npos);
}

TEST_CASE("Completion rank: the accepted name outranks a stranger on the next list",
          "[jot][completion-rank]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);

  const std::vector<LSPCompletionItem> items = {function_item("add_entry"),
                                                function_item("addition")};
  // Nothing learned yet: the shorter label leads, as it always did.
  REQUIRE(e.seed_lsp_completion_for_test(items));
  REQUIRE(e.lsp_completion_labels_for_test() ==
          std::vector<std::string>({"addition", "add_entry"}));

  // Take the other row, the way the arrow keys plus Tab would.
  e.raw_key_for_test(1009); // Down
  REQUIRE(e.lsp_completion_selected_for_test() == 1);
  e.raw_key_for_test(9); // Tab
  REQUIRE(!e.lsp_completion_visible_for_test());
  REQUIRE(e.buffer_for_test().line(1) == "  add_entry");

  // The next list puts the name the user just reached for first. The caret goes
  // back inside the word the item wrote, since the list only holds what the typed
  // text leads into and the caret now sits at the end of the whole name.
  e.scroll_cursor_to_for_test(1, 5);
  REQUIRE(e.seed_lsp_completion_for_test(items));
  REQUIRE(e.lsp_completion_labels_for_test() ==
          std::vector<std::string>({"add_entry", "addition"}));
}

TEST_CASE("Completion rank: a table on disk re-ranks the popup", "[jot][completion-rank]")
{
  const std::vector<LSPCompletionItem> items = {function_item("quick_scan"),
                                                function_item("quicksilver")};

  // With no history, the shorter of the two leads.
  seed_config_home();
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
    REQUIRE(e.seed_lsp_completion_for_test(items));
    REQUIRE(e.lsp_completion_labels_for_test()[0] == "quick_scan");
  }

  // A habit stored under the config home outweighs the one character of length,
  // which is the whole point: what this user reaches for, not the shortest name.
  const std::string home = seed_config_home();
  write_usage_file(home, "cpp\tquicksilver\t5\n");
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
    REQUIRE(e.seed_lsp_completion_for_test(items));
    REQUIRE(e.lsp_completion_labels_for_test()[0] == "quicksilver");
  }
}

TEST_CASE("Completion rank: the word the file already uses leads", "[jot][completion-rank]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  // `total_count` is declared above the caret and `tot` is being typed: the name
  // the file is already about is the one being reached for again, however much
  // longer it is than the stranger beside it.
  open_with_caret(e, "int total_count = 0;\nint main() {\n  tot\n}\n", 2, 5);
  REQUIRE(e.seed_lsp_completion_for_test(
      {function_item("totalizer"), function_item("total_count")}));
  REQUIRE(e.lsp_completion_labels_for_test()[0] == "total_count");
}

TEST_CASE("Completion rank: a decorated label is still the same name",
          "[jot][completion-rank]")
{
  // clangd hangs a marker on the label it sends: a leading space, or the bullet
  // it puts on the symbols it would add an include for. The name is what the
  // table is keyed on, so a row has to be recognised through that decoration.
  auto decorated = [](const std::string &label, const std::string &name)
  {
    LSPCompletionItem it = function_item(name);
    it.label = label;
    it.filter_text = name;
    return it;
  };

  const std::string home = seed_config_home();
  write_usage_file(home, "cpp\tquicksilver\t5\n");
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
    REQUIRE(e.seed_lsp_completion_for_test(
        {decorated(" quick_scan", "quick_scan"), decorated("\u2022quicksilver", "quicksilver")}));
    REQUIRE(e.lsp_completion_labels_for_test()[0] == "\u2022quicksilver");

    // Accepting it counts the name, not the decoration, so the next one is
    // recognised too.
    e.raw_key_for_test(9); // Tab
  }
  const std::string written = read_file(usage_file_in(home));
  REQUIRE(written.find("cpp\tquicksilver\t6") != std::string::npos);
}

TEST_CASE("Completion rank: a stale habit loses to a fresh one", "[jot][completion-rank]")
{
  const std::string home = seed_config_home();
  const std::int64_t now = now_seconds();
  // Five uses of `quicksilver` a month ago against two of `quick_scan` today. The
  // old count is four times the new one, so without fading it wins.
  write_usage_file(home,
                   "cpp\tquicksilver\t5\t" + std::to_string(now - 28 * 86400)
                       + "\ncpp\tquick_scan\t2\t" + std::to_string(now) + "\n");

  const std::vector<LSPCompletionItem> items = {function_item("quick_scan"),
                                                function_item("quicksilver")};

  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
    REQUIRE(e.seed_lsp_completion_for_test(items));
    // Two half-lives of two weeks: the month-old habit is worn down to 1.25 uses
    // and the two from today lead.
    REQUIRE(e.lsp_completion_labels_for_test()[0] == "quick_scan");
  }
  // Nothing was accepted, so the table on disk is left as it is: that ranking was
  // a read of it, not a write over it.
  REQUIRE(read_file(usage_file_in(home)).find("quicksilver\t5\t") != std::string::npos);

  {
    Editor e;
    e.set_home_menu_visible(false);
    e.config_set_for_test("lsp_completion_learn_half_life_days", "0");
    open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
    REQUIRE(e.seed_lsp_completion_for_test(items));
    // The same table with fading off puts the old habit back on top, which is
    // what says the flip above came from the decay rather than from the file.
    REQUIRE(e.lsp_completion_labels_for_test()[0] == "quicksilver");
  }
}

TEST_CASE("Completion rank: accepting a row writes the table on shutdown",
          "[jot][completion-rank]")
{
  const std::string home = seed_config_home();
  {
    Editor e;
    e.set_home_menu_visible(false);
    open_with_caret(e, "int main() {\n  ad\n}\n", 1, 4);
    REQUIRE(e.seed_lsp_completion_for_test({function_item("add")}));
    e.raw_key_for_test(9); // Tab
    REQUIRE(e.buffer_for_test().line(1) == "  add");
  } // the editor shuts down here, which is where the table is written

  const std::string written = read_file(usage_file_in(home));
  REQUIRE(written.find("cpp\tadd\t1") != std::string::npos);
}

TEST_CASE("Completion rank: learning off leaves the list and the disk alone",
          "[jot][completion-rank]")
{
  const std::string home = seed_config_home();
  write_usage_file(home, "cpp\tquicksilver\t5\n");

  Editor e;
  e.set_home_menu_visible(false);
  e.config_set_for_test("lsp_completion_learn", "false");
  open_with_caret(e, "int main() {\n  qui\n}\n", 1, 5);
  REQUIRE(e.seed_lsp_completion_for_test(
      {function_item("quick_scan"), function_item("quicksilver")}));
  // The plain order, and the table is not even read.
  REQUIRE(e.lsp_completion_labels_for_test()[0] == "quick_scan");
}

TEST_CASE("Completion rank: the buffer is read once per word, not per keystroke",
          "[jot][completion-rank]")
{
  seed_config_home();
  Editor e;
  e.set_home_menu_visible(false);
  open_with_caret(e, "int total_count = 0;\nint main() {\n  tot\n}\n", 2, 5);

  // A response the typed word does not lead into leaves the popup off screen, so
  // each seed below is the request behind one keystroke with the server still to
  // answer, which is the window the read used to happen on every keystroke of the
  // word.
  const std::vector<LSPCompletionItem> other = {function_item("zzz")};
  const unsigned long long before = e.completion_context_rebuilds_for_test();
  REQUIRE(!e.seed_lsp_completion_for_test(other));
  const unsigned long long first = e.completion_context_rebuilds_for_test();
  REQUIRE(first == before + 1);

  // Another letter of the same word: the words around the caret did not move, so
  // the ranking reuses the read the word already has.
  e.insert_char_for_test('a');
  REQUIRE(!e.seed_lsp_completion_for_test(other));
  REQUIRE(e.completion_context_rebuilds_for_test() == first);

  // A word somewhere else is a new session and does read the buffer again.
  e.scroll_cursor_to_for_test(0, 5);
  REQUIRE(!e.seed_lsp_completion_for_test(other));
  REQUIRE(e.completion_context_rebuilds_for_test() == first + 1);
}
