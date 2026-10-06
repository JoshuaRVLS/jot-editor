// The Bible reader's pure half (src/features/bible.*): how a typed reference
// becomes a chapter and a chapter's JSON becomes the `[Bible]` tab's lines.
// No network here -- the editor side (jot/app/bible_client.cpp) is pinned by
// test/bible_probe.py against a local fixture server.
#include "features/bible.h"

#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace
{
  const char *kChapterJson = R"({
    "translation": {
      "id": "BSB",
      "name": "Berean Standard Bible",
      "englishName": "Berean Standard Bible"
    },
    "book": { "id": "JHN", "name": "John", "commonName": "John" },
    "chapter": {
      "number": 3,
      "content": [
        { "type": "heading", "text": "Jesus and Nicodemus" },
        { "type": "verse", "number": 16, "text": "For God so loved the world.", "footnotes": [] },
        { "type": "line_break" },
        { "type": "verse", "number": 17, "text": "For God did not send His Son.", "footnotes": [] },
        { "type": "verse", "number": 18, "text": "Whoever believes in Him is not condemned.", "footnotes": [] }
      ]
    }
  })";

  const char *kCommentaryJson = R"({
    "commentary": { "id": "adam-clarke", "englishName": "Adam Clarke Bible Commentary" },
    "book": { "id": "PHM", "commonName": "Philemon" },
    "chapter": {
      "number": 1,
      "introduction": "The preface.",
      "content": [
        { "type": "verse", "content": ["Philemon plm 0:0 Preface.", "A second paragraph."] },
        { "type": "verse", "number": 4, "content": ["I thank my God always."] }
      ]
    }
  })";

  const char *kXrefJson = R"({
    "dataset": { "id": "open-cross-ref" },
    "book": { "id": "JHN" },
    "chapter": {
      "number": 3,
      "content": [
        {
          "verse": 16,
          "references": [
            { "book": "ROM", "chapter": 5, "verse": 8, "score": 9 },
            { "book": "1JN", "chapter": 4, "verse": 9, "score": 7 },
            { "book": "2JN", "chapter": 1, "verse": 1, "endVerse": 13, "score": 3 }
          ]
        }
      ]
    }
  })";

  const char *kTranslationsJson = R"({
    "translations": [
      { "id": "BSB", "englishName": "Berean Standard Bible", "language": "eng" },
      { "id": "ENGWEBP", "englishName": "World English Bible", "language": "eng" }
    ]
  })";

  Bible::Reference parse(const std::string &text)
  {
    Bible::Reference reference;
    REQUIRE(Bible::parse_reference(text, reference));
    return reference;
  }
} // namespace

TEST_CASE("Bible book lookup answers to ids, names and the usual abbreviations", "[bible]")
{
  REQUIRE(Bible::find_book("JHN"));
  REQUIRE(std::string(Bible::find_book("JHN")->name) == "John");
  REQUIRE(Bible::find_book("john"));
  REQUIRE(Bible::find_book("Jn"));
  REQUIRE(Bible::find_book("1 cor"));
  REQUIRE(std::string(Bible::find_book("1cor")->name) == "1 Corinthians");
  REQUIRE(Bible::find_book("1 Corinthians"));
  REQUIRE(Bible::find_book("Ps"));
  REQUIRE(std::string(Bible::find_book("ps")->name) == "Psalms");
  REQUIRE(Bible::find_book("song of solomon"));
  REQUIRE(Bible::find_book("Song of Songs"));
  REQUIRE(Bible::find_book("Rev"));
  REQUIRE(Bible::find_book("Revelation"));
  REQUIRE(Bible::find_book("3jn"));
  REQUIRE(Bible::find_book("1st John"));
  REQUIRE_FALSE(Bible::find_book("hezekiah"));
  REQUIRE_FALSE(Bible::find_book("not a book"));
  // The table is the whole canon, in order.
  REQUIRE(Bible::books().size() == 66);
  REQUIRE(std::string(Bible::books().front().id) == "GEN");
  REQUIRE(std::string(Bible::books().back().id) == "REV");
}

