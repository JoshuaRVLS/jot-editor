// Save hygiene (src/features/save_hygiene.*, wired into Editor::save_buffer_at
// in src/editor/app/file.cpp), and the atomic write under it
// (src/base/file_util.*).
//
// Three parts. The rules are pure, so each is asserted directly: what counts as
// trailing whitespace, what comes off, and which files have to keep theirs. The
// write helper is exercised on real files in /tmp, where the case that matters
// is the file that is already there: it is replaced, it keeps its mode, and a
// write that cannot happen leaves it exactly as it was. The editor cases pin
// the save itself, since the buffer and the bytes have to agree afterwards.

#include "base/file_util.h"
#include "editor.h"
#include "workspace/save_hygiene.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  std::string scratch_path(const std::string &name)
  {
    static int counter = 0;
    return "/tmp/jot_save_hygiene_" + std::to_string(::getpid()) + "_"
           + std::to_string(counter++) + "_" + name;
  }

  void write_text(const std::string &path, const std::string &text)
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
  }

  std::string read_back(const std::string &path)
  {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  }

  unsigned int mode_of(const std::string &path)
  {
    struct stat st
    {
    };
    if (::stat(path.c_str(), &st) != 0)
    {
      return 0;
    }
    return static_cast<unsigned int>(st.st_mode & 07777);
  }

  // The text a buffer would be saved as, so a case can pin the buffer as well
  // as the file on disk.
  std::string buffer_text(const FileBuffer &buf)
  {
    std::string text;
    for (const std::string &line : buf.lines)
    {
      text += line;
      text += '\n';
    }
    return text;
  }

  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_save_test_XXXXXX";
      mkdtemp(cfgdir);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // An editor that saves what it is given rather than what a formatter makes of
  // it: the two on-save formatters are off, so the bytes are this test's.
  Editor &saving_editor(bool trim_on_save)
  {
    Editor &e = probe_editor();
    e.set_home_menu_visible(false);
    e.set_config_for_test("clang_format_on_save", "false");
    e.set_config_for_test("prettier_on_save", "false");
    e.set_config_for_test("trim_trailing_whitespace_on_save", trim_on_save ? "true" : "false");
    return e;
  }

  // A save with the built-in reindent on or off at a known step, the external
  // formatters always off: what lands on disk is the reindent's own output.
  Editor &reindent_editor(bool on, int tab_size)
  {
    Editor &e = saving_editor(true);
    e.set_config_for_test("tab_size", std::to_string(tab_size));
    e.set_config_for_test("reindent_on_save", on ? "true" : "false");
    e.apply_config_live_for_test();
    return e;
  }

  int buffer_index_for(Editor &e, const std::string &path)
  {
    for (int i = 0; i < e.buffer_count_for_test(); i++)
    {
      if (e.buffer_for_test(i).filepath == path)
      {
        return i;
      }
    }
    return -1;
  }
} // namespace

TEST_CASE("Save hygiene: a line ends in spaces or tabs, and nothing else", "[jot][save]")
{
  REQUIRE(SaveHygiene::has_trailing_whitespace("int a = 1; "));
  REQUIRE(SaveHygiene::has_trailing_whitespace("int a = 1;\t"));
  REQUIRE(SaveHygiene::has_trailing_whitespace("   "));
  REQUIRE_FALSE(SaveHygiene::has_trailing_whitespace("int a = 1;"));
  REQUIRE_FALSE(SaveHygiene::has_trailing_whitespace(""));

  REQUIRE(SaveHygiene::trim_trailing_whitespace("int a = 1;  \t ") == "int a = 1;");
  REQUIRE(SaveHygiene::trim_trailing_whitespace("    ") == "");
  REQUIRE(SaveHygiene::trim_trailing_whitespace("\t\t") == "");
  // Whitespace inside the line is content, and stays.
  REQUIRE(SaveHygiene::trim_trailing_whitespace("a  =  b") == "a  =  b");
  REQUIRE(SaveHygiene::trim_trailing_whitespace("a") == "a");
}

