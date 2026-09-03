#include "lsp/install.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("LSP install command mapping", "[lsp]")
{
  const auto python = LspInstall::command_for_server("python");
  REQUIRE(python.supported);
  REQUIRE(python.command.find("python-lsp-server") != std::string::npos);

  const auto typescript = LspInstall::command_for_server("typescript");
  REQUIRE(typescript.supported);
  REQUIRE(typescript.command.find("typescript-language-server") != std::string::npos);

  REQUIRE_FALSE(LspInstall::command_for_server("rust").supported);
}

TEST_CASE("LSP install terminal command emits lifecycle markers", "[lsp]")
{
  const auto install = LspInstall::command_for_server("bash");
  const std::string command = LspInstall::terminal_command(install);

  REQUIRE(command.find("[jot:lsp] start bash") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] success bash exit=%s") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] failed bash exit=%s") != std::string::npos);
}

TEST_CASE("LSP install marker parser reads terminal status", "[lsp]")
{
  LspInstall::Marker marker;
  REQUIRE(LspInstall::parse_marker("[jot:lsp] start python", marker));
  REQUIRE(marker.phase == "start");
  REQUIRE(marker.server == "python");
  REQUIRE(marker.exit_code == -1);

  REQUIRE(LspInstall::parse_marker("prefix [jot:lsp] failed html exit=23", marker));
  REQUIRE(marker.phase == "failed");
  REQUIRE(marker.server == "html");
  REQUIRE(marker.exit_code == 23);
  REQUIRE_FALSE(LspInstall::parse_marker("no marker", marker));
}
