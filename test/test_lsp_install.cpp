// The LSP installer's host half: the [jot:lsp] marker protocol, the managed
// bin/receipt lookups, and the bundled-payload lookup a release package uses to
// install a server it already ships (clangd) with no network.
#include "editor.h"
#include "lsp/install.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
  void seed_config_home()
  {
    char home[] = "/tmp/jot_lsp_install_test_XXXXXX";
    mkdtemp(home);
    setenv("JOT_CONFIG_HOME", home, 1);
    setenv("JOT_CACHE_HOME", home, 1);
  }
} // namespace

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

TEST_CASE("LSP bundled payload lookup follows the payload root", "[lsp]")
{
  // Stage a payload the way a release lays it out: <root>/<bin>/... with the
  // binary under the archive's own versioned directory level.
  const fs::path root = fs::temp_directory_path() / "jot_lsp_payload_test";
  const fs::path bin_dir = root / "clangd" / "clangd_22.1.8" / "bin";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(bin_dir, ec);
  {
    std::ofstream(bin_dir / "clangd") << "#!/bin/sh\nexit 0\n";
  }

  // The env var is authoritative while set, so the assertions hold whatever the
  // install tree beside the test binary looks like.
  setenv("JOT_LSP_PAYLOAD_DIR", root.c_str(), 1);
  REQUIRE(LspInstall::bundled_payload_dir("clangd") == (root / "clangd").string());

  // A name the package does not carry, and the empty name, both answer "".
  REQUIRE(LspInstall::bundled_payload_dir("rust-analyzer").empty());
  REQUIRE(LspInstall::bundled_payload_dir("").empty());

  // A file where a payload directory would be is not a payload.
  {
    std::ofstream(root / "gopls") << "not a directory\n";
  }
  REQUIRE(LspInstall::bundled_payload_dir("gopls").empty());

  unsetenv("JOT_LSP_PAYLOAD_DIR");
  fs::remove_all(root, ec);
}

TEST_CASE("LSP install plan prefers a bundled payload over a download", "[lsp]")
{
  seed_config_home();
  const fs::path root = fs::temp_directory_path() / "jot_lsp_payload_plan";
  const fs::path bin_dir = root / "clangd" / "clangd_22.1.8" / "bin";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(bin_dir, ec);
  {
    std::ofstream(bin_dir / "clangd") << "#!/bin/sh\nexit 0\n";
  }

  setenv("JOT_LSP_PAYLOAD_DIR", root.c_str(), 1);
  Editor e;
  std::string id, script, message;
  REQUIRE(e.lsp_install_plan_for_test("cpp", &id, &script, &message));
  REQUIRE(id == "cpp");
  // The shipped copy is linked, not downloaded: no curl, no unpacking.
  REQUIRE(script.find((root / "clangd").string()) != std::string::npos);
  REQUIRE(script.find("ln -sfn") != std::string::npos);
  REQUIRE(script.find("receipt") != std::string::npos);
  REQUIRE(script.find("curl") == std::string::npos);
  REQUIRE(message.find("bundled copy") != std::string::npos);

  // With no payload in sight the same request goes back to the download
  // manager, which is what an unpackaged/source build does.
  setenv("JOT_LSP_PAYLOAD_DIR", "/nonexistent/jot-payload", 1);
  std::string id2, script2, message2;
  REQUIRE(e.lsp_install_plan_for_test("cpp", &id2, &script2, &message2));
  REQUIRE(id2 == "cpp");
  REQUIRE(script2.find("releases/download") != std::string::npos);
  REQUIRE(message2.find("bundled") == std::string::npos);

  unsetenv("JOT_LSP_PAYLOAD_DIR");
  fs::remove_all(root, ec);
}
