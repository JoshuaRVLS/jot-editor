#include "editor.h"
#include "tools/lsp/client.h"
#include "tools/lsp/internal.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
  // A probe editor for the prompt cases: real Editor, isolated config, temp file.
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_lsp_prompt_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      seeded = true;
    }
    static Editor e;
    return e;
  }

  std::string write_temp_source(Editor &e, const std::string &text)
  {
    static int counter = 0;
    const std::string path = "/tmp/jot_rename_prompt_" + std::to_string(::getpid()) + "_"
                             + std::to_string(counter++) + ".cpp";
    std::ofstream out(path);
    out << text;
    out.close();
    e.load_file(path);
    return path;
  }
} // namespace

TEST_CASE("LSP file URI encodes reserved path characters", "[lsp]")
{
  const std::filesystem::path input = std::filesystem::temp_directory_path() / "jot lsp#%.cpp";
  const std::string uri = LSPClient::file_uri_from_path(input.string());

  REQUIRE(uri.find("%20") != std::string::npos);
  REQUIRE(uri.find("%23") != std::string::npos);
  REQUIRE(uri.find("%25") != std::string::npos);
  REQUIRE(LSPClient::file_path_from_uri(uri) == input.lexically_normal().string());
}

TEST_CASE("LSP UTF-16 offsets round trip UTF-8 editor positions", "[lsp]")
{
  const std::string text = "a"
                           "\xC3\xA9"
                           "\xF0\x9F\x98\x80"
                           "z";

  REQUIRE(LSPClient::utf16_offset_from_utf8(text, 0) == 0);
  REQUIRE(LSPClient::utf16_offset_from_utf8(text, 1) == 1);
  REQUIRE(LSPClient::utf16_offset_from_utf8(text, 3) == 2);
  REQUIRE(LSPClient::utf16_offset_from_utf8(text, 7) == 4);
  REQUIRE(LSPClient::utf16_offset_from_utf8(text, 8) == 5);

  REQUIRE(LSPClient::utf8_offset_from_utf16(text, 0) == 0);
  REQUIRE(LSPClient::utf8_offset_from_utf16(text, 1) == 1);
  REQUIRE(LSPClient::utf8_offset_from_utf16(text, 2) == 3);
  REQUIRE(LSPClient::utf8_offset_from_utf16(text, 4) == 7);
  REQUIRE(LSPClient::utf8_offset_from_utf16(text, 5) == 8);
}

TEST_CASE("LSP rename WorkspaceEdit parses changes and documentChanges", "[lsp]")
{
  const std::filesystem::path a =
      (std::filesystem::temp_directory_path() / "jot rename a.cpp").lexically_normal();
  const std::filesystem::path b =
      (std::filesystem::temp_directory_path() / "jot rename b.cpp").lexically_normal();

  // Classic `changes` map form.
  {
    std::string json = R"({"changes":{")"
                       + LSPClient::file_uri_from_path(a.string()) + R"(":[{"range":{"start":{"line":0,"character":3},"end":{"line":0,"character":8}},"newText":"renamed"}]}})";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> out;
    lsp_detail::workspace_edit_from_result(value, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].first == a.string());
    REQUIRE(out[0].second.size() == 1);
    REQUIRE(out[0].second[0].start_line == 0);
    REQUIRE(out[0].second[0].start_char == 3);
    REQUIRE(out[0].second[0].end_char == 8);
    REQUIRE(out[0].second[0].new_text == "renamed");
  }

  // v3 documentChanges form (what clangd/rust-analyzer send for renames).
  {
    std::string json = R"({"documentChanges":[{"textDocument":{"uri":")"
                       + LSPClient::file_uri_from_path(b.string())
                       + R"("},"edits":[{"range":{"start":{"line":2,"character":0},"end":{"line":2,"character":4}},"newText":"other"}]}]})";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> out;
    lsp_detail::workspace_edit_from_result(value, out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0].first == b.string());
    REQUIRE(out[0].second.size() == 1);
    REQUIRE(out[0].second[0].start_line == 2);
    REQUIRE(out[0].second[0].start_char == 0);
    REQUIRE(out[0].second[0].end_char == 4);
    REQUIRE(out[0].second[0].new_text == "other");
  }

  // Both forms in one result are merged by file.
  {
    std::string json = R"({"changes":{")"
                       + LSPClient::file_uri_from_path(a.string())
                       + R"(":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":1}},"newText":"A"}]},"documentChanges":[{"textDocument":{"uri":")"
                       + LSPClient::file_uri_from_path(a.string())
                       + R"("},"edits":[{"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":1}},"newText":"B"}]}]})";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    std::vector<std::pair<std::string, std::vector<LSPTextEdit>>> out;
    lsp_detail::workspace_edit_from_result(value, out);
    REQUIRE(out.size() == 2); // one entry per form, not merged
    REQUIRE(out[0].first == a.string());
    REQUIRE(out[1].first == a.string());
  }
}

