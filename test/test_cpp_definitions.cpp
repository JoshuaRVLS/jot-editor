// The C++ definition checks (src/syntax/cpp_definitions.*).
//
// The pure half drives parse_file/analyze on text, so each heuristic the parser
// answers -- a call is not a declaration, an initializer is not a declaration,
// `= 0` needs no body, an inline body may repeat -- is a case of its own. The
// editor half (at the bottom) runs a real scan over a throwaway workspace and
// checks what lands in the diagnostics every surface reads.
#include "cpp_definitions.h"
#include "core/editor.h"
#include "ui/text.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
  namespace fs = std::filesystem;
  using CppDefinitions::FunctionRecord;
  using CppDefinitions::Issue;

  constexpr int kError = 1;
  constexpr int kWarning = 2;
  constexpr int kInfo = 3;

  void seed_config_home()
  {
    char home[] = "/tmp/jot_cpp_defs_test_XXXXXX";
    REQUIRE(mkdtemp(home) != nullptr);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }

  std::vector<FunctionRecord> parse_all(const std::vector<std::pair<std::string, std::string>> &files)
  {
    std::vector<FunctionRecord> records;
    for (const auto &file : files)
    {
      std::vector<FunctionRecord> parsed = CppDefinitions::parse_file(file.first, file.second);
      records.insert(records.end(), parsed.begin(), parsed.end());
    }
    return records;
  }

  std::string describe(const std::vector<Issue> &issues)
  {
    std::string out;
    for (const Issue &issue : issues)
    {
      out += fs::path(issue.file).filename().string() + ":" + std::to_string(issue.line + 1) + ":"
             + std::to_string(issue.col + 1) + " sev" + std::to_string(issue.severity) + " "
             + issue.message + "\n";
    }
    return out.empty() ? "(no issues)\n" : out;
  }

  std::vector<Issue> check(const std::vector<std::pair<std::string, std::string>> &files)
  {
    const std::vector<Issue> issues = CppDefinitions::analyze(parse_all(files));
    INFO(describe(issues));
    return issues;
  }

  const Issue *issue_for(const std::vector<Issue> &issues, const std::string &needle)
  {
    for (const Issue &issue : issues)
    {
      if (issue.message.find(needle) != std::string::npos)
      {
        return &issue;
      }
    }
    return nullptr;
  }
} // namespace

TEST_CASE("C++ definitions: a header declaration with no body is reported", "[jot]")
{
  const std::vector<Issue> issues = check({{"widget.hpp", "void reset();\nint count() const;\n"}});
  REQUIRE(issues.size() == 2);
  REQUIRE(issues[0].severity == kWarning);
  REQUIRE(issues[0].line == 0);
  REQUIRE(issues[0].col == 5);
  REQUIRE(issue_for(issues, "\"reset()\"") != nullptr);
  REQUIRE(issue_for(issues, "\"count() const\"") != nullptr);
  REQUIRE(issues[0].end_line == 0);
  REQUIRE(issues[0].end_col == 10); // one past `reset`, the whole name
}

TEST_CASE("C++ definitions: source declarations without a body are reported at the name", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"linkedlist.c", "#include <stdio.h>\nvoid render();\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kWarning);
  REQUIRE(issues[0].line == 1);
  REQUIRE(issues[0].col == 5);
  REQUIRE(issues[0].end_line == 1);
  REQUIRE(issues[0].end_col == 11);
  REQUIRE(issues[0].message.find("No definition found") != std::string::npos);
}

