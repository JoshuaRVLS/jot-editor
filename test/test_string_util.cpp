// The shared text helpers (src/base/string_util.h).
//
// These replaced a copy of each transform in every module that needed one, so
// the point of most of these cases is that the shared version still behaves
// like the private ones did. The line windowing is checked against a naive
// std::getline split: the view helpers exist to avoid building that vector, so
// it is the honest thing to compare them to.

#include "base/string_util.h"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
  // What std::getline reads, with the CR of a CRLF line taken off.
  std::vector<std::string> ref_lines(const std::string &text)
  {
    std::vector<std::string> out;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      out.push_back(line);
    }
    return out;
  }
} // namespace

TEST_CASE("string_util: trimming finds the text between the spaces", "[jot]")
{
  const std::string padded = "  \t value \r\n";
  REQUIRE(string_util::trim_view(padded) == "value");
  REQUIRE(string_util::ltrim_view(padded) == "value \r\n");
  REQUIRE(string_util::rtrim_view(padded) == "  \t value");
  REQUIRE(string_util::trim_copy(padded) == "value");
  REQUIRE(string_util::ltrim_copy(padded) == "value \r\n");

  REQUIRE(string_util::trim_view("   ").empty());
  REQUIRE(string_util::trim_view("").empty());
  REQUIRE(string_util::is_space('\n'));
  REQUIRE_FALSE(string_util::is_space('x'));
}

TEST_CASE("string_util: case folding and the prefix tests", "[jot]")
{
  std::string upper = "MiXeD";
  string_util::to_lower(upper);
  REQUIRE(upper == "mixed");
  REQUIRE(string_util::lower_copy("AbC") == "abc");

  REQUIRE(string_util::starts_with("src/a/b.cpp", "src/"));
  REQUIRE_FALSE(string_util::starts_with("s", "src/"));
  REQUIRE(string_util::ends_with("src/a/b.cpp", ".cpp"));
  REQUIRE_FALSE(string_util::ends_with("src/a/b.cpp", ".c"));
  REQUIRE(string_util::contains("src/a/b.cpp", "a/b"));
  REQUIRE(string_util::starts_with_icase("Content-Type", "content-"));
  REQUIRE_FALSE(string_util::starts_with_icase("con", "content-"));
}

TEST_CASE("string_util: to_int reads a session field", "[jot]")
{
  REQUIRE(string_util::to_int("42") == 42);
  REQUIRE(string_util::to_int("-7") == -7);
  REQUIRE(string_util::to_int("  13 ") == 13);
  REQUIRE(string_util::to_int("12abc") == 12);

  REQUIRE(string_util::to_int("") == std::nullopt);
  REQUIRE(string_util::to_int("   ") == std::nullopt);
  REQUIRE(string_util::to_int("abc") == std::nullopt);
  REQUIRE(string_util::to_int("99999999999999999999") == std::nullopt);
}

TEST_CASE("string_util: splitting keeps or drops empty fields", "[jot]")
{
  const std::string record = "file\t/tmp/a.cpp\t3\t\t1";
  const std::vector<std::string_view> parts = string_util::split(record, '\t');
  REQUIRE(parts.size() == 5);
  REQUIRE(parts[0] == "file");
  REQUIRE(parts[2] == "3");
  REQUIRE(parts[3].empty());
  REQUIRE(string_util::join(parts, "\t") == record);

  // An empty line is one empty field, and a trailing delimiter still ends a field.
  REQUIRE(string_util::split("", '\t').size() == 1);
  REQUIRE(string_util::split("a\t", '\t').size() == 2);

  const std::vector<std::string_view> picked = string_util::fields(" explorer, git ,, debug ", ',');
  REQUIRE(picked.size() == 3);
  REQUIRE(picked[0] == "explorer");
  REQUIRE(picked[1] == "git");
  REQUIRE(picked[2] == "debug");
}

TEST_CASE("string_util: a trailing newline closes a line instead of opening one", "[jot]")
{
  const std::vector<std::string> texts = {"",
                                          "a",
                                          "a\n",
                                          "\n",
                                          "a\nb",
                                          "a\nb\n",
                                          "a\n\nb",
                                          "a\r\nb\r\n"};
  for (const std::string &text : texts)
  {
    const std::vector<std::string> reference = ref_lines(text);
    REQUIRE(string_util::line_count(text) == reference.size());

    const std::vector<std::string_view> got = string_util::lines(text);
    REQUIRE(got.size() == reference.size());
    for (size_t i = 0; i < reference.size(); i++)
    {
      REQUIRE(got[i] == reference[i]);
    }
  }
}