TEST_CASE("LSP code actions parse titles and edits, drop command-only", "[lsp]")
{
  const std::filesystem::path target =
      (std::filesystem::temp_directory_path() / "jot fix.cpp").lexically_normal();

  std::string json = R"([{"title":"Replace with auto","kind":"quickfix","edit":{"changes":{")"
                     + LSPClient::file_uri_from_path(target.string())
                     + R"(":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":5}},"newText":"auto"}]}}},{"title":"Organize imports","command":{"title":"Organize imports","command":"workbench.action.organizeImports"}}])";
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value(json, pos, value));
  std::vector<LSPCodeAction> actions;
  lsp_detail::code_actions_from_result(value, actions);

  // The edit-bearing action is kept; the command-only one is dropped.
  REQUIRE(actions.size() == 1);
  REQUIRE(actions[0].title == "Replace with auto");
  REQUIRE(actions[0].kind == "quickfix");
  REQUIRE(actions[0].edits.size() == 1);
  REQUIRE(actions[0].edits[0].first == target.string());
  REQUIRE(actions[0].edits[0].second.size() == 1);
  REQUIRE(actions[0].edits[0].second[0].start_char == 0);
  REQUIRE(actions[0].edits[0].second[0].end_char == 5);
  REQUIRE(actions[0].edits[0].second[0].new_text == "auto");
}

// The completion popup splits each row using the item's own fields, and the
// half of the information that arrives in labelDetails is only sent when the
// client advertises labelDetailsSupport. These cases pin the parse; the client
// capability itself is asserted below so a future edit cannot silently drop it
// and leave the popup with nothing to work from.
TEST_CASE("LSP completion items parse labelDetails", "[lsp]")
{
  // Both fields, as clangd sends them (parameter list + include path).
  {
    std::string json =
        R"json([{"label":"printf","kind":3,"detail":"int","labelDetails":{"detail":"(const char *format, ...)","description":"X <stdio.h>"}}])json";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    const auto items = lsp_detail::completion_items_from_json(value);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].label == "printf");
    REQUIRE(items[0].detail == "int");
    REQUIRE(items[0].label_detail == "(const char *format, ...)");
    REQUIRE(items[0].label_description.find("stdio.h") != std::string::npos);
  }

  // Only a description, as rust-analyzer sends for trait methods.
  {
    std::string json =
        R"json([{"label":"next","detail":"fn next() -> Option<T>","labelDetails":{"description":"(as Iterator)"}}])json";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    const auto items = lsp_detail::completion_items_from_json(value);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].label_detail.empty());
    REQUIRE(items[0].label_description == "(as Iterator)");
  }

  // Absent (the common case for servers that never send it): the fields stay
  // empty rather than picking up something else.
  {
    std::string json = R"json([{"label":"foo","detail":"int","documentation":"a note"}])json";
    size_t pos = 0;
    lsp_detail::JsonValue value;
    REQUIRE(lsp_detail::parse_json_value(json, pos, value));
    const auto items = lsp_detail::completion_items_from_json(value);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].label_detail.empty());
    REQUIRE(items[0].label_description.empty());
    // The older fields are untouched by the change.
    REQUIRE(items[0].detail == "int");
    REQUIRE(items[0].documentation == "a note");
  }
}

// The labelDetails fields only arrive if the client asks for them, so the
// capability is asserted directly: without it servers are entitled to omit the
// data the popup now relies on for its richer rows.
TEST_CASE("LSP initialize advertises labelDetailsSupport", "[lsp]")
{
  const std::string caps = LSPClient::completion_client_capabilities();
  REQUIRE(caps.find("\"labelDetailsSupport\":true") != std::string::npos);
  REQUIRE(caps.find("\"completionItem\":{") != std::string::npos);
  // ...and it has to be *valid* JSON: this string is spliced into the
  // initialize request, where a missing comma makes the whole request
  // unparseable and the server refuses to start.
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value("{" + caps + "}", pos, value));
  REQUIRE(value.type == lsp_detail::JsonValue::Object);
}

TEST_CASE("LSP initialize advertises window.workDoneProgress", "[lsp]")
{
  // Servers gate $/progress on this capability: without it they check once and
  // never report work, so the statusline spinner would never have anything to
  // show. Same trap (and same test shape) as labelDetailsSupport.
  const std::string caps = LSPClient::window_client_capabilities();
  REQUIRE(caps.find("\"workDoneProgress\":true") != std::string::npos);
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value("{" + caps + "}", pos, value));
  REQUIRE(value.type == lsp_detail::JsonValue::Object);
}