TEST_CASE("Save hygiene: markdown keeps the spaces that mean a line break", "[jot][save]")
{
  REQUIRE(SaveHygiene::preserves_trailing_whitespace("notes.md"));
  REQUIRE(SaveHygiene::preserves_trailing_whitespace("docs/README.MARKDOWN"));
  REQUIRE(SaveHygiene::preserves_trailing_whitespace("/tmp/a/b.mdx"));
  REQUIRE_FALSE(SaveHygiene::preserves_trailing_whitespace("main.cpp"));
  REQUIRE_FALSE(SaveHygiene::preserves_trailing_whitespace("Makefile"));
  // A dot in a directory name is not an extension, and the nearest dot wins.
  REQUIRE_FALSE(SaveHygiene::preserves_trailing_whitespace("/tmp/a.md/README"));
  REQUIRE(SaveHygiene::preserves_trailing_whitespace("/tmp/a.md/notes.md"));
}

TEST_CASE("Atomic save: an existing file is replaced and keeps its mode", "[jot][save]")
{
  const std::string path = scratch_path("keeps-mode.txt");
  write_text(path, "old\n");
  ::chmod(path.c_str(), 0640);

  REQUIRE(file_util::write_file_atomic(path, "new\n").empty());
  REQUIRE(read_back(path) == "new\n");
  // The temporary a fresh file would have created is not allowed to turn a
  // group-readable file into a private one.
  REQUIRE(mode_of(path) == 0640);
  REQUIRE_FALSE(fs::exists(path + ".jot-saving"));

  // And the swap works again on the file it just created.
  REQUIRE(file_util::write_file_atomic(path, "one more\n").empty());
  REQUIRE(read_back(path) == "one more\n");
  std::remove(path.c_str());
}

TEST_CASE("Atomic save: a write that cannot happen leaves the file alone", "[jot][save]")
{
  // A path whose directory does not exist: nothing is created anywhere, and no
  // temporary is left behind beside a file that was never there.
  const std::string missing = scratch_path("no-such-dir/keep.txt");
  REQUIRE_FALSE(file_util::write_file_atomic(missing, "x\n").empty());
  REQUIRE_FALSE(fs::exists(missing));
  REQUIRE_FALSE(fs::exists(missing + ".jot-saving"));

  // A directory that refuses the temporary: the file that was there keeps every
  // byte rather than being truncated by the attempt. Root ignores the mode, so
  // this half only means something as an ordinary user.
  if (::geteuid() != 0)
  {
    const std::string dir = scratch_path("readonly-dir");
    fs::create_directory(dir);
    const std::string path = dir + "/keep.txt";
    write_text(path, "old\n");
    ::chmod(dir.c_str(), 0500);

    REQUIRE_FALSE(file_util::write_file_atomic(path, "new\n").empty());
    REQUIRE(read_back(path) == "old\n");
    REQUIRE_FALSE(fs::exists(path + ".jot-saving"));

    ::chmod(dir.c_str(), 0700);
    std::remove(path.c_str());
    fs::remove(dir);
  }
}

TEST_CASE("Save: trailing whitespace leaves both the buffer and the file", "[jot][save]")
{
  Editor &e = saving_editor(true);
  const std::string path = scratch_path("trim.cpp");
  write_text(path, "int a = 1;   \nint b = 2;\t\n");
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  REQUIRE(read_back(path) == "int a = 1;\nint b = 2;\n");
  // What is on screen is what landed on disk: a save that wrote trimmed bytes
  // over an untrimmed buffer would leave the file and the window disagreeing.
  REQUIRE(buffer_text(e.buffer_for_test(index)) == "int a = 1;\nint b = 2;\n");
  std::remove(path.c_str());
}

TEST_CASE("Save: a trimmed line leaves the syntax highlighting where it was", "[jot][save]")
{
  Editor &e = saving_editor(true);
  const std::string path = scratch_path("trim-syntax.cpp");
  // Lines of two shapes, so a highlight shifted by a whole token lands on
  // something the eye (and this case) can tell apart.
  std::string text;
  for (int i = 1; i <= 300; i++)
  {
    text += (i % 2 == 0) ? ("int value" + std::to_string(i) + " = " + std::to_string(i) + ";")
                         : ("// note " + std::to_string(i) + " with words");
    if (i == 1)
    {
      text += std::string(24, ' ');
    }
    text += '\n';
  }
  write_text(path, text);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  if (!e.syntax_tree_ready_for_test())
  {
    SKIP("no tree-sitter grammar for C++ in this build");
  }

  REQUIRE(e.save_buffer_for_test(index, false));
  const std::string saved = read_back(path);
  REQUIRE(saved.find(";  ") == std::string::npos);

  // Both sides of the comparison are first paints of the same bytes, so the
  // per-line cache cannot paper over the difference: the buffer the save left
  // behind, and a buffer opened from what the save wrote. The line is one of
  // the declaration ones, whose first cells carry the type color and whose next
  // word does not, so a shifted highlight is unmistakable.
  const int probe_line = 199;
  const std::vector<std::pair<int, int>> after_trim = e.line_syntax_colors_for_test(probe_line);
  const std::string copy = scratch_path("trim-syntax-copy.cpp");
  write_text(copy, saved);
  e.load_file(copy);
  const std::vector<std::pair<int, int>> fresh = e.line_syntax_colors_for_test(probe_line);

  REQUIRE_FALSE(fresh.empty());
  REQUIRE(fresh[0] != fresh[5]);
  REQUIRE(after_trim == fresh);
  std::remove(path.c_str());
  std::remove(copy.c_str());
}

