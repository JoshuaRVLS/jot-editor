// The status-line prompts: the line the editor asks on at the bottom of the
// frame, and what a reader types into it -- :compile's command, an incremental
// search, and a find-file path.
//
// None of them parses what is typed. The compile session runs a shell command of
// the reader's own choosing -- `make`, `cmake --build build`, a pipeline -- in
// the workspace the editor has open, and the cases here run real ones: `sh
// ./build.sh` printing the error lines a compiler prints, then the jump paths
// (:cnext, and Enter on a row of the log) landing on the file the build named.
// The job has a terminal of its own, which is what lets the same prompt run a
// program: the cases below ask the job whether its output is a tty, hand it a
// stdin that answers, check that the colours a command prints arrive as spans
// instead of escape bytes, and pin what a rewritten row and a missing final
// newline leave in the log.
// An i-search moves as the query grows and Ctrl+S walks it; a find-file path
// completes the way a shell completes one. Every case drives its prompt through
// its own keys, which is what a reader presses, rather than by setting the input
// field.
#include "core/editor.h"
#include "ui/ui.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_compile_cfg_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  // A workspace with a file the build fails in, and the script that says so in
  // the shape a compiler says it.
  std::string make_probe_workspace()
  {
    char dir[] = "/tmp/jot_compile_ws_XXXXXX";
    REQUIRE(mkdtemp(dir) != nullptr);
    const std::string root(dir);
    {
      std::ofstream out(root + "/broken.cpp");
      out << "int main() {\n"
             "  return missing;\n"
             "  int unused = 0;\n"
             "}\n";
    }
    {
      std::ofstream out(root + "/search.txt");
      out << "alpha beta gamma\n"
             "delta region12 epsilon\n"
             "region7 zeta\n";
    }
    {
      std::ofstream out(root + "/build.sh");
      out << "#!/bin/sh\n"
             "printf \"broken.cpp:2:10: error: use of undeclared identifier 'missing'\\n\"\n"
             "printf \"broken.cpp:3:7: warning: unused variable 'unused' [-Wunused-variable]"
             "\\n\"\n"
             "exit 1\n";
    }
    return root;
  }

  void wait_for_compile(Editor &editor)
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (editor.compile_running_for_test() && std::chrono::steady_clock::now() < deadline)
    {
      editor.poll_compile_job_for_test();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // One last poll: the job's exit is what writes the footer.
    editor.poll_compile_job_for_test();
  }

  std::string buffer_text(Editor &editor, int buffer_id)
  {
    std::string text;
    for (const std::string &line : editor.buffer_for_test(buffer_id).lines)
    {
      text += line;
      text += "\n";
    }
    return text;
  }

  // Types `text` into the prompt, key by key.
  void type_into_prompt(Editor &editor, const std::string &text)
  {
    for (char c : text)
    {
      editor.minibuffer_key_for_test((unsigned char)c);
    }
  }

  // Backspaces the pre-filled line away and types `text` in its place: the
  // compile prompt re-offers the last command, so a case that wants its own
  // starts by saying so.
  void retype_prompt(Editor &editor, const std::string &text)
  {
    const size_t prefilled = editor.minibuffer_input_for_test().size();
    for (size_t i = 0; i < prefilled; i++)
    {
      editor.minibuffer_key_for_test(127);
    }
    type_into_prompt(editor, text);
  }
} // namespace