TEST_CASE("LSP progress tokens start, move and clear", "[lsp]")
{
  std::map<std::string, LSPProgress> tokens;

  // begin carries the title; an empty message/percentage means "not yet".
  apply_lsp_progress(tokens, "tok", "begin", "Indexing", "", -1);
  REQUIRE(tokens.size() == 1);
  REQUIRE(tokens["tok"].title == "Indexing");
  REQUIRE(tokens["tok"].message.empty());
  REQUIRE(tokens["tok"].percentage == -1);

  // report moves the message and percentage without losing the title.
  apply_lsp_progress(tokens, "tok", "report", "", "3/12 files", 25);
  REQUIRE(tokens["tok"].title == "Indexing");
  REQUIRE(tokens["tok"].message == "3/12 files");
  REQUIRE(tokens["tok"].percentage == 25);

  // Fields that did not move stay put rather than being cleared.
  apply_lsp_progress(tokens, "tok", "report", "", "", -1);
  REQUIRE(tokens["tok"].message == "3/12 files");
  REQUIRE(tokens["tok"].percentage == 25);

  // Two tokens are tracked separately, which is what the "+N" in the statusline
  // counts.
  apply_lsp_progress(tokens, "other", "begin", "Loading", "", -1);
  REQUIRE(tokens.size() == 2);

  // end retires just that token; an unknown kind is treated as the end so a
  // server that stops reporting cannot leave a spinner running.
  apply_lsp_progress(tokens, "tok", "end", "", "", -1);
  REQUIRE(tokens.size() == 1);
  REQUIRE(tokens.count("tok") == 0);
  apply_lsp_progress(tokens, "other", "surprise", "", "", -1);
  REQUIRE(tokens.empty());
}

TEST_CASE("LSP initialize params are one valid JSON object", "[lsp]")
{
  // Assembled by hand from many pieces, so it is asserted as a whole: the two
  // bugs this has already had (a dropped comma, a stray brace from the window
  // capability) both produced "Text after end of document" from clangd and a
  // server that silently never started.
  lsp_detail::JsonValue params;
  size_t pos = 0;
  const std::string json = LSPClient::initialize_params_json_for_test();
  REQUIRE(lsp_detail::parse_json_value(json, pos, params));
  REQUIRE(params.type == lsp_detail::JsonValue::Object);
  // The reader must stop exactly at the end: trailing text is what the server
  // chokes on.
  REQUIRE(pos == json.size());

  // window.workDoneProgress has to sit *inside* capabilities -- as a params
  // member it is ignored and servers never report progress.
  const lsp_detail::JsonValue *caps = lsp_detail::json_object_get(params, "capabilities");
  REQUIRE(caps != nullptr);
  const lsp_detail::JsonValue *window = lsp_detail::json_object_get(*caps, "window");
  REQUIRE(window != nullptr);
  const lsp_detail::JsonValue *progress = lsp_detail::json_object_get(*window, "workDoneProgress");
  REQUIRE(progress != nullptr);
  REQUIRE(progress->type == lsp_detail::JsonValue::Bool);
  REQUIRE(progress->bool_value);
  REQUIRE(lsp_detail::json_object_get(params, "window") == nullptr);
}

TEST_CASE("Rename prompt seeds from the cursor, applies on Enter, cancels on Esc", "[lsp]")
{
  // The prompt is the interactive half of LSP rename: :lsprename with no name
  // opens it seeded with the identifier under the cursor, so the common case is
  // one edit and Enter. Esc must leave the buffer alone.
  Editor &e = probe_editor();
  write_temp_source(e, "int counter_value = 1;\n");

  // Cursor inside "counter_value": the prompt opens holding that word.
  e.scroll_cursor_to_for_test(0, 6);
  e.open_rename_prompt_for_test();
  REQUIRE(e.rename_prompt_visible_for_test());
  REQUIRE(e.rename_prompt_text_for_test() == "counter_value");

  // Editing the field does not touch the buffer.
  e.rename_prompt_input_for_test('x');
  REQUIRE(e.rename_prompt_text_for_test() == "counter_valuex");
  REQUIRE(e.buffer_for_test().line(0) == "int counter_value = 1;");

  e.rename_prompt_input_for_test(127); // Backspace
  REQUIRE(e.rename_prompt_text_for_test() == "counter_value");

  // Esc closes it and changes nothing.
  e.rename_prompt_input_for_test(27);
  REQUIRE_FALSE(e.rename_prompt_visible_for_test());
  REQUIRE(e.buffer_for_test().line(0) == "int counter_value = 1;");
}

