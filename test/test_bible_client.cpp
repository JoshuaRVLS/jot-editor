// The editor side of the Bible reader (`:bible`, src/editor/app/bible_client.cpp).
//
// There is no server here: every case pre-seeds the on-disk cache for the URL
// the command will ask for, so a cache hit drives the whole editor path --
// reference parsing, buffer landing, re-use and the save refusal -- without a
// socket. The one live run (against a local fixture server, including
// `:bible random`, which cannot be pre-seeded) is test/bible_probe.py.
#include "core/editor.h"
#include "extras/bible.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace
{
  std::string g_cache_home;

  void seed_config_home()
  {
    char home[] = "/tmp/jot_bible_client_XXXXXX";
    mkdtemp(home);
    g_cache_home = home;
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  const char *kBase = "https://bible.helloao.org/api";

  void seed_cache(const std::string &url, const std::string &body)
  {
    const fs::path dir = fs::path(g_cache_home) / "bible";
    fs::create_directories(dir);
    std::ofstream out(dir / Bible::cache_file_name(url));
    out << body;
  }

  void seed_chapter(const std::string &book_id,
                    const std::string &book_name,
                    int chapter,
                    int first_verse)
  {
    std::ostringstream json;
    json << "{\"translation\":{\"id\":\"BSB\",\"englishName\":\"Berean Standard Bible\"},"
         << "\"book\":{\"id\":\"" << book_id << "\",\"commonName\":\"" << book_name
         << "\"},\"chapter\":{\"number\":" << chapter << ",\"content\":["
         << "{\"type\":\"heading\",\"text\":\"A Heading\"},"
         << "{\"type\":\"verse\",\"number\":" << first_verse
         << ",\"text\":\"The first verse of " << book_name << " " << chapter << ".\"},"
         << "{\"type\":\"verse\",\"number\":" << (first_verse + 1)
         << ",\"text\":\"The second verse of " << book_name << " " << chapter << ".\"}]}}";
    seed_cache(Bible::chapter_url(kBase, "BSB", book_id, chapter), json.str());
  }

  std::string chapter_url(const std::string &book_id, int chapter)
  {
    return Bible::chapter_url(kBase, "BSB", book_id, chapter);
  }

  bool has_line(const std::vector<std::string> &lines, const std::string &line)
  {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
  }

  bool has_text(const std::vector<std::string> &lines, const std::string &text)
  {
    for (const std::string &line : lines)
    {
      if (line.find(text) != std::string::npos)
      {
        return true;
      }
    }
    return false;
  }

  long long today_day()
  {
    const std::time_t now = std::time(nullptr);
    return now <= 0 ? 0 : (long long)(now / 86400);
  }
} // namespace

TEST_CASE("Bible: a reference opens in the passage tab", "[jot][bible]")
{
  seed_config_home();
  seed_chapter("JHN", "John", 3, 16);
  Editor e;

  e.run_ex_for_test("bible john 3:16");
  // The tab names the passage, and the passage is coloured.
  REQUIRE(e.buffer_for_test().filepath.rfind("[Bible] John 3:16", 0) == 0);
  REQUIRE_FALSE(e.buffer_for_test().decorations.empty());
  const std::vector<std::string> lines = e.bible_view_lines_for_test();
  REQUIRE(lines.front() == "John 3 - Berean Standard Bible (BSB)");
  // The requested verse is marked; its neighbour comes along as context.
  REQUIRE(has_line(lines, ">16  The first verse of John 3."));
  REQUIRE(has_line(lines, " 17  The second verse of John 3."));
  REQUIRE(e.message_for_test().find("John 3:16") != std::string::npos);

  // The same chapter without a verse reads whole, with no marker.
  e.run_ex_for_test("bible john 3");
  const std::vector<std::string> whole = e.bible_view_lines_for_test();
  REQUIRE(has_line(whole, " 16  The first verse of John 3."));
  REQUIRE_FALSE(has_text(whole, ">16"));
}

TEST_CASE("Bible: the passage tab is one buffer and it is not a file", "[jot][bible]")
{
  seed_config_home();
  seed_chapter("JHN", "John", 3, 16);
  seed_chapter("JHN", "John", 4, 1);
  Editor e;

  e.run_ex_for_test("bible john 3");
  REQUIRE(has_text(e.bible_view_lines_for_test(), "John 3."));
  const int after_first = e.buffer_count_for_test();

  // Walking on replaces the text where it is: one scratch tab, not one per
  // passage.
  e.run_ex_for_test("bible next");
  REQUIRE(has_text(e.bible_view_lines_for_test(), "John 4."));
  REQUIRE_FALSE(has_text(e.bible_view_lines_for_test(), "John 3."));
  REQUIRE(e.buffer_count_for_test() == after_first);
  // The name follows the passage, so the strip never lies about what is open.
  REQUIRE(e.buffer_for_test().filepath.rfind("[Bible] John 4", 0) == 0);

  // Back the other way, across nothing at all.
  e.run_ex_for_test("bible prev");
  REQUIRE(has_text(e.bible_view_lines_for_test(), "John 3."));

  // A scratch view is not a file: `:w` must not litter the workspace with one.
  const std::string tab_path = e.buffer_for_test().filepath;
  e.run_ex_for_test("w");
  REQUIRE_FALSE(fs::exists(tab_path));

  // And it is a view, not a document: typing, pasting and deleting leave the
  // passage exactly as it was laid out.
  const std::vector<std::string> before = e.bible_view_lines_for_test();
  REQUIRE_FALSE(e.insert_char_for_test('x'));
  REQUIRE(e.message_for_test().find("read-only") != std::string::npos);
  e.paste_text_for_test("pasted text");
  e.run_ex_for_test("replace verse line");
  const std::vector<std::string> after = e.bible_view_lines_for_test();
  REQUIRE(after == before);
  REQUIRE_FALSE(e.buffer_for_test().modified);
}

TEST_CASE("Bible: commentary, cross references and translations each render", "[jot][bible]")
{
  seed_config_home();
  seed_chapter("JHN", "John", 3, 16);
  seed_cache(Bible::commentary_url(kBase, "adam-clarke", "JHN", 3),
             R"({"commentary":{"id":"adam-clarke","englishName":"Adam Clarke Bible Commentary"},
                 "book":{"id":"JHN","commonName":"John"},
                 "chapter":{"number":3,"content":[
                   {"type":"verse","number":16,"content":["The note on verse sixteen."]}]}})");
  seed_cache(Bible::xref_url(kBase, "JHN", 3),
             R"({"dataset":{"id":"open-cross-ref"},"book":{"id":"JHN"},
                 "chapter":{"number":3,"content":[
                   {"verse":16,"references":[{"book":"ROM","chapter":5,"verse":8,"score":9}]}]}})");
  seed_cache(Bible::translations_url(kBase),
             R"({"translations":[{"id":"BSB","englishName":"Berean Standard Bible"},
                                 {"id":"ENGWEBP","englishName":"World English Bible"}]})");
  Editor e;

  e.run_ex_for_test("bible commentary john 3:16");
  const std::vector<std::string> commentary = e.bible_view_lines_for_test();
  REQUIRE(commentary.front() == "John 3 - Adam Clarke Bible Commentary (adam-clarke)");
  REQUIRE(has_line(commentary, ">16  The note on verse sixteen."));

  e.run_ex_for_test("bible xref john 3");
  const std::vector<std::string> xrefs = e.bible_view_lines_for_test();
  REQUIRE(xrefs.front() == "Cross references - John 3 (open-cross-ref)");
  REQUIRE(has_text(xrefs, "John 3:16  ->  Romans 5:8"));

  // `:bible xref` with no argument re-uses the last passage.
  e.run_ex_for_test("bible xref");
  REQUIRE(has_text(e.bible_view_lines_for_test(), "Romans 5:8"));

  e.run_ex_for_test("bible translations");
  REQUIRE(e.buffer_for_test().filepath == "[Bible] translations");
  const std::vector<std::string> translations = e.bible_view_lines_for_test();
  REQUIRE(translations.front() == "Available translations (2)");
  REQUIRE(has_text(translations, "Berean Standard Bible"));
  REQUIRE(e.message_for_test().find("2 translations") != std::string::npos);
}

