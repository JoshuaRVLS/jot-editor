// Reading a build command's output back into locations (:compile).
//
// The shapes asserted here are the ones the tools in this tree print -- GCC and
// Clang's `file:line:col: error: message`, javac's column-less form, the
// generic `file:line: message`, rustc's message-then-arrow pair, Python's
// `File "x.py", line 3`, MSVC and tsc's `file(line,col): error CODE: message`.
// Two families have cases of their own because they are the ones a naive
// `:digits:` scan gets wrong: context lines that carry a location without
// naming a problem (GCC's include chain) and tool chatter that only looks like
// a path (`make: *** [Makefile:3: all] Error 1`). A jump list that lists those
// buries the errors it exists for.
#include "text/compile_parse.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

namespace
{
  // One line, one finding. The parser is stateful, so every case gets its own.
  bool feed_one(const std::string &line, CompileFinding &out)
  {
    compile_parse::Parser parser;
    return parser.feed(line, out);
  }
} // namespace

TEST_CASE("gcc's four-part location is a finding", "[compile]")
{
  CompileFinding finding;
  REQUIRE(feed_one("src/render/frame.cpp:184:5: error: 'foo' was not declared in this scope", finding));
  CHECK(finding.filepath == "src/render/frame.cpp");
  CHECK(finding.line == 184);
  CHECK(finding.col == 5);
  CHECK(finding.severity == 1);
  CHECK(finding.message == "'foo' was not declared in this scope");
}

TEST_CASE("a column-less location keeps its message whole", "[compile]")
{
  CompileFinding finding;
  REQUIRE(feed_one("src/ui/tabs.c:12: warning: unused variable 'x' [-Wunused-variable]", finding));
  CHECK(finding.filepath == "src/ui/tabs.c");
  CHECK(finding.line == 12);
  CHECK(finding.col == 0);
  CHECK(finding.severity == 2);
  CHECK(finding.message == "unused variable 'x' [-Wunused-variable]");
}

TEST_CASE("the severity keyword decides the severity", "[compile]")
{
  CompileFinding note;
  REQUIRE(feed_one("src/a.c:12:2: note: in expansion of macro 'X'", note));
  CHECK(note.severity == 3);

  CompileFinding fatal;
  REQUIRE(feed_one("src/a.c:12:2: fatal error: stdio.h: No such file or directory", fatal));
  CHECK(fatal.severity == 1);
  CHECK(fatal.message == "stdio.h: No such file or directory");

  // Plain javac: `error:` with no column and no code.
  CompileFinding javac;
  REQUIRE(feed_one("src/A.java:7: error: ';' expected", javac));
  CHECK(javac.line == 7);
  CHECK(javac.col == 0);
  CHECK(javac.message == "';' expected");
}

TEST_CASE("a line with no severity keyword is an error", "[compile]")
{
  CompileFinding finding;
  REQUIRE(feed_one("./main.go:12:5: undefined: foo", finding));
  CHECK(finding.filepath == "./main.go");
  CHECK(finding.line == 12);
  CHECK(finding.col == 5);
  CHECK(finding.severity == 1);
  CHECK(finding.message == "undefined: foo");
}

TEST_CASE("rustc's message waits for the arrow line that locates it", "[compile]")
{
  compile_parse::Parser parser;
  CompileFinding finding;
  CHECK_FALSE(parser.feed("error[E0425]: cannot find value `x` in this scope", finding));
  REQUIRE(parser.feed("  --> src/main.rs:12:5", finding));
  CHECK(finding.filepath == "src/main.rs");
  CHECK(finding.line == 12);
  CHECK(finding.col == 5);
  CHECK(finding.severity == 1);
  CHECK(finding.message == "cannot find value `x` in this scope");
}

TEST_CASE("a rustc warning arrives with its own severity", "[compile]")
{
  compile_parse::Parser parser;
  CompileFinding finding;
  CHECK_FALSE(parser.feed("warning: unused variable: `x`", finding));
  REQUIRE(parser.feed(" --> src/main.rs:3:9", finding));
  CHECK(finding.severity == 2);
  CHECK(finding.message == "unused variable: `x`");
}