TEST_CASE("Save: the setting off leaves every space where it was", "[jot][save]")
{
  Editor &e = saving_editor(false);
  const std::string path = scratch_path("keep.cpp");
  const std::string text = "int a = 1;   \n";
  write_text(path, text);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  REQUIRE(read_back(path) == text);
  std::remove(path.c_str());
}

TEST_CASE("Reindent: the languages with block rules, and no others", "[jot][save]")
{
  for (const char *path :
       {"a.cpp",  "/tmp/deep/a.HPP", "a.py",     "a.lua",   "a.rs",     "a.go",  "a.swift",
        "a.js",   "a.jsx",           "a.ts",     "a.tsx",   "a.java",   "a.cs",  "a.kt",
        "a.dart", "a.scala",         "a.groovy", "a.proto", "a.thrift", "a.zig", "a.css",
        "a.scss", "a.less",          "a.json",   "a.jsonc"})
  {
    REQUIRE(SaveHygiene::supports_reindent(path));
  }
  // A keyword block (ruby, shell), a markup tree and a title comment are not
  // braces, so these rules do not read them.
  for (const char *path :
       {"notes.md", "a.rb", "a.sh", "a.html", "a.yaml", "a.toml", "Makefile", "/tmp/a.cpp/README"})
  {
    REQUIRE_FALSE(SaveHygiene::supports_reindent(path));
  }
}

TEST_CASE("Reindent: a C++ body written one level out is pulled back in", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "void f()", "{", "foo();", "if (x)", "{", "bar();", "}", "}"};
  const std::vector<std::string> want = {
      "void f()", "{", "  foo();", "  if (x)", "  {", "    bar();", "  }", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.cpp", 2) == want);
}

TEST_CASE("Reindent: a body at another width is rebased on the settings step", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "void f()", "{", "    foo();", "    if (x)", "    {", "        bar();", "    }", "}"};
  const std::vector<std::string> want = {
      "void f()", "{", "  foo();", "  if (x)", "  {", "    bar();", "  }", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.cpp", 2) == want);
}