TEST_CASE("Bible references parse the way people type them", "[bible]")
{
  Bible::Reference reference = parse("John 3:16");
  REQUIRE(std::string(reference.book->id) == "JHN");
  REQUIRE(reference.chapter == 3);
  REQUIRE(reference.has_verse);
  REQUIRE(reference.verse_first == 16);
  REQUIRE(reference.verse_last == 16);

  reference = parse("john3:16");
  REQUIRE(std::string(reference.book->id) == "JHN");
  REQUIRE(reference.chapter == 3);
  REQUIRE(reference.verse_first == 16);

  reference = parse("1cor13:4-7");
  REQUIRE(std::string(reference.book->id) == "1CO");
  REQUIRE(reference.chapter == 13);
  REQUIRE(reference.verse_first == 4);
  REQUIRE(reference.verse_last == 7);
  REQUIRE(reference.has_verse_span());

  reference = parse("1 Cor 13:4-7");
  REQUIRE(std::string(reference.book->id) == "1CO");
  REQUIRE(reference.verse_first == 4);

  // The `bib` reference CLI's spaced form: book+chapter, then the verses.
  reference = parse("1cor13 4-7");
  REQUIRE(std::string(reference.book->id) == "1CO");
  REQUIRE(reference.chapter == 13);
  REQUIRE(reference.verse_first == 4);
  REQUIRE(reference.verse_last == 7);

  reference = parse("ps23");
  REQUIRE(std::string(reference.book->id) == "PSA");
  REQUIRE(reference.chapter == 23);
  REQUIRE_FALSE(reference.has_verse);

  reference = parse("Song of Solomon 3:1");
  REQUIRE(std::string(reference.book->id) == "SNG");
  REQUIRE(reference.chapter == 3);
  REQUIRE(reference.verse_first == 1);

  reference = parse("genesis1.1");
  REQUIRE(std::string(reference.book->id) == "GEN");
  REQUIRE(reference.chapter == 1);
  REQUIRE(reference.verse_first == 1);

  // A bare book name opens its first chapter.
  reference = parse("jude");
  REQUIRE(std::string(reference.book->id) == "JUD");
  REQUIRE(reference.chapter == 1);
  REQUIRE_FALSE(reference.has_verse);

  // Verses can run backwards as typed; the span is normalised.
  reference = parse("john 3:17-16");
  REQUIRE(reference.verse_first == 17);
  REQUIRE(reference.verse_last == 17);

  REQUIRE(Bible::reference_label(parse("john3:16-17")) == "John 3:16-17");
  REQUIRE(Bible::reference_label(parse("john3")) == "John 3");
  REQUIRE(Bible::reference_short(parse("john3:16-17")) == "John 3:16");

  Bible::Reference rejected;
  REQUIRE_FALSE(Bible::parse_reference("", rejected));
  REQUIRE_FALSE(Bible::parse_reference("hello world", rejected));
  REQUIRE_FALSE(Bible::parse_reference("john3abc", rejected));
  REQUIRE_FALSE(Bible::parse_reference("3:16", rejected));
}

TEST_CASE("Bible reference hover finds the passage under the caret", "[bible]")
{
  const std::string line = "As it says in John 3:16, God loved the world.";
  const int at = (int)line.find("3:16");
  int start = 0;
  int end = 0;
  Bible::Reference reference;
  REQUIRE(Bible::reference_at(line, at, start, end, reference));
  REQUIRE(std::string(reference.book->id) == "JHN");
  REQUIRE(reference.verse_first == 16);
  REQUIRE(line.substr((size_t)start, (size_t)(end - start)) == "John 3:16");

  // The caret on the book name finds the same span.
  REQUIRE(Bible::reference_at(line, (int)line.find("John"), start, end, reference));
  REQUIRE(line.substr((size_t)start, (size_t)(end - start)) == "John 3:16");

  // A sentence with no reference yields nothing.
  REQUIRE_FALSE(Bible::reference_at("God loved the world.", 4, start, end, reference));
  // `john3abc` is not a reference, so neither is the hover.
  REQUIRE_FALSE(Bible::reference_at("see john3abc here", 6, start, end, reference));
}

