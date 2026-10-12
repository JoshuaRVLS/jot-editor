#ifndef JOT_COMPILE_PARSE_H
#define JOT_COMPILE_PARSE_H

// Errorformat, the small part of it that matters: reading a build command's
// output back into the locations it names, so `:cnext` can walk them the way
// `M-g n` walks Emacs' *compilation* buffer.
//
// The shapes handled are the ones the tools people build with actually print:
// GCC/Clang's `file:line:col: error: message`, javac's column-less form, the
// generic `file:line: message`, rustc's message-then-arrow pair, Python's
// `File "x.py", line 3` and MSVC/tsc's `file(line,col): error CODE: message`.
// What is deliberately *not* a finding is spelled out in the tests: make's
// `*** [Makefile:3: all] Error 1` and GCC's `In file included from ...` lines
// carry a location-looking run of digits but name no problem of their own, and
// a jump list that lists them buries the errors that matter.

#include <string>
#include <vector>

// One jumpable location a build command printed. Line and column are the
// numbers as printed (1-based); the caller resolves a relative `filepath`
// against the directory the command ran in.
struct CompileFinding
{
  std::string filepath;
  int line = 0;
  int col = 0; // 0 when the tool printed no column
  // 1 error, 2 warning, 3 note -- the severities the editor's other finding
  // lists use, so a compile finding and a diagnostic are coloured alike.
  int severity = 1;
  std::string message;
};

namespace compile_parse
{
  // Feeds a command's output one line at a time. Stateful because several
  // tools split one finding over two lines: rustc prints
  // `error[E0425]: cannot find value` and then `--> src/main.rs:12:5`, and the
  // message has to wait for the line that carries the location.
  class Parser
  {
  public:
    // True when `line` names a location, which is then written to `out`.
    bool feed(const std::string &line, CompileFinding &out);

    // Forgets a half-read finding (a new run starts a fresh parser anyway; a
    // caller that stops reading mid-output says so here).
    void reset();

  private:
    std::string pending_message_;
    int pending_severity_ = 0;
  };
} // namespace compile_parse

#endif