TEST_CASE(":compile runs the typed shell command and reads its findings", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  editor.compile_run_for_test("sh ./build.sh");
  CHECK(editor.compile_command_for_test() == "sh ./build.sh");
  wait_for_compile(editor);

  CHECK_FALSE(editor.compile_running_for_test());
  CHECK(editor.compile_exit_code_for_test() == 1);
  REQUIRE(editor.compile_finding_count_for_test() == 2);
  CHECK(editor.compile_finding_file_for_test(0) == root + "/broken.cpp");
  CHECK(editor.compile_finding_file_for_test(1) == root + "/broken.cpp");
  CHECK(editor.compile_finding_line_for_test(0) == 2);
  CHECK(editor.compile_finding_line_for_test(1) == 3);

  // The output lives in the compilation buffer, which is a view rather than a
  // file: read-only, and named for what it renders.
  const int buffer_id = editor.compile_buffer_id_for_test();
  REQUIRE(buffer_id >= 0);
  CHECK(editor.buffer_for_test(buffer_id).filepath == "[Compile]");
  CHECK(editor.buffer_for_test(buffer_id).read_only);
  const std::string text = buffer_text(editor, buffer_id);
  CHECK(text.find("Compilation started at") != std::string::npos);
  CHECK(text.find("sh ./build.sh") != std::string::npos);
  CHECK(text.find("use of undeclared identifier") != std::string::npos);
  CHECK(text.find("Compilation exited abnormally with code 1") != std::string::npos);

  // The findings carry the rows they came from, which is what Enter on a row
  // reads.
  CHECK(editor.compile_finding_row_for_test(0) >= 0);
  CHECK(editor.compile_finding_row_for_test(0) < editor.compile_finding_row_for_test(1));
}

TEST_CASE("the build opens a split of its own below the file being read", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);
  editor.load_file(root + "/search.txt");

  editor.compile_run_for_test("sh ./build.sh");
  wait_for_compile(editor);

  // A split holds the log, and the file the reader was in keeps the pane it was
  // in rather than being replaced by it (the editor is shared between cases, so
  // the split is only checked to be there, not counted afresh).
  CHECK((int)editor.panes_for_test().size() > 1);
  const int compile_pane = editor.compile_pane_index_for_test();
  REQUIRE(compile_pane >= 0);
  CHECK(editor.panes_for_test()[(size_t)compile_pane].buffer_id
        == editor.compile_buffer_id_for_test());
  bool file_still_shown = false;
  for (const SplitPane &pane : editor.panes_for_test())
  {
    if (pane.buffer_id != editor.compile_buffer_id_for_test())
    {
      file_still_shown = true;
    }
  }
  CHECK(file_still_shown);

  // The log is shown in that split, not opened as a tab of it. The pane holds
  // the buffer, so the pane draws it with no strip row above it; and the
  // window's own list -- the workspace strip on row 0, and the keys that walk
  // it -- does not carry the buffer at all.
  const int compile_buffer = editor.compile_buffer_id_for_test();
  CHECK(editor.panes_for_test()[(size_t)compile_pane].tab_buffer_ids
        == std::vector<int>{compile_buffer});
  CHECK_FALSE(editor.pane_shows_tab_strip_for_test(compile_pane));
  CHECK(editor.pane_tab_layout_for_test(compile_pane).segments.empty());
  CHECK_FALSE(editor.tabline_shows_buffer_for_test(compile_buffer));

  // The pane's own keys walk past it too. Two files opened in the log's split
  // make a list of three slots, of which the strip draws two: Alt+1 -- the slot
  // the log holds -- moves nothing, and the numbers the reader can see (2 and
  // 3) are the files the strip lists.
  editor.load_file(root + "/broken.cpp");
  editor.load_file(root + "/search.txt");
  const std::vector<int> &tabs = editor.panes_for_test()[(size_t)compile_pane].tab_buffer_ids;
  REQUIRE(tabs.size() == 3);
  CHECK(tabs[0] == compile_buffer);
  const PaneTabLayout layout = editor.pane_tab_layout_for_test(compile_pane);
  REQUIRE(layout.segments.size() == 2);
  CHECK(layout.segments[0].tab_index == 1);
  CHECK(layout.segments[1].tab_index == 2);
  editor.terminal_key_for_test('1', false, false, true);
  CHECK(editor.buffer_for_test().filepath != "[Compile]");
  CHECK(editor.buffer_for_test().filepath == root + "/search.txt");
  editor.terminal_key_for_test('2', false, false, true);
  CHECK(editor.buffer_for_test().filepath == root + "/broken.cpp");
}