TEST_CASE("Bible URLs match the API guide", "[bible]")
{
  const std::string base = "https://bible.helloao.org/api";
  REQUIRE(Bible::translations_url(base)
          == "https://bible.helloao.org/api/available_translations.json");
  REQUIRE(Bible::books_url(base, "BSB") == "https://bible.helloao.org/api/BSB/books.json");
  REQUIRE(Bible::chapter_url(base, "BSB", "JHN", 3)
          == "https://bible.helloao.org/api/BSB/JHN/3.simple.json");
  REQUIRE(Bible::commentary_url(base, "adam-clarke", "GEN", 1)
          == "https://bible.helloao.org/api/c/adam-clarke/GEN/1.json");
  REQUIRE(Bible::xref_url(base, "JHN", 3)
          == "https://bible.helloao.org/api/d/open-cross-ref/JHN/3.json");
  // A trailing slash on the base does not double up.
  REQUIRE(Bible::chapter_url(base + "/", "BSB", "JHN", 3)
          == "https://bible.helloao.org/api/BSB/JHN/3.simple.json");
}

TEST_CASE("Bible cache names are stable per URL and distinct across URLs", "[bible]")
{
  const std::string a = Bible::cache_file_name("https://example/BSB/JHN/3.simple.json");
  const std::string b = Bible::cache_file_name("https://example/BSB/JHN/3.simple.json");
  const std::string c = Bible::cache_file_name("https://example/BSB/JHN/4.simple.json");
  REQUIRE(a == b);
  REQUIRE(a != c);
  REQUIRE(a.rfind("bible-", 0) == 0);
  REQUIRE(a.size() == std::string("bible-").size() + 16 + std::string(".json").size());
}

TEST_CASE("Bible chapter JSON becomes readable lines", "[bible]")
{
  Bible::Chapter chapter;
  REQUIRE(Bible::parse_chapter(kChapterJson, chapter));
  REQUIRE(chapter.translation_id == "BSB");
  REQUIRE(chapter.translation_name == "Berean Standard Bible");
  REQUIRE(chapter.book_id == "JHN");
  REQUIRE(chapter.book_name == "John");
  REQUIRE(chapter.chapter == 3);
  REQUIRE(chapter.verse_numbers == std::vector<int>{16, 17, 18});
  REQUIRE(Bible::verse_text(chapter, 17) != nullptr);
  REQUIRE(*Bible::verse_text(chapter, 17) == "For God did not send His Son.");
  REQUIRE(Bible::verse_text(chapter, 99) == nullptr);

  Bible::Reference whole;
  whole.book = Bible::find_book("john");
  whole.chapter = 3;
  const std::vector<std::string> lines =
      Bible::render_chapter(chapter, whole, {60, true, 0});
  REQUIRE(lines.front() == "John 3 - Berean Standard Bible (BSB)");
  // The heading precedes the verse it introduces, and each verse has its own
  // numbered paragraph.
  REQUIRE(std::find(lines.begin(), lines.end(), "Jesus and Nicodemus") != lines.end());
  REQUIRE(std::find(lines.begin(), lines.end(), " 16  For God so loved the world.") != lines.end());
  REQUIRE(std::find(lines.begin(), lines.end(), " 17  For God did not send His Son.") != lines.end());

  // A single verse request marks it with `>` and keeps its context.
  const Bible::Reference single = parse("john3:17");
  REQUIRE(single.has_verse);
  const std::vector<std::string> narrowed =
      Bible::render_chapter(chapter, single, {60, true, 1});
  REQUIRE(std::find(narrowed.begin(), narrowed.end(), ">17  For God did not send His Son.")
          != narrowed.end());
  REQUIRE(std::find(narrowed.begin(), narrowed.end(), " 16  For God so loved the world.")
          != narrowed.end());
  REQUIRE(std::find(narrowed.begin(), narrowed.end(), " 18  Whoever believes in Him is not condemned.")
          != narrowed.end());
}

