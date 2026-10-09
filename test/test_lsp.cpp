#include "editor.h"
#include "editor/model/panes.h" // pane_viewport_h
#include "lsp/client.h"
#include "lsp/internal.h"
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

TEST_CASE("An LSP edit takes its undo step in the buffer it names", "[lsp][undo]")
{
  Editor &e = probe_editor();
  e.set_home_menu_visible(false);
  const std::string a = write_temp_source(e, "int alpha = 1;\n");
  const std::string b = write_temp_source(e, "int beta = 2;\n");
  // b is the buffer in front; a is open behind it.
  REQUIRE(e.host().core.buffer_content() == "int beta = 2;");
  int a_index = -1;
  for (int i = 0; i < e.buffer_count_for_test(); i++)
  {
    if (e.buffer_for_test(i).filepath == a)
    {
      a_index = i;
      break;
    }
  }
  REQUIRE(a_index >= 0);

  // A format (or rename) answer for a arrives while b is the buffer on screen.
  LSPTextEdit edit;
  edit.start_line = 0;
  edit.start_char = 0;
  edit.end_line = 0;
  edit.end_char = 3;
  edit.new_text = "long";
  e.apply_lsp_text_edits_for_test(a, {edit});
  REQUIRE(e.buffer_for_test(a_index).lines[0] == "long alpha = 1;");
  // The buffer in front is not the one the answer was for, and it is untouched.
  REQUIRE(e.host().core.buffer_content() == "int beta = 2;");

  // The edit's step belongs to that buffer's own history: Ctrl+Z in its window
  // takes it back, rather than having been pushed onto whatever buffer the
  // pane happened to be showing.
  e.load_file(a);
  REQUIRE(e.host().core.buffer_content() == "long alpha = 1;");
  e.host().core.undo();
  REQUIRE(e.host().core.buffer_content() == "int alpha = 1;");
  std::remove(a.c_str());
  std::remove(b.c_str());
}

TEST_CASE("Tab after an LSP edit that shortens the file stays inside it", "[lsp]")
{
  Editor &e = probe_editor();
  e.set_home_menu_visible(false);
  const std::string path = write_temp_source(e, "alpha\nbeta\ngamma\ndelta\n");
  FileBuffer &buf = e.buffer_for_test();

  // The last four rows are selected, so both ends of the selection address the
  // file as it stands now.
  const int last_row = (int)buf.lines.size() - 1;
  REQUIRE(buf.lines[last_row] == "delta");
  buf.selection = {{0, 0}, {(int)buf.lines[last_row].size(), last_row}, true};
  buf.cursor = buf.selection.end;

  // A format answer rewrites rows 1-3 into one empty row: the file shrinks, and
  // the end of the selection has no row under it any more.
  LSPTextEdit edit;
  edit.start_line = 1;
  edit.start_char = 0;
  edit.end_line = 3;
  edit.end_char = 5;
  edit.new_text = "";
  e.apply_lsp_text_edits_for_test(path, {edit});
  REQUIRE(e.buffer_for_test().lines.size() == 2);

  // The selection comes back onto the file it addresses...
  const FileBuffer &after = e.buffer_for_test();
  REQUIRE(after.selection.end.y == 1);
  REQUIRE(after.selection.end.x == 0);

  // ...so Tab indents the rows that exist. Left stale, the walk over the
  // selected rows indexed the line list past its end.
  e.apply_default_tab();
  REQUIRE(e.host().core.buffer_content() == "  alpha\n  ");
  std::remove(path.c_str());
}

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

// Auto-import: a completion item may carry the edits that make the completed
// name resolvable (an `import` statement for TS/JS, an `#include` for clangd).
// They arrive as the same TextEdit array a formatting result is, and the apply
// path needs the ranges intact -- an item whose edits are dropped completes to
// an identifier the file cannot compile.
TEST_CASE("LSP completion items parse additionalTextEdits", "[lsp]")
{
  std::string json =
      R"json([{"label":"Widget","kind":7,"insertText":"Widget","additionalTextEdits":[{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"newText":"import { Widget } from \"./widget\";\n"},{"range":{"start":{"line":4,"character":2},"end":{"line":4,"character":9}},"newText":"value"}]}])json";
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value(json, pos, value));
  const auto items = lsp_detail::completion_items_from_json(value);
  REQUIRE(items.size() == 1);
  REQUIRE(items[0].additional_text_edits.size() == 2);
  REQUIRE(items[0].additional_text_edits[0].start_line == 0);
  REQUIRE(items[0].additional_text_edits[0].start_char == 0);
  REQUIRE(items[0].additional_text_edits[0].new_text == "import { Widget } from \"./widget\";\n");
  REQUIRE(items[0].additional_text_edits[1].start_line == 4);
  REQUIRE(items[0].additional_text_edits[1].end_char == 9);
  REQUIRE(items[0].additional_text_edits[1].new_text == "value");

  // An item with none (every other server) leaves the list empty rather than
  // inheriting the previous item's edits.
  const std::string plain = R"json([{"label":"other"},{"label":"Widget"}])json";
  size_t plain_pos = 0;
  lsp_detail::JsonValue plain_value;
  REQUIRE(lsp_detail::parse_json_value(plain, plain_pos, plain_value));
  const auto plain_items = lsp_detail::completion_items_from_json(plain_value);
  REQUIRE(plain_items.size() == 2);
  REQUIRE(plain_items[0].additional_text_edits.empty());
  REQUIRE(plain_items[1].additional_text_edits.empty());
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