TEST_CASE("a rustc arrow with no message in hand is not a finding", "[compile]")
{
  CompileFinding finding;
  CHECK_FALSE(feed_one("   --> src/main.rs:12:5", finding));
}

TEST_CASE("python's traceback frame is a finding", "[compile]")
{
  CompileFinding finding;
  REQUIRE(feed_one("  File \"tools/build.py\", line 42, in main", finding));
  CHECK(finding.filepath == "tools/build.py");
  CHECK(finding.line == 42);
  CHECK(finding.col == 0);
  CHECK(finding.severity == 1);
  CHECK(finding.message == "File \"tools/build.py\", line 42, in main");
}

TEST_CASE("a parenthesised location carries msvc and tsc", "[compile]")
{
  CompileFinding tsc;
  REQUIRE(feed_one("src/ui/tabs.ts(31,7): error TS2322: Type 'x' is not assignable.", tsc));
  CHECK(tsc.filepath == "src/ui/tabs.ts");
  CHECK(tsc.line == 31);
  CHECK(tsc.col == 7);
  CHECK(tsc.severity == 1);
  CHECK(tsc.message == "Type 'x' is not assignable.");

  CompileFinding msvc;
  REQUIRE(feed_one("src\\a.cpp(88): warning C4996: 'strcpy': deprecated", msvc));
  CHECK(msvc.filepath == "src\\a.cpp");
  CHECK(msvc.line == 88);
  CHECK(msvc.col == 0);
  CHECK(msvc.severity == 2);
  CHECK(msvc.message == "'strcpy': deprecated");
}

TEST_CASE("a windows drive letter is not a location", "[compile]")
{
  CompileFinding finding;
  REQUIRE(feed_one("C:\\src\\a.cpp:12:3: error: something broke", finding));
  CHECK(finding.filepath == "C:\\src\\a.cpp");
  CHECK(finding.line == 12);
  CHECK(finding.col == 3);
}

TEST_CASE("a path with a space survives, a tool's chatter does not", "[compile]")
{
  CompileFinding spaced;
  REQUIRE(feed_one("/home/a b/src/x.cpp:3:1: error: y", spaced));
  CHECK(spaced.filepath == "/home/a b/src/x.cpp");
  CHECK(spaced.line == 3);

  CompileFinding make;
  CHECK_FALSE(feed_one("make: *** [Makefile:3: all] Error 1", make));

  CompileFinding chain;
  CHECK_FALSE(feed_one("In file included from /usr/include/stdio.h:27:0,", chain));

  CompileFinding from;
  CHECK_FALSE(feed_one("                 from src/render/frame.cpp:3:", from));

  CompileFinding count;
  CHECK_FALSE(feed_one("1 error generated.", count));

  CompileFinding linker;
  CHECK_FALSE(feed_one("/usr/bin/ld: cannot find -lfoo", linker));
}

TEST_CASE("output that names nothing is not a finding", "[compile]")
{
  CompileFinding finding;
  CHECK_FALSE(feed_one("", finding));
  CHECK_FALSE(feed_one("   ", finding));
  CHECK_FALSE(feed_one("gcc -c -o a.o a.c", finding));
  CHECK_FALSE(feed_one("  |        ^^^^ expected value", finding));
  CHECK_FALSE(feed_one("= help: consider borrowing here", finding));
  CHECK_FALSE(feed_one("ninja: build stopped: subcommand failed.", finding));
}

TEST_CASE("a location followed by a colon and nothing else is not a finding", "[compile]")
{
  CompileFinding finding;
  CHECK_FALSE(feed_one("src/a.c:12:5:", finding));
}

TEST_CASE("reset drops a message that never found its location", "[compile]")
{
  compile_parse::Parser parser;
  CompileFinding finding;
  CHECK_FALSE(parser.feed("error: something", finding));
  parser.reset();
  CHECK_FALSE(parser.feed("  --> src/a.c:1:1", finding));
}