TEST_CASE("C++ definitions: a source declaration is satisfied by a workspace definition", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"linkedlist.c", "void render();\n"}, {"render.c", "void render() {}\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: another standalone program cannot implement triangle's render", "[jot]")
{
  const auto issues = check(
      {{"game/triangle.c", "#include <stdio.h>\n\nvoid render();\n\nint main() { return 0; }\n"},
       {"game/pacman.c", "void render();\nint main() { render(); }\nvoid render() {}\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].file == "game/triangle.c");
  REQUIRE(issues[0].line == 2);
  REQUIRE(issues[0].col == 5);
  REQUIRE(issues[0].end_col == 11); // one past `render`, the whole name
  REQUIRE(issues[0].message.find("No definition found") != std::string::npos);

  REQUIRE(
      check({{"triangle.c", "void render();\nint main() {}\n"}, {"render.c", "void render() {}\n"}})
          .empty());
  REQUIRE(check({{"triangle.c", "void render();\nint main() {}\nvoid render() {}\n"},
                 {"pacman.c", "int main() {}\nvoid render() {}\n"}})
              .empty());
}

TEST_CASE("C++ definitions: qualified missing declarations underline the final name component",
          "[jot]")
{
  const std::vector<Issue> issues =
      check({{"widget.hpp", "namespace app { void render_widget(int value); }\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].line == 0);
  REQUIRE(issues[0].col == 21);
  REQUIRE(issues[0].end_line == 0);
  REQUIRE(issues[0].end_col == 34); // one past `render_widget`, the whole name
}

TEST_CASE("C++ definitions: parameter names do not have to match", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"widget.hpp", "void reset(int flags = 0);\nvoid draw(const char *name);\nvoid sizes(int of[4]);\n"},
             {"widget.cpp",
              "void reset(int f) {}\nvoid draw(char const *text) {}\nvoid sizes(int *out) {}\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: platform type spellings and keyword-ish names match", "[jot]")
{
  // `module` is a context-sensitive keyword and a perfectly ordinary parameter
  // name; `DWORD` / `std::uint8_t` are the same types as what the definition
  // writes, so a pair like this is complete rather than missing.
  const std::vector<Issue> issues =
      check({{"buffer.hpp",
              "void load_script(const std::string &module, int export_mode);\n"
              "void move_bytes(DWORD code, const std::vector<std::uint8_t> &bytes);\n"},
             {"buffer.cpp",
              "void load_script(const std::string &name, int mode) {}\n"
              "void move_bytes(unsigned long code, const std::vector<unsigned char> &data) {}\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: platform-selected files are alternatives, not duplicates", "[jot]")
{
  // A build system compiles one of these, never both.
  const std::vector<Issue> alternatives =
      check({{"platform_win32.cpp", "int spawn(const std::string &cmd) { return 1; }\n"},
             {"platform_posix.cpp", "int spawn(const std::string &cmd) { return 0; }\n"}});
  REQUIRE(alternatives.empty());

  // Two files of the *same* platform still are duplicates, and so are two
  // untagged ones.
  const std::vector<Issue> same_platform =
      check({{"a_win32.cpp", "int spawn() { return 1; }\n"},
             {"b_win32.cpp", "int spawn() { return 2; }\n"}});
  REQUIRE(same_platform.size() == 1);
  REQUIRE(same_platform[0].severity == kError);
}

TEST_CASE("C++ definitions: class members are matched through their scope", "[jot]")
{
  const std::string header = "namespace app {\n"
                             "class Widget {\n"
                             " public:\n"
                             "  Widget();\n"
                             "  ~Widget();\n"
                             "  Widget(const Widget &other);\n"
                             "  void reset();\n"
                             "  int count() const;\n"
                             "};\n"
                             "} // namespace app\n";
  const std::vector<Issue> complete = check(
      {{"widget.hpp", header},
       {"widget.cpp",
        "app::Widget::Widget() : count_(0) {}\n"
        "app::Widget::~Widget() = default;\n"
        "app::Widget::Widget(const Widget &other) : count_(other.count_) {}\n"
        "void app::Widget::reset() {}\n"
        "int app::Widget::count() const { return count_; }\n"}});
  REQUIRE(complete.empty());

  // Drop one body: exactly that one is reported, at the declaration.
  const std::vector<Issue> missing =
      check({{"widget.hpp", header},
             {"widget.cpp",
              "app::Widget::Widget() : count_(0) {}\n"
              "app::Widget::~Widget() = default;\n"
              "app::Widget::Widget(const Widget &other) : count_(other.count_) {}\n"
              "int app::Widget::count() const { return count_; }\n"}});
  REQUIRE(missing.size() == 1);
  REQUIRE(issue_for(missing, "\"app::Widget::reset()\"") != nullptr);
  REQUIRE(missing[0].line == 6);
}

TEST_CASE("C++ definitions: definitions inside the class body count", "[jot]")
{
  const std::vector<Issue> issues = check({{"widget.hpp",
                                           "class Widget {\n"
                                           " public:\n"
                                           "  void reset() {}\n"
                                           "  int count() const { return 0; }\n"
                                           "};\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: = 0, = delete and = default need no body", "[jot]")
{
  const std::vector<Issue> issues = check({{"widget.hpp",
                                           "class Widget {\n"
                                           " public:\n"
                                           "  Widget() = default;\n"
                                           "  Widget(const Widget &) = delete;\n"
                                           "  virtual void run() = 0;\n"
                                           "  virtual ~Widget() = default;\n"
                                           "};\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: two bodies for one signature is an error", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"a.cpp", "int helper(int x) { return x; }\n"},
             {"b.cpp", "int helper(int value) { return value + 1; }\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kError);
  REQUIRE(issues[0].file.find("b.cpp") != std::string::npos);
  REQUIRE(issues[0].message.find("first definition at") != std::string::npos);
  REQUIRE(issues[0].message.find("a.cpp:1") != std::string::npos);
}

TEST_CASE("C++ definitions: the same signature twice in one file is a redefinition", "[jot]")
{
  const std::vector<Issue> issues = check({{"a.cpp", "void f() {}\nvoid f() {}\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kError);
  REQUIRE(issues[0].line == 1);
  REQUIRE(issues[0].message.find("more than once in this file (also at line 1)") != std::string::npos);
}

TEST_CASE("C++ definitions: a file-scope main is each program's own entry point", "[jot]")
{
  // A folder of standalone programs (competitive programming, samples, probes) is
  // compiled one file at a time, so a second `main` never reaches the same
  // linker invocation and is not a duplicate.
  const std::vector<Issue> standalone =
      check({{"first.cpp", "int main() { return 0; }\n"},
             {"second.cpp", "int main() { return 1; }\n"},
             {"third.cpp", "int main(int argc, char **argv) { return argc; }\n"}});
  REQUIRE(standalone.empty());

  // The exemption is about the entry point, not about matching-signature pairs:
  // two ordinary file-scope functions with one signature are still reported.
  const std::vector<Issue> helpers =
      check({{"first.cpp", "int helper() { return 0; }\n"},
             {"second.cpp", "int helper() { return 1; }\n"}});
  REQUIRE(helpers.size() == 1);
  REQUIRE(helpers[0].severity == kError);

  // When *every* file in the workspace carries its own entry point, the files
  // are compiled one at a time and no two of them ever share a link, so a
  // repeated helper is not reported either (the shape a competitive-programming
  // folder has: one `solution.c` per problem, each with its own `print_all`).
  const std::vector<Issue> programs =
      check({{"bubble.cpp", "int main() { return 0; }\nint print_all() { return 1; }\n"},
             {"selection.cpp", "int main() { return 0; }\nint print_all() { return 2; }\n"}});
  REQUIRE(programs.empty());

  // One file without an entry point and the check is back: that file can be
  // linked with either program, so a helper it shares with one of them is the
  // linker's error again.
  const std::vector<Issue> mixed =
      check({{"bubble.cpp", "int main() { return 0; }\nint print_all() { return 1; }\n"},
             {"helper.cpp", "int print_all() { return 2; }\n"}});
  REQUIRE(mixed.size() == 1);
  REQUIRE(mixed[0].severity == kError);

  // Turning the cross-file check off does not turn the same-file rule off: two
  // bodies in one program file are a redefinition however the folder builds.
  const std::vector<Issue> repeated_in_one =
      check({{"bubble.cpp",
              "int main() { return 0; }\nint print_all() { return 1; }\n"
              "int print_all() { return 2; }\n"}});
  REQUIRE(repeated_in_one.size() == 1);
  REQUIRE(repeated_in_one[0].severity == kError);
  REQUIRE(repeated_in_one[0].message.find("more than once in this file") != std::string::npos);

  // A `main` under a namespace is an ordinary function: the rule still applies.
  const std::vector<Issue> namespaced =
      check({{"a.cpp", "namespace app { int main() { return 1; } }\n"},
             {"b.cpp", "namespace app { int main() { return 2; } }\n"}});
  REQUIRE(namespaced.size() == 1);
  REQUIRE(namespaced[0].severity == kError);

  // And one file cannot hold two entry points either.
  const std::vector<Issue> twice =
      check({{"one.cpp", "int main() { return 0; }\nint main() { return 1; }\n"}});
  REQUIRE(twice.size() == 1);
  REQUIRE(twice[0].severity == kError);
  REQUIRE(twice[0].message.find("more than once in this file") != std::string::npos);
}

TEST_CASE("C++ definitions: inline, template and static bodies may repeat", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"inline.hpp", "inline int step() { return 1; }\n"},
             {"inline_other.hpp", "inline int step() { return 2; }\n"},
             {"tmpl.hpp", "template <class T> T pick(T v) { return v; }\n"},
             {"tmpl_other.hpp", "template <class T> T pick(T v) { return v; }\n"},
             {"a.cpp", "static int local() { return 1; }\n"},
             {"b.cpp", "static int local() { return 2; }\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: a non-inline body in a header is a hint", "[jot]")
{
  const std::vector<Issue> issues = check({{"util.hpp", "int helper() { return 1; }\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kInfo);
  REQUIRE(issues[0].message.find("defined in a header without inline") != std::string::npos);
}

TEST_CASE("C++ definitions: calls and initializers are not declarations", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"app.cpp",
              "static Config kConfig(1, 2);\n"
              "static const std::string kName(\"x\");\n"
              "using Callback = void (*)(int);\n"
              "typedef int (*Other)(int);\n"
              "void (*fp)(int);\n"
              "std::function<void(int)> handler;\n"
              "\n"
              "void run() {\n"
              "  helper(1);\n"
              "  process(name);\n"
              "  if (ready()) { emit(); }\n"
              "  for (auto &item : items) { consume(item); }\n"
              "}\n"
              "\n"
              "void real(int x) { (void)x; }\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: raw string literals are not code", "[jot]")
{
  // A raw string has no escapes, so every quote inside it is content. Reading
  // its first quote as the string's end made the rest of the literal parse as
  // code -- phantom declarations, and duplicate bodies for the `main()`s that
  // test fixtures write inside their JSON.
  const std::vector<Issue> issues = check(
      {{"client.hpp", "int real_function();\n"},
       {"client.cpp",
        "std::string payload = R\"({\"changes\":{\")\" + uri + R\"(\":[{\"newText\":\"x\"}]}})\";\n"
        "std::string doc = R\"json(int phantom() { return 1; } \"quoted\")json\";\n"
        "int real_function() { return 0; }\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: function pointers and casts are not declarations", "[jot]")
{
  const std::vector<Issue> issues = check({{"util.hpp",
                                           "typedef void (*Callback)(int);\n"
                                           "using Handler = int (*)(int, int);\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: operators are matched by their spelling", "[jot]")
{
  const std::string header = "class Vec {\n"
                             " public:\n"
                             "  Vec &operator=(const Vec &other);\n"
                             "  bool operator<(const Vec &other) const;\n"
                             "  int operator()(int index) const;\n"
                             "  explicit operator bool() const;\n"
                             "  Vec operator+(const Vec &other) const;\n"
                             "};\n";
  const std::vector<Issue> complete =
      check({{"vec.hpp", header},
             {"vec.cpp",
              "Vec &Vec::operator=(const Vec &rhs) { return *this; }\n"
              "bool Vec::operator<(const Vec &rhs) const { return false; }\n"
              "int Vec::operator()(int i) const { return i; }\n"
              "Vec::operator bool() const { return true; }\n"
              "Vec Vec::operator+(const Vec &rhs) const { return rhs; }\n"}});
  REQUIRE(complete.empty());

  const std::vector<Issue> missing = check({{"vec.hpp", header},
                                            {"vec.cpp",
                                             "Vec &Vec::operator=(const Vec &rhs) { return *this; }\n"
                                             "bool Vec::operator<(const Vec &rhs) const { return false; }\n"
                                             "int Vec::operator()(int i) const { return i; }\n"
                                             "Vec::operator bool() const { return true; }\n"}});
  REQUIRE(missing.size() == 1);
  REQUIRE(issue_for(missing, "operator+(const Vec& other) const") != nullptr);
}

TEST_CASE("C++ definitions: templates are only a hint when missing", "[jot]")
{
  const std::vector<Issue> issues = check({{"pick.hpp", "template <class T> T pick(T value);\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kInfo);
  REQUIRE(issues[0].message.find("pick(T value)") != std::string::npos);
}

TEST_CASE("C++ definitions: an include guard does not make everything conditional", "[jot]")
{
  const std::string guarded = "#ifndef WIDGET_H\n"
                              "#define WIDGET_H\n"
                              "void reset();\n"
                              "#endif\n";
  const std::vector<Issue> satisfied = check({{"widget.hpp", guarded}, {"widget.cpp", "void reset() {}\n"}});
  REQUIRE(satisfied.empty());

  // A real conditional is reported, but only as a hint: the declaration may not
  // exist in this configuration.
  const std::vector<Issue> conditional =
      check({{"widget.hpp",
              "#ifndef WIDGET_H\n"
              "#define WIDGET_H\n"
              "#ifdef _WIN32\n"
              "void win_only();\n"
              "#endif\n"
              "#endif\n"}});
  REQUIRE(conditional.size() == 1);
  REQUIRE(conditional[0].severity == kInfo);
  REQUIRE(conditional[0].message.find("win_only()") != std::string::npos);
}

TEST_CASE("C++ definitions: another scope's body is not this declaration's", "[jot]")
{
  // `a::f()` and `b::f()` are two functions, so the missing one is reported as
  // missing, not as "found elsewhere".
  const std::vector<Issue> unrelated = check({{"a.hpp", "namespace a { void f(); }\n"},
                                              {"b.cpp", "namespace b { void f() {} }\n"}});
  REQUIRE(unrelated.size() == 1);
  REQUIRE(unrelated[0].severity == kWarning);
  REQUIRE(unrelated[0].message.find("No definition found for \"a::f()\"") != std::string::npos);

  // Two scopes that do share a component are the qualification the parser could
  // not join up, so that one is only a hint.
  const std::vector<Issue> related =
      check({{"outer.hpp", "namespace ns { void f(); }\n"},
             {"outer.cpp", "void f() {}\n"}});
  REQUIRE(related.size() == 1);
  REQUIRE(related[0].severity == kInfo);
  REQUIRE(related[0].message.find("no definition in scope \"ns\"") != std::string::npos);
  REQUIRE(related[0].message.find("file scope") != std::string::npos);
}

TEST_CASE("C++ definitions: a changed signature points at the near miss", "[jot]")
{
  const std::vector<Issue> issues = check({{"rect.hpp", "double area(int w, int h);\n"},
                                           {"rect.cpp", "double area(double w, double h) { return w * h; }\n"}});
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == kWarning);
  REQUIRE(issues[0].message.find("a definition with different parameters is at") != std::string::npos);
  REQUIRE(issues[0].message.find("rect.cpp:1") != std::string::npos);
}

TEST_CASE("C++ definitions: nested classes and namespaces keep their scope", "[jot]")
{
  const std::vector<Issue> issues =
      check({{"outer.hpp",
              "namespace ns {\n"
              "class Outer {\n"
              " public:\n"
              "  class Inner {\n"
              "   public:\n"
              "    void go();\n"
              "  };\n"
              "  void run();\n"
              "};\n"
              "} // namespace ns\n"},
             {"outer.cpp", "void ns::Outer::run() {}\nvoid ns::Outer::Inner::go() {}\n"}});
  REQUIRE(issues.empty());
}

TEST_CASE("C++ definitions: a scan walks a workspace and skips build trees", "[jot]")
{
  char dir[] = "/tmp/jot_cpp_defs_ws_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  fs::create_directories(root / "src");
  fs::create_directories(root / "build" / "src");
  {
    std::ofstream out(root / "src" / "widget.hpp");
    out << "class Widget {\n public:\n  void reset();\n  int count() const;\n};\n";
  }
  {
    std::ofstream out(root / "src" / "widget.cpp");
    out << "void Widget::reset() {}\n";
  }
  {
    // A build tree's copy must not be scanned, and must not make the source
    // pair look duplicated.
    std::ofstream out(root / "build" / "src" / "widget.cpp");
    out << "void Widget::reset() {}\nint Widget::count() const { return 0; }\n";
  }

  const CppDefinitions::ScanResult result = CppDefinitions::scan_workspace(root.string());
  INFO(describe(result.issues));
  REQUIRE(result.stats.files_scanned == 2);
  REQUIRE(result.issues.size() == 1);
  REQUIRE(issue_for(result.issues, "count() const") != nullptr);
  REQUIRE(result.stats.missing == 1);
  REQUIRE(result.stats.definitions >= 1);
}

TEST_CASE("C++ definitions: a scan survives a directory that does not exist", "[jot]")
{
  const CppDefinitions::ScanResult result =
      CppDefinitions::scan_workspace("/tmp/jot_cpp_defs_missing_workspace");
  REQUIRE(result.issues.empty());
  REQUIRE(result.stats.files_scanned == 0);
}

// The editor half: a landed scan has to reach the per-file diagnostics store
// every surface reads (the merge, the badges, the picker), not just the stats.
TEST_CASE("C++ definitions: a landed scan feeds the diagnostics store", "[jot]")
{
  seed_config_home();
  char dir[] = "/tmp/jot_cpp_defs_editor_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "widget.hpp");
    out << "#pragma once\nvoid reset();\n";
  }
  {
    std::ofstream out(root / "widget.cpp");
    out << "#include \"widget.hpp\"\nvoid reset() {}\nint twice() { return 1; }\n";
  }
  {
    std::ofstream out(root / "other.cpp");
    out << "int twice() { return 2; }\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  e.run_cpp_definitions_scan_for_test();

  REQUIRE(e.cpp_definitions_files_for_test() == 3);
  REQUIRE(e.cpp_definitions_missing_for_test() == 0);
  REQUIRE(e.cpp_definitions_duplicates_for_test() == 1);
  // The repeated body is reported on the second definition in path order (the
  // pair is other.cpp then widget.cpp), so that file's diagnostics hold the
  // error and the ones that carry the first body or only the declaration stay
  // clean.
  REQUIRE(e.diagnostics_count_for_test((root / "widget.cpp").string()) == 1);
  REQUIRE(e.diagnostics_count_for_test((root / "other.cpp").string()) == 0);
  REQUIRE(e.diagnostics_count_for_test((root / "widget.hpp").string()) == 0);
  const std::string summary = e.cpp_definitions_summary_for_test();
  REQUIRE(summary.find("1 repeated") != std::string::npos);
  REQUIRE(summary.find("across 3 files") != std::string::npos);
}

// The Problems view leads with a header: the totals, the severities and how many
// findings each file holds, in that order. It is what tells a panel whether the
// scan found one thing in one file or forty across the tree.
TEST_CASE("C++ definitions: the Problems header tallies the findings per file", "[jot]")
{
  seed_config_home();
  char dir[] = "/tmp/jot_cpp_defs_header_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "widget.hpp");
    out << "#pragma once\nvoid reset();\nint missing();\n";
  }
  {
    std::ofstream out(root / "widget.cpp");
    out << "#include \"widget.hpp\"\nvoid reset() {}\nint twice() { return 1; }\n";
  }
  {
    std::ofstream out(root / "other.cpp");
    out << "int twice() { return 2; }\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  e.run_cpp_definitions_scan_for_test();

  // Two findings, one per file: the repeated body on the second definition in
  // path order (widget.cpp) and the header's unimplemented declaration.
  const auto findings = e.cpp_definition_findings_for_test();
  REQUIRE(findings.size() == 2);
  REQUIRE(findings[0].filepath == (root / "widget.cpp").string());
  REQUIRE(findings[0].severity == kError);
  REQUIRE(findings[1].filepath == (root / "widget.hpp").string());
  REQUIRE(findings[1].severity == kWarning);

  const std::string header = e.problems_header_for_test(120);
  REQUIRE(header.find("2 findings in 2 files") != std::string::npos);
  REQUIRE(header.find("1 error, 1 warning") != std::string::npos);
  REQUIRE(header.find("widget.cpp 1") != std::string::npos);
  REQUIRE(header.find("widget.hpp 1") != std::string::npos);
  // A narrow panel keeps the beginning and loses the tail, never a broken
  // multi-byte cell (the separator is two cells wide).
  const std::string narrow = e.problems_header_for_test(24);
  REQUIRE(ui_cell_count(narrow) <= 24);
  REQUIRE(narrow.find("2 findings") != std::string::npos);
}

TEST_CASE("C++ definitions: next and previous walk the findings", "[jot]")
{
  seed_config_home();
  char dir[] = "/tmp/jot_cpp_defs_jump_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "widget.hpp");
    out << "#pragma once\nvoid reset();\nint missing();\n";
  }
  {
    std::ofstream out(root / "widget.cpp");
    out << "#include \"widget.hpp\"\nvoid reset() {}\nint twice() { return 1; }\n";
  }
  {
    std::ofstream out(root / "other.cpp");
    out << "int twice() { return 2; }\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  e.run_cpp_definitions_scan_for_test();

  auto at = [&]() -> std::string
  {
    return fs::path(e.buffer_for_test().filepath).filename().string() + ":"
           + std::to_string(e.buffer_for_test().cursor.y + 1) + ":"
           + std::to_string(e.buffer_for_test().cursor.x + 1);
  };

  // Starting in the header above both findings, `next` lands on this file's own
  // finding first -- the walk is by file and line, not by severity.
  e.load_file((root / "widget.hpp").string());
  e.scroll_cursor_to_for_test(0, 0);
  REQUIRE(e.cpp_definitions_jump_for_test(1));
  REQUIRE(at() == "widget.hpp:3:5");

  // From there the next one is in the file that sorts before this one (nothing
  // follows widget.hpp), and it wraps around to the first finding.
  REQUIRE(e.cpp_definitions_jump_for_test(1));
  REQUIRE(at() == "widget.cpp:3:5");

  // `prev` walks back the way it came.
  REQUIRE(e.cpp_definitions_jump_for_test(-1));
  REQUIRE(at() == "widget.hpp:3:5");
  REQUIRE(e.cpp_definitions_jump_for_test(-1));
  REQUIRE(at() == "widget.cpp:3:5");
}

// The signature is what decides whether an unsaved buffer is worth a scan, so
// what it must not see is an edit that no finding can move between, and what it
// must see is every one that can.
TEST_CASE("C++ definitions: a signature moves for a declaration and not for a body's text", "[jot]")
{
  const std::string base = "#include <stdio.h>\nvoid render();\nint main() { return 0; }\n";
  const std::string start = CppDefinitions::signature_of("a.c", base);
  REQUIRE(start == CppDefinitions::signature_of("a.c", base));

  // Everything inside a body, and everything around one, is the parser's
  // business but not a finding's.
  const std::string edited_body = "#include <stdio.h>\nvoid render();\n"
                                  "int main() { int total = 0; return total; }\n";
  REQUIRE(CppDefinitions::signature_of("a.c", edited_body) == start);
  REQUIRE(CppDefinitions::signature_of("a.c", "\n" + base) != start);

  // A declaration appearing, and a body arriving for it, are the two moves the
  // checks exist for.
  REQUIRE(CppDefinitions::signature_of("a.c", "int main() { return 0; }\n") != start);
  REQUIRE(CppDefinitions::signature_of("a.c",
                                       "#include <stdio.h>\nvoid render() {}\n"
                                       "int main() { return 0; }\n")
          != start);

  // Which file it is decides which rule applies to it, so the path is part of
  // it: the same text in a header is a declaration another file may implement.
  REQUIRE(CppDefinitions::signature_of("a.hpp", base) != start);
}

TEST_CASE("C++ definitions: an unsaved buffer's text is what the scan reads", "[jot]")
{
  char dir[] = "/tmp/jot_cpp_defs_unsaved_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "linkedlist.c");
    out << "#include <stdio.h>\n\nint main() {}\n";
  }

  // On disk there is nothing to report: the walk sees a program with a body and
  // no declarations.
  CppDefinitions::ScanResult result = CppDefinitions::scan_workspace(root.string());
  REQUIRE(result.stats.files_scanned == 1);
  REQUIRE(result.issues.empty());

  // The editor's text says otherwise, and it is what a scan has to be handed:
  // the walk reads the file the last time it was written.
  std::map<std::string, std::string> unsaved;
  unsaved[(root / "linkedlist.c").string()] =
      "#include <stdio.h>\n\nvoid render();\n\nint main() {}\n";
  result = CppDefinitions::scan_workspace(root.string(), CppDefinitions::ScanLimits(), unsaved);
  REQUIRE(result.issues.size() == 1);
  REQUIRE(result.issues[0].file == (root / "linkedlist.c").string());
  REQUIRE(result.issues[0].line == 2);
  REQUIRE(result.issues[0].col == 5);
  REQUIRE(result.issues[0].end_col == 11); // one past `render`, the whole name

  // A buffer whose file the walk never reached -- written into existence -- is
  // part of the workspace all the same, and one outside it is not.
  unsaved.clear();
  unsaved[(root / "fresh.c").string()] = "void fresh();\n";
  result = CppDefinitions::scan_workspace(root.string(), CppDefinitions::ScanLimits(), unsaved);
  REQUIRE(result.issues.size() == 1);
  REQUIRE(result.issues[0].file == (root / "fresh.c").string());

  unsaved.clear();
  unsaved["/elsewhere/other.c"] = "void elsewhere();\n";
  result = CppDefinitions::scan_workspace(root.string(), CppDefinitions::ScanLimits(), unsaved);
  REQUIRE(result.issues.empty());
}

TEST_CASE("C++ definitions: a declaration typed and not saved is reported", "[jot]")
{
  seed_config_home();
  char dir[] = "/tmp/jot_cpp_defs_dirty_XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const fs::path root = fs::path(dir);
  {
    std::ofstream out(root / "linkedlist.c");
    out << "#include <stdio.h>\n\nint main() {}\n";
  }

  Editor e;
  e.set_home_menu_visible(false);
  e.open_workspace(root.string(), false);
  e.load_file((root / "linkedlist.c").string());
  e.run_cpp_definitions_scan_for_test();
  REQUIRE(e.cpp_definitions_missing_for_test() == 0);
  REQUIRE(e.cpp_definitions_dirty_changed_for_test() == false);

  // The declaration lands in the buffer, not on disk: the checks read the
  // workspace from disk, so only the unsaved-text path can see it.
  e.host().core.set_buffer_content("#include <stdio.h>\n\nvoid render();\n\nint main() {}\n");
  REQUIRE(e.cpp_definitions_dirty_changed_for_test() == true);
  // The poll asked once: a second ask with nothing typed is not a new question.
  REQUIRE(e.cpp_definitions_dirty_changed_for_test() == false);
  e.run_cpp_definitions_scan_for_test();
  REQUIRE(e.cpp_definitions_missing_for_test() == 1);
  e.age_lsp_typing_for_test((root / "linkedlist.c").string(), 5001);
  e.paint_held_lsp_diagnostics_for_test();
  REQUIRE(e.diagnostics_count_for_test((root / "linkedlist.c").string()) == 1);

  // Typing inside the body is not a signature change, so it asks for nothing.
  e.host().core.set_buffer_content("#include <stdio.h>\n\nvoid render();\n\n"
                                   "int main() { return 0; }\n");
  REQUIRE(e.cpp_definitions_dirty_changed_for_test() == false);

  // A body arriving for it clears the finding, again without a save.
  e.host().core.set_buffer_content("#include <stdio.h>\n\nvoid render() {}\n\nint main() {}\n");
  REQUIRE(e.cpp_definitions_dirty_changed_for_test() == true);
  e.run_cpp_definitions_scan_for_test();
  REQUIRE(e.cpp_definitions_missing_for_test() == 0);
  e.age_lsp_typing_for_test((root / "linkedlist.c").string(), 5001);
  e.paint_held_lsp_diagnostics_for_test();
  REQUIRE(e.diagnostics_count_for_test((root / "linkedlist.c").string()) == 0);
}

TEST_CASE("C++ definitions: workspace classification visits each record once", "[jot]")
{
  std::vector<FunctionRecord> records;
  for (int i = 0; i < 2000; ++i)
  {
    const std::string name = "helper_" + std::to_string(i);
    auto parsed = CppDefinitions::parse_file("unit.cpp", "void " + name + "() {}\n");
    records.insert(records.end(), parsed.begin(), parsed.end());
  }
  CppDefinitions::ScanStats stats;
  REQUIRE(CppDefinitions::analyze(records, &stats).empty());
  REQUIRE(stats.definitions == 2000);
  REQUIRE(stats.classification_records == (long long)records.size());
}

// The scan runs over this project's own sources on every open and save, so a
// repeated body the checker finds here is one the owner sees while editing. The
// tree has none: the helper written once per layer (the same right-hand
// truncation in the home screen and in the frame) sits in an anonymous namespace
// in each, which is the linker's business and not this rule's.
TEST_CASE("C++ definitions: the project's own sources define each body once", "[jot]")
{
  const CppDefinitions::ScanResult result = CppDefinitions::scan_workspace(JOT_SOURCE_DIR);
  std::vector<CppDefinitions::Issue> duplicates;
  for (const CppDefinitions::Issue &issue : result.issues)
  {
    if (issue.message.find("defined more than once") != std::string::npos)
    {
      duplicates.push_back(issue);
    }
  }
  INFO(describe(duplicates));
  REQUIRE(duplicates.empty());
}