TEST_CASE("Reindent: a fragment keeps the column it opens at", "[jot][save]")
{
  const std::vector<std::string> lines = {"    if (x)", "    {", "        foo();", "    }"};
  const std::vector<std::string> want = {"    if (x)", "    {", "      foo();", "    }"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.cpp", 2) == want);
}

TEST_CASE("Reindent: a wrapped expression keeps its own column", "[jot][save]")
{
  const std::vector<std::string> lines = {"int total = 1 +", "    2;", "int y = 3;"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.cpp", 2) == lines);
}

TEST_CASE("Reindent: a multi-line comment's own text is left alone", "[jot][save]")
{
  const std::vector<std::string> lines = {"void f()", "{", "  /* note", "     keep", "  */", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.cpp", 2) == lines);
}

TEST_CASE("Reindent: a JavaScript switch keeps its labels inside the braces", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "switch (x) {", "case 1:", "foo();", "break;", "default:", "bar();", "}"};
  const std::vector<std::string> want = {
      "switch (x) {", "  case 1:", "    foo();", "    break;", "  default:", "    bar();", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.js", 2) == want);
}

TEST_CASE("Reindent: JSX markup keeps the nesting its author gave it", "[jot][save]")
{
  const std::vector<std::string> lines = {"function App() {",
                                          "  return (",
                                          "    <div>",
                                          "      <span>hi</span>",
                                          "    </div>",
                                          "  );",
                                          "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.jsx", 2) == lines);
}

TEST_CASE("Reindent: a Go switch pulls its labels out to the switch", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "switch x {", "case 1:", "foo()", "default:", "bar()", "}"};
  const std::vector<std::string> want = {
      "switch x {", "case 1:", "    foo()", "default:", "    bar()", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.go", 4) == want);
}

TEST_CASE("Reindent: a Rust where clause keeps its bound list", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "fn f<T>(x: T) -> T", "where", "  T: Clone,", "{", "  x", "}"};
  const std::vector<std::string> want = {
      "fn f<T>(x: T) -> T", "where", "  T: Clone,", "{", "  x", "}"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.rs", 2) == want);
}

TEST_CASE("Reindent: a stylesheet and a JSON document step in at the braces", "[jot][save]")
{
  const std::vector<std::string> css = {
      ".card {", "color: red;", ".title {", "font-size: 1rem;", "}", "}"};
  const std::vector<std::string> css_want = {
      ".card {", "  color: red;", "  .title {", "    font-size: 1rem;", "  }", "}"};
  REQUIRE(SaveHygiene::reindent_lines(css, "a.css", 2) == css_want);

  const std::vector<std::string> json = {"{", "\"a\": {", "\"b\": [", "1,", "2", "],", "},", "}"};
  const std::vector<std::string> json_want = {
      "{", "  \"a\": {", "    \"b\": [", "      1,", "      2", "    ],", "  },", "}"};
  REQUIRE(SaveHygiene::reindent_lines(json, "a.json", 2) == json_want);
}

TEST_CASE("Reindent: a Lua block follows the same rules", "[jot][save]")
{
  const std::vector<std::string> lines = {"function f()", "return 1", "end"};
  const std::vector<std::string> want = {"function f()", "  return 1", "end"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.lua", 2) == want);
}

TEST_CASE("Reindent: a Lua long string's body is text", "[jot][save]")
{
  const std::vector<std::string> lines = {"local s = [[", "   keep   it", "]]"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.lua", 2) == lines);
}

TEST_CASE("Reindent: a Python block is rebased on the settings step", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "def f():", "    if x:", "        return 1", "    return 2"};
  const std::vector<std::string> want = {"def f():", "  if x:", "    return 1", "  return 2"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.py", 2) == want);
}

TEST_CASE("Reindent: a Python file already at the settings step is unchanged", "[jot][save]")
{
  const std::vector<std::string> lines = {"def f():", "  if x:", "    return 1", "  return 2"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.py", 2) == lines);
}

TEST_CASE("Reindent: a Python triple-quoted body is text, not indentation", "[jot][save]")
{
  const std::vector<std::string> lines = {
      "x = 1", "text = \"\"\"", "    keep   this", "\"\"\"", "y = 2"};
  REQUIRE(SaveHygiene::reindent_lines(lines, "a.py", 4) == lines);
}

TEST_CASE("Save: the built-in reindent lands in the buffer and the file", "[jot][save]")
{
  Editor &e = reindent_editor(true, 2);
  const std::string path = scratch_path("reindent.cpp");
  write_text(path, "void f()\n{\nfoo();\n}\n");
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  const std::string want = "void f()\n{\n  foo();\n}\n";
  REQUIRE(read_back(path) == want);
  // What is on screen is what landed on disk.
  REQUIRE(buffer_text(e.buffer_for_test(index)) == want);
  std::remove(path.c_str());
}

TEST_CASE("Save: the reindent uses the step in settings", "[jot][save]")
{
  Editor &e = reindent_editor(true, 4);
  const std::string path = scratch_path("reindent-width.cpp");
  write_text(path, "void f()\n{\n  foo();\n}\n");
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  REQUIRE(read_back(path) == "void f()\n{\n    foo();\n}\n");
  std::remove(path.c_str());
}

TEST_CASE("Save: the reindent setting off leaves the indentation alone", "[jot][save]")
{
  Editor &e = reindent_editor(false, 2);
  const std::string path = scratch_path("reindent-off.cpp");
  const std::string text = "void f()\n{\nfoo();\n}\n";
  write_text(path, text);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  REQUIRE(read_back(path) == text);
  std::remove(path.c_str());
}

TEST_CASE("Save: the built-in reindent is one undo step", "[jot][save][undo]")
{
  Editor &e = reindent_editor(true, 2);
  const std::string path = scratch_path("undo-reindent.cpp");
  const std::string original = "void f()\n{\nfoo();\n}\n";
  write_text(path, original);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  // The loaded buffer is the one the keys and the undo command act on
  // (core.buffer_content() joins the lines without a trailing newline).
  REQUIRE(e.host().core.buffer_content() == "void f()\n{\nfoo();\n}");

  // One typed edit ahead of the save, so the step the save takes is not the
  // only thing on the stack.
  e.host().core.set_cursor(2, 0);
  e.host().core.insert_char_at_carets('x');
  const std::string typed = "void f()\n{\nxfoo();\n}\n";
  REQUIRE(buffer_text(e.buffer_for_test(index)) == typed);

  REQUIRE(e.save_buffer_for_test(index, true));
  const std::string cleaned = "void f()\n{\n  xfoo();\n}\n";
  REQUIRE(read_back(path) == cleaned);
  REQUIRE(buffer_text(e.buffer_for_test(index)) == cleaned);

  // Ctrl+Z takes the save's own rewrite back first: the buffer holds the text
  // as it was typed, the file keeps what the save wrote, and the buffer reads
  // as unsaved because the two now disagree.
  e.host().core.undo();
  REQUIRE(buffer_text(e.buffer_for_test(index)) == typed);
  REQUIRE(e.buffer_for_test(index).modified);
  REQUIRE(read_back(path) == cleaned);

  // The press after that reaches the typed character: the cleanup took one
  // step rather than hiding the edit underneath it.
  e.host().core.undo();
  REQUIRE(buffer_text(e.buffer_for_test(index)) == original);
  std::remove(path.c_str());
}

TEST_CASE("Save: a clean file reindented on save is still undoable", "[jot][save][undo]")
{
  Editor &e = reindent_editor(true, 2);
  const std::string path = scratch_path("undo-clean.cpp");
  const std::string original = "void f()\n{\nfoo();\n}\n";
  write_text(path, original);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.host().core.buffer_content() == "void f()\n{\nfoo();\n}");
  REQUIRE_FALSE(e.buffer_for_test(index).modified);

  // Ctrl+S on a file nobody has edited: the reindent still lands in the buffer
  // and on disk, and it still has to be takeable back.
  REQUIRE(e.save_buffer_for_test(index, true));
  const std::string cleaned = "void f()\n{\n  foo();\n}\n";
  REQUIRE(read_back(path) == cleaned);
  REQUIRE(buffer_text(e.buffer_for_test(index)) == cleaned);
  REQUIRE_FALSE(e.buffer_for_test(index).modified);

  e.host().core.undo();
  REQUIRE(buffer_text(e.buffer_for_test(index)) == original);
  // The file holds the cleaned bytes, so what came back is an unsaved change:
  // without the dot a user could close the tab and drop the text they just
  // recovered without being asked, since the save had cleared the flag.
  REQUIRE(e.buffer_for_test(index).modified);
  REQUIRE(read_back(path) == cleaned);
  std::remove(path.c_str());
}

TEST_CASE("Save: the reindent setting off takes no undo step", "[jot][save][undo]")
{
  Editor &e = reindent_editor(false, 2);
  const std::string path = scratch_path("undo-reindent-off.cpp");
  const std::string original = "void f()\n{\nfoo();\n}\n";
  write_text(path, original);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  e.host().core.set_cursor(2, 0);
  e.host().core.insert_char_at_carets('x');
  REQUIRE(buffer_text(e.buffer_for_test(index)) == "void f()\n{\nxfoo();\n}\n");

  REQUIRE(e.save_buffer_for_test(index, true));
  REQUIRE(read_back(path) == "void f()\n{\nxfoo();\n}\n");

  // Nothing was rewritten, so the first press is the typed character's own
  // step: a save that changes no text must not push a state of its own and
  // swallow it.
  e.host().core.undo();
  REQUIRE(buffer_text(e.buffer_for_test(index)) == original);
  std::remove(path.c_str());
}

TEST_CASE("Save: markdown keeps the trailing spaces even with the setting on", "[jot][save]")
{
  Editor &e = saving_editor(true);
  const std::string path = scratch_path("hard-break.md");
  // Two trailing spaces are a hard line break in markdown, so this is content.
  const std::string text = "first line  \nsecond line\n";
  write_text(path, text);
  e.load_file(path);

  const int index = buffer_index_for(e, path);
  REQUIRE(index >= 0);
  REQUIRE(e.save_buffer_for_test(index, true));

  REQUIRE(read_back(path) == text);
  std::remove(path.c_str());
}