TEST_CASE(":cnext lands on the file the build named", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);
  editor.compile_run_for_test("sh ./build.sh");
  wait_for_compile(editor);

  // The first finding, then the walk to the next one: a jump means the same
  // thing in this list as it does in the problems list.
  REQUIRE(editor.compile_jump_next_for_test());
  CHECK(editor.message_for_test() == "broken.cpp:2");
  CHECK(editor.buffer_for_test().filepath == root + "/broken.cpp");
  // The finding is printed 1-based; the caret it lands on is the column of the
  // error, also 1-based.
  CHECK(editor.buffer_for_test().cursor.y == 1);
  CHECK(editor.buffer_for_test().cursor.x == 9);
  REQUIRE(editor.compile_jump_next_for_test());
  CHECK(editor.buffer_for_test().cursor.y == 2);
  CHECK(editor.buffer_for_test().cursor.x == 6);

  // Enter on a row of the log jumps the same way, from whichever finding's row
  // the reader pressed it on.
  const int row = editor.compile_finding_row_for_test(0);
  REQUIRE(row >= 0);
  REQUIRE(editor.compile_jump_row_for_test(row));
  CHECK(editor.message_for_test() == "broken.cpp:2");
  CHECK(editor.buffer_for_test().cursor.y == 1);
  CHECK(editor.buffer_for_test().cursor.x == 9);
}

TEST_CASE("the build runs under a terminal of its own", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  // The job is a program's terminal too, not only a compiler's: this one asks
  // whether its output is a tty, and reads its stdin -- which has to answer
  // with end-of-input rather than wait for a keyboard the log does not have.
  editor.compile_run_for_test("if test -t 1; then echo IS-A-TTY; fi; "
                              "if read line; then echo read:$line; else echo STDIN-EOF; fi");
  wait_for_compile(editor);

  CHECK_FALSE(editor.compile_running_for_test());
  CHECK(editor.compile_exit_code_for_test() == 0);
  const std::string text = buffer_text(editor, editor.compile_buffer_id_for_test());
  CHECK(text.find("IS-A-TTY") != std::string::npos);
  CHECK(text.find("STDIN-EOF") != std::string::npos);
  CHECK(text.find("Compilation finished at") != std::string::npos);
}

TEST_CASE("a command's own colours land in the log as spans", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  // Red (SGR 31), a palette entry (`38;5;196`) and bold green (`1;32`) on one
  // row: the three spellings a build tool colourises with.
  editor.compile_run_for_test("printf '\\033[31mred\\033[0m \\033[38;5;196mexact\\033[0m "
                              "\\033[1;32mgreen\\033[0m\\n'");
  wait_for_compile(editor);

  const int buffer_id = editor.compile_buffer_id_for_test();
  REQUIRE(buffer_id >= 0);
  const std::string text = buffer_text(editor, buffer_id);
  // The sequences were read, not printed: the log holds the words and no escape.
  CHECK(text.find("red exact green") != std::string::npos);
  CHECK(text.find('\x1b') == std::string::npos);

  const FileBuffer &buf = editor.buffer_for_test(buffer_id);
  int row = -1;
  for (int i = 0; i < (int)buf.lines.size(); i++)
  {
    if (buf.lines[(size_t)i].find("red exact green") != std::string::npos)
    {
      row = i;
    }
  }
  REQUIRE(row >= 0);

  // Each run is a span carrying the colour the command named: ANSI red is
  // palette entry 1, `38;5;196` is entry 196, and bold green is bright entry 10.
  std::vector<std::pair<int, int>> spans; // (column, foreground)
  for (const Decoration &decoration : buf.decorations)
  {
    if (decoration.row == row && decoration.fg >= 0)
    {
      spans.emplace_back(decoration.col, decoration.fg);
    }
  }
  REQUIRE(spans.size() == 3);
  CHECK(spans[0] == std::make_pair(0, 1));
  CHECK(spans[1] == std::make_pair(4, 196));
  CHECK(spans[2] == std::make_pair(10, 10));
}

TEST_CASE("a rewritten row and a tail with no newline still read right", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  // A progress bar rewrites the row it is on with a carriage return, so only
  // what it left there is still on screen -- and a command whose last line has
  // no final newline still said something. The two states are printf's own
  // arguments so that the header's copy of the command cannot be mistaken for
  // either of them.
  editor.compile_run_for_test("printf 'step %d\\rstep %d\\n' 1 2; printf 'tail with no newline'");
  wait_for_compile(editor);

  const std::string text = buffer_text(editor, editor.compile_buffer_id_for_test());
  CHECK(text.find("step 2") != std::string::npos);
  CHECK(text.find("step 1") == std::string::npos);
  CHECK(text.find("tail with no newline") != std::string::npos);
}

