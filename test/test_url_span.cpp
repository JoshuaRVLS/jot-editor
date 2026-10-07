// Link detection for Ctrl+click (src/base/url_span.h).
//
// The editor opens the URL under the pointer, so the span has to be the URL
// and nothing else: the dot that ends the sentence around it is the sentence's,
// the parens a markdown link adds are the markup's, and a name that merely
// looks like a host (`example.com`, `www.example.com`) is not a link at all.
// These are the cases that decide whether a click opens the browser or asks the
// language server about a token.
#include "base/url_span.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
  // What the pointer at byte column `col` answers with, or "" for no link.
  std::string span_at(const std::string &line, int col)
  {
    int start = -1;
    int end = -1;
    if (!url_span::find(line, col, start, end))
    {
      return "";
    }
    return line.substr((size_t)start, (size_t)(end - start));
  }

  // The column of `needle` in `line`, plus an offset, for readable cases.
  int col_of(const std::string &line, const std::string &needle, int offset = 0)
  {
    const size_t at = line.find(needle);
    REQUIRE(at != std::string::npos);
    return (int)at + offset;
  }
} // namespace

TEST_CASE("A link answers from every one of its cells", "[jot]")
{
  const std::string line = "  // see https://example.com/docs for more";
  const std::string url = "https://example.com/docs";
  // The scheme's first byte, its middle, the host, the path's last byte.
  for (int offset : {0, 4, 8, 12, (int)url.size() - 1})
  {
    INFO("column " << offset << " of " << url);
    REQUIRE(span_at(line, col_of(line, "https://example.com/docs", offset)) == url);
  }

  // The prose around it answers for itself: the word before, the word after,
  // the cell just past the line's end.
  REQUIRE(span_at(line, col_of(line, "see")) == "");
  REQUIRE(span_at(line, col_of(line, "for")) == "");
  REQUIRE(span_at(line, (int)line.size()) == "");
  REQUIRE(span_at(line, -1) == "");
}

TEST_CASE("The prose tail is not part of the link", "[jot]")
{
  struct Scene
  {
    const char *line;
    const char *url;
    const char *why;
  };
  const Scene scenes[] = {
      {"see https://example.com.", "https://example.com", "the sentence's dot"},
      {"see https://example.com, then", "https://example.com", "a comma"},
      {"see https://example.com;", "https://example.com", "a semicolon"},
      {"see https://example.com:", "https://example.com", "a colon"},
      {"really? https://example.com!", "https://example.com", "an exclamation"},
      {"why https://example.com?", "https://example.com", "a question mark"},
      {"(see https://example.com).", "https://example.com", "closing paren and dot"},
      {"[see https://example.com].", "https://example.com", "closing bracket"},
      {"<https://example.com>", "https://example.com", "markdown autolink"},
      {"|https://example.com|x", "https://example.com", "markdown table pipe"},
      {"\"https://example.com/a\"", "https://example.com/a", "a string's quote"},
      {"'https://example.com/a'", "https://example.com/a", "a single quote"},
      {"`https://example.com/a`", "https://example.com/a", "a code span's backtick"},
      // A closer the link itself opens belongs to it, however punctuation the
      // text after it looks: real hosts carry parens (Wikipedia) and queries
      // carry everything else.
      {"https://en.wikipedia.org/wiki/Jot_(editor) here",
       "https://en.wikipedia.org/wiki/Jot_(editor)", "a matched paren"},
      {"https://example.com/x?q=1&z=2 end", "https://example.com/x?q=1&z=2", "a query"},
      {"https://example.com/a/b/ end", "https://example.com/a/b/", "a trailing slash"},
      {"(see (https://example.com/a))", "https://example.com/a", "both closers trimmed"},
  };

  for (const Scene &scene : scenes)
  {
    const std::string line = scene.line;
    const int col = col_of(line, "http");
    INFO(scene.line << " (" << scene.why << ")");
    REQUIRE(span_at(line, col + 4) == scene.url);
  }
}

TEST_CASE("Only explicit schemes count", "[jot]")
{
  // A host without a scheme is a hostname in code as often as it is a place to
  // go; the editor does not guess.
  REQUIRE(span_at("visit www.example.com now", col_of("visit www.example.com now", "www")) == "");
  REQUIRE(span_at("host = example.com", col_of("host = example.com", "example")) == "");
  // A scheme has to start a word: the tail of one name is not another scheme.
  REQUIRE(span_at("xhttps://example.com", col_of("xhttps://example.com", "https")) == "");
  REQUIRE(span_at("ftps://example.com", col_of("ftps://example.com", "example")) == "");
  REQUIRE(span_at("ftp://example.com", col_of("ftp://example.com", "example")) == "");
  // A scheme with nothing behind it names nothing.
  REQUIRE(span_at("http://", 2) == "");
  REQUIRE(span_at("file://", 3) == "");
  REQUIRE(span_at("mailto:", 3) == "");
  REQUIRE(span_at("see mailto:.", col_of("see mailto:.", "mailto")) == "");

  // The other schemes the editor answers for, and the case the scheme may
  // arrive in.
  REQUIRE(span_at("mail me at mailto:dev@example.com now",
                  col_of("mail me at mailto:dev@example.com now", "mailto") + 4)
          == "mailto:dev@example.com");
  REQUIRE(span_at("open file:///tmp/notes.md please",
                  col_of("open file:///tmp/notes.md please", "file") + 3)
          == "file:///tmp/notes.md");
  REQUIRE(span_at("see HTTPS://Example.com/x",
                  col_of("see HTTPS://Example.com/x", "HTTPS") + 2)
          == "HTTPS://Example.com/x");
}

TEST_CASE("The pointer answers for the link it is inside", "[jot]")
{
  const std::string first = "https://a.example/x";
  const std::string second = "https://b.example/y";
  const std::string two = "// first " + first + " then " + second + " end";
  const int first_at = (int)two.find(first);
  const int second_at = (int)two.find(second);
  REQUIRE(span_at(two, first_at + 8) == first);
  REQUIRE(span_at(two, second_at + 8) == second);
  REQUIRE(span_at(two, first_at) == first);
  REQUIRE(span_at(two, first_at + (int)first.size() - 1) == first);
  // The cell right after the first URL is prose, not the second URL's scheme.
  REQUIRE(span_at(two, first_at + (int)first.size()) == "");
  REQUIRE(span_at(two, second_at + (int)second.size()) == "");

  // One link nested in another's query is one run, and the whole run is the
  // answer: the pointer inside it is inside the link.
  const std::string nested = "https://a.example/?next=https://b.example";
  REQUIRE(span_at(nested, col_of(nested, "https://b.example", 3)) == nested);
}