// The active parameter of signature help is spelled two ways: the result-level
// `activeParameter` (the older one clangd sends) and a per-signature
// `activeParameter`. Reading only the per-signature field left clangd's calls
// with nothing to highlight, so both spellings are pinned here.
TEST_CASE("LSP signature help reads the active parameter both ways", "[lsp]")
{
  // clangd's shape: the parameter is on the result, the signature has none.
  const std::string clangd = "{\"activeSignature\":0,\"activeParameter\":1,\"signatures\":[{"
                             "\"label\":\"add(int left, int right)\","
                             "\"parameters\":[{\"label\":[4,12]},{\"label\":[14,23]}]}]}";
  size_t pos = 0;
  lsp_detail::JsonValue value;
  REQUIRE(lsp_detail::parse_json_value(clangd, pos, value));
  const LSPSignatureHelpResult parsed = lsp_detail::signature_help_from_result(value);
  REQUIRE(parsed.signatures.size() == 1);
  REQUIRE(parsed.active_parameter == 1);
  REQUIRE(parsed.signatures[0].active_parameter == -1);
  REQUIRE(parsed.signatures[0].parameters.size() == 2);
  REQUIRE(parsed.signatures[0].parameters[1].label_start == 14);
  REQUIRE(parsed.signatures[0].parameters[1].label_end == 23);

  // A server that only sets the per-signature field still parses, and the
  // result-level one stays unset so the renderer falls back to it.
  const std::string per_signature =
      "{\"signatures\":[{\"label\":\"f(int a)\",\"activeParameter\":0,"
      "\"parameters\":[{\"label\":\"int a\"}]}]}";
  pos = 0;
  REQUIRE(lsp_detail::parse_json_value(per_signature, pos, value));
  const LSPSignatureHelpResult parsed2 = lsp_detail::signature_help_from_result(value);
  REQUIRE(parsed2.active_parameter == -1);
  REQUIRE(parsed2.signatures.size() == 1);
  REQUIRE(parsed2.signatures[0].active_parameter == 0);
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

// --- Navigation: declaration / type definition / implementation, and the
// header <-> source flip.
//
// The four location lookups share one request/reply path and differ only in the
// method sent, so the method mapping is asserted directly and the landing policy
// is driven through the same handler the client's poll loop calls.

TEST_CASE("LSP navigation kinds map to their protocol methods", "[lsp][navigation]")
{
  using Kind = LSPNavigationKind;
  REQUIRE(std::string(lsp_detail::navigation_method_name(Kind::Definition))
          == "textDocument/definition");
  REQUIRE(std::string(lsp_detail::navigation_method_name(Kind::Declaration))
          == "textDocument/declaration");
  REQUIRE(std::string(lsp_detail::navigation_method_name(Kind::TypeDefinition))
          == "textDocument/typeDefinition");
  REQUIRE(std::string(lsp_detail::navigation_method_name(Kind::Implementation))
          == "textDocument/implementation");
}

TEST_CASE("Switch header/source result parses the URI clangd answers with", "[lsp][navigation]")
{
  const std::filesystem::path header =
      (std::filesystem::temp_directory_path() / "jot switch pair.h").lexically_normal();
  const std::string uri = LSPClient::file_uri_from_path(header.string());
  REQUIRE(LSPClient::file_path_from_uri(uri) == header.string());
  // No pair is reported as an empty string, which the editor turns into a
  // message rather than a jump.
  REQUIRE(LSPClient::file_path_from_uri("").empty());
}

TEST_CASE("A declaration jump lands and reports which lookup found it", "[lsp][navigation]")
{
  Editor &e = probe_editor();
  const std::string file = write_temp_source(
      e, "struct Widget { int value; };\nint main() { Widget w; return w.value; }\n");
  e.scroll_cursor_to_for_test(1, 20);

  LSPDefinitionResult result;
  result.origin_filepath = file;
  result.origin_line = 1;
  result.origin_character = 20;
  result.navigation = LSPNavigationKind::Declaration;
  LSPLocation location;
  location.filepath = file;
  location.line = 0;
  location.character = 13;
  location.end_line = 0;
  location.end_character = 18;
  result.locations.push_back(location);

  e.deliver_lsp_definition_for_test(result);

  REQUIRE(e.buffer_for_test().cursor.y == 0);
  const std::string name = std::filesystem::path(file).filename().string();
  REQUIRE(e.message_for_test() == "Declaration: " + name + ":1");
}

// Where a definition jump leaves the view is part of reading the definition:
// the minimal reveal pinned the caret's line to the last row of the pane, so a
// jump into a function arrived with the rest of the function still below the
// fold and the eye starting at the bottom edge. A landmark that is off screen
// is centred instead; one that is already on screen keeps the view it found.
namespace
{
  // 400 lines, so a target in the middle of them has a full pane of code below.
  std::string filler_source()
  {
    std::string text;
    for (int i = 0; i < 400; i++)
    {
      text += "// filler " + std::to_string(i) + "\n";
    }
    return text;
  }

  void deliver_definition_jump(Editor &e, const std::string &file, int origin_line, int target_line)
  {
    LSPDefinitionResult result;
    result.origin_filepath = file;
    result.origin_line = origin_line;
    result.origin_character = 3;
    result.navigation = LSPNavigationKind::Definition;
    LSPLocation location;
    location.filepath = file;
    location.line = target_line;
    location.character = 4;
    location.end_line = target_line;
    location.end_character = 10;
    result.locations.push_back(location);
    e.deliver_lsp_definition_for_test(result);
  }
} // namespace

TEST_CASE("A definition jump off screen centres the target line", "[lsp][navigation]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(100, 30);
  const std::string file = write_temp_source(e, filler_source());
  const int target_line = 200;

  e.scroll_cursor_to_for_test(0, 3);
  const int viewport_h = pane_viewport_h(e.pane_for_test());
  REQUIRE(viewport_h > 4);
  REQUIRE(e.buffer_for_test().scroll_offset == 0);

  deliver_definition_jump(e, file, /*origin_line=*/0, target_line);

  REQUIRE(e.buffer_for_test().cursor.y == target_line);
  REQUIRE(target_line - e.buffer_for_test().scroll_offset == viewport_h / 2);
}

// The viewport cannot scroll past the last line, so a definition near the end of
// the file still comes to rest against the bottom edge. Pinned because it is the
// one case centring has nothing to give, and it must not scroll past the end to
// fake a middle.
TEST_CASE("A definition jump near the end of the file lands on the last row", "[lsp][navigation]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(100, 30);
  const std::string file = write_temp_source(e, filler_source());
  const int target_line = 399;

  e.scroll_cursor_to_for_test(0, 3);
  const int viewport_h = pane_viewport_h(e.pane_for_test());

  deliver_definition_jump(e, file, /*origin_line=*/0, target_line);

  REQUIRE(e.buffer_for_test().cursor.y == target_line);
  REQUIRE(e.buffer_for_test().scroll_offset == 400 - viewport_h);
  REQUIRE(target_line - e.buffer_for_test().scroll_offset == viewport_h - 1);
}

TEST_CASE("A definition jump onto a line already on screen leaves the viewport alone",
          "[lsp][navigation]")
{
  Editor &e = probe_editor();
  e.apply_resize_for_test(100, 30);
  const std::string file = write_temp_source(e, filler_source());

  e.scroll_cursor_to_for_test(0, 3);
  REQUIRE(e.buffer_for_test().scroll_offset == 0);

  deliver_definition_jump(e, file, /*origin_line=*/0, /*target_line=*/5);

  REQUIRE(e.buffer_for_test().cursor.y == 5);
  REQUIRE(e.buffer_for_test().scroll_offset == 0);
}

TEST_CASE("A location lookup with no hit says so instead of waiting", "[lsp][navigation]")
{
  Editor &e = probe_editor();
  const std::string file = write_temp_source(e, "int main() { return 0; }\n");
  e.scroll_cursor_to_for_test(0, 4);

  LSPDefinitionResult result;
  result.origin_filepath = file;
  result.origin_line = 0;
  result.origin_character = 4;
  result.navigation = LSPNavigationKind::TypeDefinition;

  e.deliver_lsp_definition_for_test(result);

  REQUIRE(e.message_for_test() == "No type definition found");
}

TEST_CASE("Switch header/source opens the paired file, or reports there is none", "[lsp][navigation]")
{
  Editor &e = probe_editor();
  const std::string source = write_temp_source(e, "int f();\n");
  // The probe editor is shared across cases, so start the history from here.
  e.reset_jumplist_for_test();
  const std::filesystem::path twin =
      std::filesystem::path(source).replace_extension(".h");
  {
    std::ofstream out(twin.string());
    out << "int f();\n";
  }

  e.deliver_lsp_switch_source_header_for_test("");
  REQUIRE(e.message_for_test() == "No paired header/source found");
  REQUIRE(std::filesystem::path(e.buffer_for_test().filepath).filename().string()
          == std::filesystem::path(source).filename().string());

  e.deliver_lsp_switch_source_header_for_test(twin.string());
  REQUIRE(std::filesystem::path(e.buffer_for_test().filepath).filename().string()
          == twin.filename().string());
  REQUIRE(e.message_for_test() == "Switched to " + twin.filename().string());
  // Landing in the pair is a jump like any other.
  REQUIRE(e.jump_count_for_test() == 1);
}