TEST_CASE("Bible chapter rendering wraps long verses under the number", "[bible]")
{
  Bible::Chapter chapter;
  REQUIRE(Bible::parse_chapter(kChapterJson, chapter));
  Bible::Reference reference;
  reference.book = Bible::find_book("john");
  reference.chapter = 3;
  const std::vector<std::string> lines =
      Bible::render_chapter(chapter, reference, {24, false, 0});
  // Every wrapped continuation stays inside the render width and is indented
  // past the verse number.
  for (const std::string &line : lines)
  {
    REQUIRE((int)line.size() <= 40);
  }
  bool saw_continuation = false;
  for (const std::string &line : lines)
  {
    if (line.rfind("    ", 0) == 0 && !line.empty())
    {
      saw_continuation = true;
    }
  }
  REQUIRE(saw_continuation);
}

TEST_CASE("Bible rendering is highlighted by structure", "[bible]")
{
  // The colours follow the layout the renderers just wrote: the header leads,
  // the editorial headings read as headings, verse numbers as numbers, and a
  // requested verse's gutter and number stand out from the context around it.
  Bible::Chapter chapter;
  REQUIRE(Bible::parse_chapter(kChapterJson, chapter));
  const Bible::Reference single = parse("john3:17");
  const std::vector<std::string> lines =
      Bible::render_chapter(chapter, single, {60, true, 1});
  const std::vector<Bible::Span> spans = Bible::highlight(lines, Bible::Layout::Chapter);

  REQUIRE_FALSE(spans.empty());
  REQUIRE(spans.front().line == 0);
  REQUIRE(spans.front().group == "keyword");
  REQUIRE(spans.front().start == 0);
  REQUIRE(spans.front().end == (int)lines.front().size());

  int requested_line = -1;
  int heading_line = -1;
  int context_line = -1;
  for (int i = 0; i < (int)lines.size(); i++)
  {
    if (lines[(size_t)i].rfind(">17", 0) == 0)
      requested_line = i;
    if (lines[(size_t)i] == "Jesus and Nicodemus")
      heading_line = i;
    if (lines[(size_t)i].rfind(" 16", 0) == 0)
      context_line = i;
  }
  REQUIRE(requested_line > 0);
  REQUIRE(heading_line > 0);
  REQUIRE(context_line > 0);

  bool marker = false;
  bool requested_number = false;
  bool context_number = false;
  bool heading = false;
  for (const Bible::Span &span : spans)
  {
    if (span.line == requested_line && span.start == 0 && span.end == 1
        && span.group == "constant")
      marker = true;
    if (span.line == requested_line && span.start == 1 && span.end == 3
        && span.group == "constant")
      requested_number = true;
    if (span.line == context_line && span.start == 1 && span.end == 3
        && span.group == "number")
      context_number = true;
    if (span.line == heading_line && span.group == "type")
      heading = true;
  }
  REQUIRE(marker);
  REQUIRE(requested_number);
  REQUIRE(context_number);
  REQUIRE(heading);
}

TEST_CASE("Bible commentary JSON keeps prose per verse", "[bible]")
{
  Bible::Chapter chapter;
  REQUIRE(Bible::parse_commentary(kCommentaryJson, chapter));
  REQUIRE(chapter.book_name == "Philemon");
  REQUIRE(chapter.translation_name == "Adam Clarke Bible Commentary");
  REQUIRE(chapter.verse_numbers == std::vector<int>{1, 4});
  REQUIRE(Bible::verse_text(chapter, 1) != nullptr);
  REQUIRE(*Bible::verse_text(chapter, 1) == "Philemon plm 0:0 Preface.\nA second paragraph.");
}

