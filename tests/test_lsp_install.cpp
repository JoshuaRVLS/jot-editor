#include "lsp/install.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("LSP install wrapper emits lifecycle markers", "[lsp]")
{
  const std::string command = LspInstall::wrap_script("bash", "echo installing");

  REQUIRE(command.find("[jot:lsp] start bash") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] success bash exit=%s") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] failed bash exit=%s") != std::string::npos);
  // Runs through the shell so the background-job poll loop can stream output.
  REQUIRE(command.find("/bin/sh -lc") != std::string::npos);
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

TEST_CASE("LSP install platform tag is known", "[lsp]")
{
  const std::string tag = LspInstall::platform_tag();
  REQUIRE((tag == "linux" || tag == "mac" || tag == "win"));
}

TEST_CASE("LSP managed-bin and receipt lookups are non-mutating", "[lsp]")
{
  // Nothing is installed in the test environment; both lookups must be false
  // and must not create any state on disk.
  REQUIRE(LspInstall::resolve_managed_bin("no-such-server-bin").empty());
  REQUIRE_FALSE(LspInstall::is_installed(""));
  REQUIRE_FALSE(LspInstall::is_installed("no-such-server"));
}