TEST_CASE("Bible: the verse of the day is fetched once and kept for the day", "[jot][bible]")
{
  seed_config_home();
  const long long day = today_day();
  const Bible::Reference chapter = Bible::chapter_for_day(day);
  seed_chapter(chapter.book->id, chapter.book->name, chapter.chapter, 1);
  Editor e;

  const std::string first = e.bible_verse_of_the_day_for_test();
  REQUIRE_FALSE(first.empty());
  REQUIRE(e.bible_verse_of_the_day_label_for_test().rfind(chapter.book->name, 0) == 0);
  REQUIRE(e.bible_verse_of_the_day_label_for_test()
          == Bible::reference_label(Bible::Reference{
              chapter.book, chapter.chapter, true,
              Bible::verse_for_day(day, 2), Bible::verse_for_day(day, 2)}));

  // Second call: straight from the state, no fetch to refuse.
  const std::string second = e.bible_verse_of_the_day_for_test();
  REQUIRE(second == first);
  REQUIRE_FALSE(e.bible_request_running_for_test());

  // `:bible votd` shows the same passage in the tab.
  e.run_ex_for_test("bible votd");
  REQUIRE(has_text(e.bible_view_lines_for_test(), first));
}

TEST_CASE("Bible: bad input is refused with a reason", "[jot][bible]")
{
  seed_config_home();
  Editor e;

  e.run_ex_for_test("bible sunday dinner");
  REQUIRE(e.message_for_test().find("cannot read") != std::string::npos);

  e.run_ex_for_test("bible john 99");
  REQUIRE(e.message_for_test().find("John has 21 chapters") != std::string::npos);

  e.run_ex_for_test("bible commentary");
  REQUIRE(e.message_for_test().find("open a passage first") != std::string::npos);

  e.run_ex_for_test("bible next");
  REQUIRE(e.message_for_test().find("nothing to step from") != std::string::npos);
}
