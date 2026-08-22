#include "tools/lsp/client.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

TEST_CASE("LSP file URI encodes reserved path characters", "[lsp]") {
  const std::filesystem::path input =
      std::filesystem::temp_directory_path() / "jot lsp#%.cpp";
  const std::string uri = LSPClient::file_uri_from_path(input.string());

  REQUIRE(uri.find("%20") != std::string::npos);
  REQUIRE(uri.find("%23") != std::string::npos);
  REQUIRE(uri.find("%25") != std::string::npos);
  REQUIRE(LSPClient::file_path_from_uri(uri) == input.lexically_normal().string());
}

TEST_CASE("LSP UTF-16 offsets round trip UTF-8 editor positions", "[lsp]") {
  const std::string text = "a" "\xC3\xA9" "\xF0\x9F\x98\x80" "z";

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
