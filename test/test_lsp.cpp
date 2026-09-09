#include "tools/lsp/client.h"
#include "tools/lsp/internal.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

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