TEST_CASE("the compile prompt holds any shell command", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);
  editor.compile_run_for_test("sh ./build.sh");
  wait_for_compile(editor);

  // The prompt re-offers the last command, and what is typed replaces it.
  editor.open_compile_prompt_for_test();
  REQUIRE(editor.show_minibuffer_for_test());
  CHECK(editor.minibuffer_label_for_test() == "Compile command: ");
  CHECK(editor.minibuffer_input_for_test() == "sh ./build.sh");

  const int prefilled = (int)editor.minibuffer_input_for_test().size();
  for (int i = 0; i < prefilled; i++)
  {
    editor.minibuffer_key_for_test(127);
  }
  CHECK(editor.minibuffer_input_for_test().empty());
  type_into_prompt(editor, "true");
  CHECK(editor.minibuffer_input_for_test() == "true");
  CHECK(editor.minibuffer_cursor_for_test() == 4);
  editor.minibuffer_key_for_test('\n');
  CHECK_FALSE(editor.show_minibuffer_for_test());

  // `true` is a shell command like any other, and it succeeds.
  wait_for_compile(editor);
  CHECK(editor.compile_command_for_test() == "true");
  CHECK(editor.compile_exit_code_for_test() == 0);
  CHECK(editor.compile_finding_count_for_test() == 0);
}

TEST_CASE("the command is one line the shell runs, quotes and pipes included", "[compile]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  // Nothing in the line is a command the editor knows: quoting, a pipe, and an
  // argument with a space in it go to `/bin/sh` as typed.
  const std::string command = "printf \"one two\\n\" | grep -c two";
  editor.open_compile_prompt_for_test();
  REQUIRE(editor.show_minibuffer_for_test());
  retype_prompt(editor, command);
  CHECK(editor.minibuffer_input_for_test() == command);
  editor.minibuffer_key_for_test('\n');
  wait_for_compile(editor);

  CHECK(editor.compile_command_for_test() == command);
  CHECK(editor.compile_exit_code_for_test() == 0);
  const std::string text = buffer_text(editor, editor.compile_buffer_id_for_test());
  // The log opens with the command as it was typed, and holds what it printed.
  CHECK(text.find("$ " + command) != std::string::npos);
  CHECK(text.find("\n1\n") != std::string::npos);
  CHECK(editor.compile_finding_count_for_test() == 0);
}

TEST_CASE("the build runs in the workspace, a space in its path and all", "[compile]")
{
  Editor &editor = probe_editor();
  // A workspace directory with a space in its name: the path is quoted on the way
  // to the shell, so the command still runs among the editor's files rather than
  // wherever the editor was started.
  char dir[] = "/tmp/jot compile ws XXXXXX";
  REQUIRE(mkdtemp(dir) != nullptr);
  const std::string root(dir);
  {
    std::ofstream out(root + "/broken.cpp");
    out << "int main() { return missing; }\n";
  }
  {
    std::ofstream out(root + "/build.sh");
    out << "#!/bin/sh\n"
           "printf \"broken.cpp:1:22: error: use of undeclared identifier 'missing'\\n\"\n"
           "exit 1\n";
  }
  editor.set_workspace_root_for_test(root);

  editor.compile_run_for_test("sh ./build.sh");
  wait_for_compile(editor);

  CHECK(editor.compile_exit_code_for_test() == 1);
  REQUIRE(editor.compile_finding_count_for_test() == 1);
  CHECK(editor.compile_finding_file_for_test(0) == root + "/broken.cpp");
  CHECK(editor.compile_finding_line_for_test(0) == 1);
}