TEST_CASE("Bible cross references sort strongest first and render grouped", "[bible]")
{
  std::vector<Bible::Xref> xrefs;
  REQUIRE(Bible::parse_xrefs(kXrefJson, xrefs));
  REQUIRE(xrefs.size() == 3);
  REQUIRE(xrefs.front().score == 9);
  REQUIRE(std::string(Bible::find_book(xrefs.front().book)->id) == "ROM");
  REQUIRE(xrefs.back().verse_end == 13);

  Bible::Reference reference;
  reference.book = Bible::find_book("john");
  reference.chapter = 3;
  const std::vector<std::string> lines = Bible::render_xrefs(xrefs, reference, 80);
  REQUIRE(lines.front() == "Cross references - John 3 (open-cross-ref)");
  REQUIRE(lines[1] == "John 3:16  ->  Romans 5:8  (9)");
  REQUIRE(lines[2] == "           ->  1 John 4:9  (7)");
  REQUIRE(lines[3] == "           ->  2 John 1:1-13  (3)");

  std::vector<std::string> empty = Bible::render_xrefs({}, reference, 80);
  REQUIRE(empty.back() == "No cross references for this chapter");

  // The list's colours: the header, the referring verse, the arrow and the
  // verse it points at.
  const std::vector<Bible::Span> spans = Bible::highlight(lines, Bible::Layout::Xrefs);
  REQUIRE(spans.front().group == "keyword");
  bool from = false;
  bool arrow = false;
  bool target = false;
  for (const Bible::Span &span : spans)
  {
    if (span.line != 1)
      continue;
    if (span.start == 0 && span.group == "number")
      from = true;
    if (lines[1].substr((size_t)span.start, (size_t)(span.end - span.start)) == "  ->  "
        && span.group == "punctuation")
      arrow = true;
    if (lines[1].substr((size_t)span.start, (size_t)(span.end - span.start)) == "Romans 5:8"
        && span.group == "string")
      target = true;
  }
  REQUIRE(from);
  REQUIRE(arrow);
  REQUIRE(target);
}

TEST_CASE("Bible translations list across the catalog", "[bible]")
{
  std::vector<Bible::Translation> translations;
  REQUIRE(Bible::parse_translations(kTranslationsJson, translations));
  REQUIRE(translations.size() == 2);
  REQUIRE(translations[0].id == "BSB");
  REQUIRE(translations[1].id == "ENGWEBP");
  const std::vector<std::string> lines = Bible::render_translations(translations, 80);
  REQUIRE(lines.front() == "Available translations (2)");
  REQUIRE(lines[2].rfind("BSB", 0) == 0);
  REQUIRE(lines[2].find("Berean Standard Bible") != std::string::npos);

  const std::vector<Bible::Span> spans =
      Bible::highlight(lines, Bible::Layout::Translations);
  REQUIRE(spans.front().group == "keyword"); // the "Available translations" header
  bool id = false;
  for (const Bible::Span &span : spans)
  {
    if (span.line == 2 && span.start == 0 && span.end == 3 && span.group == "constant")
      id = true;
  }
  REQUIRE(id);
}

TEST_CASE("Bible verse of the day is stable for a day and spreads across the canon",
          "[bible]")
{
  const Bible::Reference first = Bible::chapter_for_day(20000);
  const Bible::Reference again = Bible::chapter_for_day(20000);
  REQUIRE(first.book != nullptr);
  REQUIRE(first.book->id == again.book->id);
  REQUIRE(first.chapter == again.chapter);
  REQUIRE(first.chapter >= 1);
  REQUIRE(first.chapter <= first.book->chapters);

  // The next day is a different chapter (not merely one along).
  const Bible::Reference next = Bible::chapter_for_day(20001);
  REQUIRE((next.book != first.book || next.chapter != first.chapter));

  // The verse pick stays inside the chapter it was asked about.
  for (long long day = 20000; day < 20040; day++)
  {
    const int verse = Bible::verse_for_day(day, 31);
    REQUIRE(verse >= 1);
    REQUIRE(verse <= 31);
  }
  REQUIRE(Bible::verse_for_day(20000, 0) == 1);
}