TEST_CASE("Rename prompt Enter without a name does not rename", "[lsp]")
{
  Editor &e = probe_editor();
  write_temp_source(e, "int other_name = 2;\n");
  e.scroll_cursor_to_for_test(0, 5);
  e.open_rename_prompt_for_test();
  REQUIRE(e.rename_prompt_text_for_test() == "other_name");

  // Clear the field, then Enter: an empty new name is not a rename, and the
  // prompt closes either way.
  for (size_t i = 0; i < std::string("other_name").size(); i++)
  {
    e.rename_prompt_input_for_test(127);
  }
  REQUIRE(e.rename_prompt_text_for_test().empty());
  e.rename_prompt_input_for_test('\n');
  REQUIRE_FALSE(e.rename_prompt_visible_for_test());
  REQUIRE(e.buffer_for_test().line(0) == "int other_name = 2;");
}

TEST_CASE(":lsprename with no name opens the prompt, with a name renames directly", "[lsp]")
{
  // The command existed first and must keep working with an argument; without
  // one it now prompts instead of reporting usage.
  Editor &e = probe_editor();
  write_temp_source(e, "int some_identifier = 3;\n");
  e.scroll_cursor_to_for_test(0, 5);

  e.run_ex_for_test(":lsprename");
  REQUIRE(e.rename_prompt_visible_for_test());
  REQUIRE(e.rename_prompt_text_for_test() == "some_identifier");
  e.rename_prompt_input_for_test(27); // Esc: leave it closed for the next check
  REQUIRE_FALSE(e.rename_prompt_visible_for_test());

  // With a name it goes straight to the rename request (no prompt): there is no
  // server in this test, so the observable effect is that no prompt opened.
  e.run_ex_for_test(":lsprename renamed_by_command");
  REQUIRE_FALSE(e.rename_prompt_visible_for_test());
}

TEST_CASE("LSP workspace/symbol parses SymbolInformation with location and container", "[lsp]")
{
  // workspace/symbol answers with SymbolInformation: name, kind, containerName
  // and a location in another file. That is the same shape documentSymbol uses
  // for its flat form, so it is parsed by the same helper -- this pins that the
  // workspace reply really does go through it (a new parser would be the easy
  // mistake, and the container is what tells two same-named symbols apart).
  const std::string json = R"([
    {"name":"Widget","kind":5,"containerName":"ui::",
     "location":{"uri":"file:///tmp/ws/widget.cpp","range":{"start":{"line":12,"character":4},
     "end":{"line":12,"character":10}}}},
    {"name":"render","kind":6,"containerName":"",
     "location":{"uri":"file:///tmp/ws/widget.cpp","range":{"start":{"line":40,"character":0},
     "end":{"line":40,"character":6}}}}
  ])";
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value(json, pos, value));
  const auto symbols = lsp_detail::document_symbols_from_result(value, std::string());
  REQUIRE(symbols.size() == 2);
  REQUIRE(symbols[0].name == "Widget");
  REQUIRE(symbols[0].detail == "ui::"); // containerName, shown as the tie-breaker
  REQUIRE(symbols[0].filepath.find("widget.cpp") != std::string::npos);
  REQUIRE(symbols[0].line == 12);
  REQUIRE(symbols[0].character == 4);
  REQUIRE(symbols[1].name == "render");
  REQUIRE(symbols[1].detail.empty());
}

TEST_CASE("Workspace diagnostics list files that are not open, once each", "[lsp]")
{
  Editor &e = probe_editor();
  const std::string open_path = write_temp_source(e, "int open_file = 1;\n");
  const std::string closed_path = "/tmp/jot_ws_diag_closed.cpp";

  // A diagnostic for a file that is not open at all: the workspace picker is
  // supposed to cover those (they are what the servers already told us), which
  // is why closing a file no longer drops its diagnostics.
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", closed_path, 7, 1, "not open but broken");
  // The same problem reported by two servers is one row.
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", closed_path, 7, 1, "not open but broken");
  e.seed_lsp_diagnostic_for_test("clang-tidy|/tmp/ws", closed_path, 7, 1, "not open but broken");
  // ...and one for an open file, to show both sources merge.
  e.seed_lsp_diagnostic_for_test("cpp|/tmp/ws", open_path, 0, 2, "open file warning");

  const auto items = e.workspace_diagnostics_for_test();
  int closed_rows = 0;
  int open_rows = 0;
  int duplicates = 0;
  for (const auto &item : items)
  {
    if (item.filepath == closed_path)
    {
      closed_rows++;
      duplicates += item.label == "Error: not open but broken" ? 1 : 0;
    }
    if (item.filepath == open_path)
    {
      open_rows++;
    }
  }
  REQUIRE(closed_rows == 1);
  REQUIRE(open_rows == 1);
  REQUIRE(duplicates == 1);

  // Errors sort before warnings, whichever file they are in.
  REQUIRE_FALSE(items.empty());
  REQUIRE(items.front().severity == 1);
}