TEST_CASE("i-search walks the buffer as the query is typed", "[prompt]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);
  editor.load_file(root + "/search.txt");

  editor.buffer_for_test().cursor = {0, 0};
  editor.open_isearch_prompt_for_test();
  REQUIRE(editor.show_minibuffer_for_test());
  CHECK(editor.minibuffer_label_for_test() == "I-search: ");

  // One match as typed, highlighted, with the caret on it.
  type_into_prompt(editor, "region");
  CHECK(editor.buffer_for_test().cursor.y == 1);
  CHECK(editor.buffer_for_test().cursor.x == 6);
  CHECK(editor.isearch_decoration_count_for_test() == 1);

  // Ctrl+S repeats it: the next match, not the same one again.
  editor.minibuffer_key_for_test('s', /*ctrl=*/true);
  CHECK(editor.buffer_for_test().cursor.y == 2);
  CHECK(editor.buffer_for_test().cursor.x == 0);

  // Turning the pattern flag on re-searches from the origin, and Ctrl+S walks
  // the pattern the same way: `region[0-9]` skips the plain word and finds the
  // numbered one.
  editor.minibuffer_key_for_test('r', /*ctrl=*/false, /*shift=*/false, /*alt=*/true);
  CHECK(editor.minibuffer_label_for_test() == "Regexp I-search: ");
  type_into_prompt(editor, "[0-9]");
  CHECK(editor.buffer_for_test().cursor.y == 1);
  CHECK(editor.buffer_for_test().cursor.x == 6);
  editor.minibuffer_key_for_test('s', /*ctrl=*/true);
  CHECK(editor.buffer_for_test().cursor.y == 2);
  CHECK(editor.buffer_for_test().cursor.x == 0);

  // Enter keeps the match; the highlight is only the search's, so it goes.
  editor.minibuffer_key_for_test('\n');
  CHECK_FALSE(editor.show_minibuffer_for_test());
  CHECK(editor.isearch_decoration_count_for_test() == 0);
  CHECK(editor.buffer_for_test().cursor.y == 2);
}

TEST_CASE("Esc abandons an i-search and puts the caret back", "[prompt]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);
  editor.load_file(root + "/search.txt");

  editor.buffer_for_test().cursor = {5, 1};
  editor.buffer_for_test().preferred_x = 5;
  editor.open_isearch_prompt_for_test();
  type_into_prompt(editor, "zeta");
  CHECK(editor.buffer_for_test().cursor.y == 2);

  editor.minibuffer_key_for_test(27);
  CHECK_FALSE(editor.show_minibuffer_for_test());
  CHECK(editor.buffer_for_test().cursor.y == 1);
  CHECK(editor.buffer_for_test().cursor.x == 5);
  CHECK(editor.isearch_decoration_count_for_test() == 0);

  // A query with no match says so without moving the caret.
  editor.open_isearch_prompt_for_test();
  type_into_prompt(editor, "nowhere");
  CHECK(editor.show_minibuffer_for_test());
  CHECK(editor.buffer_for_test().cursor.y == 1);
}

TEST_CASE("find file completes a path and opens it", "[prompt]")
{
  Editor &editor = probe_editor();
  const std::string root = make_probe_workspace();
  editor.set_workspace_root_for_test(root);

  editor.open_find_file_prompt_for_test();
  REQUIRE(editor.show_minibuffer_for_test());
  CHECK(editor.minibuffer_label_for_test() == "Find file: ");
  type_into_prompt(editor, "bro");
  editor.minibuffer_complete_for_test();
  CHECK(editor.minibuffer_input_for_test() == "broken.cpp");
  CHECK(editor.minibuffer_cursor_for_test() == 10);

  editor.minibuffer_key_for_test('\n');
  CHECK_FALSE(editor.show_minibuffer_for_test());
  CHECK(editor.buffer_for_test().filepath == root + "/broken.cpp");

  // A path that does not exist yet opens as an empty buffer, the way Emacs'
  // find-file does, so a new file can be started from the prompt.
  editor.open_find_file_prompt_for_test();
  type_into_prompt(editor, "later.txt");
  editor.minibuffer_key_for_test('\n');
  CHECK(editor.buffer_for_test().filepath == root + "/later.txt");
  CHECK(editor.buffer_for_test().line_count() == 1);
}