TEST_CASE("string_util: the tail window is the last lines of a full split", "[jot]")
{
  std::string text;
  for (int i = 0; i < 40; i++)
  {
    text += "line " + std::to_string(i) + "\n";
  }
  text += "tail without a break";
  const std::vector<std::string> reference = ref_lines(text);

  for (size_t count = 0; count <= 6; count++)
  {
    for (size_t skip = 0; skip <= 6; skip++)
    {
      const std::vector<std::string_view> window = string_util::tail_lines(text, count, skip);
      const size_t available = reference.size() > skip ? reference.size() - skip : 0;
      const size_t expected = std::min(count, available);
      REQUIRE(window.size() == expected);
      const size_t first = reference.size() - skip - expected;
      for (size_t i = 0; i < window.size(); i++)
      {
        REQUIRE(window[i] == reference[first + i]);
      }
    }
  }

  // A window wider or further back than the text comes back short, not broken.
  REQUIRE(string_util::tail_lines("one\ntwo", 10, 0).size() == 2);
  REQUIRE(string_util::tail_lines("one\ntwo", 10, 1).front() == "one");
  REQUIRE(string_util::tail_lines("one\ntwo", 1, 1).front() == "one");
  REQUIRE(string_util::tail_lines("one\ntwo", 5, 5).empty());
  REQUIRE(string_util::tail_lines("", 5, 0).empty());
  REQUIRE(string_util::tail_lines("one\ntwo", 0, 0).empty());
}

TEST_CASE("string_util: limit_lines marks a truncated popup and nothing else", "[jot]")
{
  // git ends its output with a newline, and a popup holding exactly the limit
  // was not truncated. The :gitstatus copy this replaced read its lines into a
  // stream and asked whether that stream was still "good": a stream that has
  // only stopped because the count ran out is, so a status list of exactly 18
  // rows came back with a "..." row under it that was not hiding anything.
  REQUIRE(string_util::limit_lines("a\nb\n", 2) == "a\nb");
  REQUIRE(string_util::limit_lines("a\nb", 2) == "a\nb");
  REQUIRE(string_util::limit_lines("a\nb\n", 5) == "a\nb");
  REQUIRE(string_util::limit_lines("a\n", 1) == "a");
  REQUIRE(string_util::limit_lines("a\nb\nc\n", 2) == "a\nb\n...");
  REQUIRE(string_util::limit_lines("a\nb\nc", 2) == "a\nb\n...");
  REQUIRE(string_util::limit_lines("a", 1) == "a");
  REQUIRE(string_util::limit_lines("", 3) == "");
  REQUIRE(string_util::limit_lines("a\nb", 0) == "");
  REQUIRE(string_util::limit_lines("a\nb", -1) == "");

  // CRLF from a Windows shell must not leave the carriage return on the row.
  REQUIRE(string_util::limit_lines("a\r\nb\r\n", 2) == "a\nb");
  REQUIRE(string_util::limit_lines("a\r\nb\r\n", 5) == "a\nb");
}

TEST_CASE("string_util: the session field encoding survives tabs and newlines", "[jot]")
{
  const std::string raw = "a\tb\nc\\d";
  const std::string escaped = string_util::escape_field(raw);
  REQUIRE(escaped == "a\\tb\\nc\\\\d");
  REQUIRE(string_util::unescape_field(escaped) == raw);
  // The encoding is what the state files are read back through, so a field
  // that holds a tab has to stay one field.
  REQUIRE(string_util::split(escaped, '\t').size() == 1);

  REQUIRE(string_util::unescape_field("") == "");
  REQUIRE(string_util::unescape_field("x\\") == "x\\");
}

TEST_CASE("string_util: first_line_copy stops at either line break", "[jot]")
{
  REQUIRE(string_util::first_line_copy("one\ntwo") == "one");
  REQUIRE(string_util::first_line_copy("one\r\ntwo") == "one");
  REQUIRE(string_util::first_line_copy("one") == "one");
  REQUIRE(string_util::first_line_copy("") == "");
  REQUIRE(string_util::first_line_view("one\ntwo") == "one");
}
