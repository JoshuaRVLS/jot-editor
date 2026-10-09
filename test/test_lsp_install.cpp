// The LSP installer's host half: the [jot:lsp] marker protocol, the managed
// bin/receipt lookups, and the bundled-payload lookup a release package uses to
// install a server it already ships (clangd) with no network.
#include "editor.h"
#include "lsp/install.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

  // The install tree the managed bins and generated Windows scripts live in.
  // XDG_DATA_HOME is what data_root() reads first, so the real HOME is never
  // touched. Held for the duration of the case and restored on destruction:
  // leaving it pointed at the scratch directory past the end of a case left
  // every later test that resolves a data root -- the tree-sitter parser
  // install root among them -- looking in a directory that had been deleted.
  struct SeedDataHome
  {
    SeedDataHome()
    {
      const char *previous = std::getenv("XDG_DATA_HOME");
      had_ = previous != nullptr;
      if (had_)
      {
        old_ = previous;
      }
      char data[] = "/tmp/jot_lsp_install_data_XXXXXX";
      mkdtemp(data);
      path = data;
      setenv("XDG_DATA_HOME", path.c_str(), 1);
    }
    ~SeedDataHome()
    {
      if (had_)
      {
        setenv("XDG_DATA_HOME", old_.c_str(), 1);
      }
      else
      {
        unsetenv("XDG_DATA_HOME");
      }
    }
    SeedDataHome(const SeedDataHome &) = delete;
    SeedDataHome &operator=(const SeedDataHome &) = delete;

    std::string path;
    std::string old_;
    bool had_ = false;
  };

  std::string read_file(const fs::path &path)
  {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

TEST_CASE("LSP install wrapper renders a cmd.exe script for Windows", "[lsp]")
{
  SeedDataHome data_home;
  const std::string data = data_home.path;
  setenv("JOT_INSTALL_PLATFORM", "win", 1);

  const std::string command = LspInstall::wrap_script(
      "cpp", "echo step one\necho step two\n", LspInstall::ScriptShell::Cmd);

  // Nothing POSIX survives on this path: there is no /bin/sh to hand a script
  // to, and no printf to write the markers with.
  REQUIRE(command.find("/bin/sh") == std::string::npos);
  REQUIRE(command.find("printf") == std::string::npos);
  REQUIRE(command.find("cmd /c ") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] start cpp") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] success cpp exit=0") != std::string::npos);
  REQUIRE(command.find("[jot:lsp] failed cpp exit=1") != std::string::npos);

  // The body goes into a batch file, because cmd.exe cannot fail fast on a
  // command line: `exit /b` is what aborts the script on the first bad step,
  // and `cmd /c <file>` turns that into the status the markers report.
  const fs::path scripts = fs::path(data) / "jot" / "lsp" / "scripts";
  int generated = 0;
  std::string body;
  for (const auto &entry : fs::directory_iterator(scripts))
  {
    if (entry.path().extension() != ".cmd")
    {
      continue;
    }
    generated++;
    body = read_file(entry.path());
    REQUIRE(command.find(entry.path().string()) != std::string::npos);
  }
  REQUIRE(generated == 1);
  REQUIRE(body.find("echo step one") != std::string::npos);
  REQUIRE(body.find("echo step two") != std::string::npos);
  // CRLF: cmd's parser is line oriented and a bare LF misparses some builtins.
  REQUIRE(body.find("\r\n") != std::string::npos);
  REQUIRE(body.find("\n\n") == std::string::npos);

  // The wiring, not just the explicit argument: with the platform on win the
  // default shell has to be the cmd one, or every caller silently keeps
  // handing its script to /bin/sh.
  REQUIRE(LspInstall::platform_tag() == "win");
  REQUIRE(LspInstall::default_shell() == LspInstall::ScriptShell::Cmd);
  const std::string defaulted = LspInstall::wrap_script("cpp", "echo defaulted\n");
  REQUIRE(defaulted.find("/bin/sh") == std::string::npos);
  REQUIRE(defaulted.find("cmd /c ") != std::string::npos);

  // Two installs never share a file, so a running job never has its script
  // rewritten underneath it.
  const std::string second = LspInstall::wrap_script("cpp", "echo other\n");
  REQUIRE(second != command);

  unsetenv("JOT_INSTALL_PLATFORM");
  fs::remove_all(data);
}

TEST_CASE("LSP managed bin resolves Windows launcher extensions", "[lsp]")
{
  SeedDataHome data_home;
  const std::string data = data_home.path;
  const fs::path bin = fs::path(data) / "jot" / "lsp" / "bin";
  std::error_code ec;
  fs::create_directories(bin, ec);
  std::ofstream(bin / "clangd.exe") << "MZ";
  std::ofstream(bin / "eslint-lsp.cmd") << "@echo off\n";

  setenv("JOT_INSTALL_PLATFORM", "win", 1);
  REQUIRE(LspInstall::platform_tag() == "win");
  REQUIRE(LspInstall::resolve_managed_bin("clangd") == (bin / "clangd.exe").string());
  REQUIRE(LspInstall::resolve_managed_bin("eslint-lsp") == (bin / "eslint-lsp.cmd").string());
  // A name that was never installed is not fabricated into an executable just
  // because the platform has extensions.
  REQUIRE(LspInstall::resolve_managed_bin("rust-analyzer").empty());

  // On a POSIX host the same tree answers nothing: the bare name is the only
  // shape an install produces there.
  unsetenv("JOT_INSTALL_PLATFORM");
  REQUIRE(LspInstall::resolve_managed_bin("clangd").empty());
  std::ofstream(bin / "clangd") << "#!/bin/sh\n";
  REQUIRE(LspInstall::resolve_managed_bin("clangd") == (bin / "clangd").string());

  fs::remove_all(data);
}

TEST_CASE("LSP install plan renders cmd.exe steps on Windows", "[lsp]")
{
  seed_config_home();
  SeedDataHome data_home;
  const std::string data = data_home.path;
  // No payload in sight, so the plan has to come from the download manager: the
  // renderer is what this covers.
  setenv("JOT_LSP_PAYLOAD_DIR", "/nonexistent/jot-payload", 1);
  setenv("JOT_INSTALL_PLATFORM", "win", 1);

  Editor e;
  std::string id, script, message;
  REQUIRE(e.lsp_install_plan_for_test("cpp", &id, &script, &message));
  REQUIRE(id == "cpp");
  REQUIRE(message.find("install started") != std::string::npos);
  // The github manager's Windows renderer: Windows' own curl and tar, a findstr
  // lookup for the release asset, and the batch primitives that publish it.
  REQUIRE(script.find("curl -fsSL") != std::string::npos);
  REQUIRE(script.find("tar -xf") != std::string::npos);
  REQUIRE(script.find("findstr") != std::string::npos);
  REQUIRE(script.find("for /r") != std::string::npos);
  REQUIRE(script.find("|| exit /b 1") != std::string::npos);
  REQUIRE(script.find("echo name=cpp") != std::string::npos);
  REQUIRE(script.find("setlocal") != std::string::npos);
  // Nothing from the POSIX renderer may survive on this path: none of it runs
  // on Windows, and shipping it is how the install used to fail silently.
  REQUIRE(script.find("ln -sfn") == std::string::npos);
  REQUIRE(script.find("chmod") == std::string::npos);
  REQUIRE(script.find("unzip") == std::string::npos);
  REQUIRE(script.find("XDG_DATA_HOME") == std::string::npos);
  REQUIRE(script.find("/bin/sh") == std::string::npos);

  // A package with no Windows source at all still says so, instead of
  // generating a POSIX script that cmd.exe could never run.
  std::string id2, script2, message2;
  REQUIRE(e.lsp_install_plan_for_test("swiftlint", &id2, &script2, &message2));
  REQUIRE(id2 == "swiftlint");
  REQUIRE(script2.empty());
  REQUIRE(message2.find("has no Windows installer yet") != std::string::npos);

  unsetenv("JOT_INSTALL_PLATFORM");
  unsetenv("JOT_LSP_PAYLOAD_DIR");
  fs::remove_all(data);
}

TEST_CASE("LSP install plan renders every manager family on Windows", "[lsp]")
{
  seed_config_home();
  SeedDataHome data_home;
  const std::string data = data_home.path;
  // No payload in sight: every id here has to come from its own manager.
  setenv("JOT_LSP_PAYLOAD_DIR", "/nonexistent/jot-payload", 1);
  setenv("JOT_INSTALL_PLATFORM", "win", 1);
  Editor e;

  struct Family
  {
    const char *id;
    std::vector<std::string> markers;
  }; // One id per manager family with its Windows-only steps: the manager's own
     // command, the shape of what it produces there, and the publish primitives.
  const std::vector<Family> families = {
      {"asm-lsp",
       {"cargo install --root",
        "--locked",
        "asm-lsp.exe"}}, // The package managers below are batch shims on Windows, and a batch
                         // started from a batch without `call` never hands control back.
      {"alex", {"call npm install --prefix", "alex@11.0.1", "alex.cmd"}},
      {"crlfmt",
       {"set \"GOBIN=", "go install \"github.com/cockroachdb/crlfmt@v0.5.0\"", "crlfmt.exe"}},
      // The catalog's "v1.27.1#cmd/dlv" suffix is a subpath of the module, not
      // part of the version: go rejects the inline form.
      {"delve", {"go install \"github.com/go-delve/delve/cmd/dlv@v1.27.1\""}},
      {"csharpier", {"dotnet tool update --tool-path", "--version \"1.2.6\"", "csharpier.exe"}},
      {"erb-lint",
       {"call gem install --no-user-install",
        "set \"GEM_HOME=",
        "erblint.bat",
        "call \"%_jot_found%\" %%*"}},
      {"ocaml-lsp", {"call opam install --yes --no-depext", "opam exec -- ocamllsp %%*"}},
      {"pint", {"call composer require --working-dir=", "composer.json", "pint.bat"}},
      {"luacheck", {"call luarocks install --tree", "luacheck.bat"}},
      {"gradle-language-server",
       {"open-vsx.org/api/vscjava/vscode-gradle", "tar -xf", "java -jar \"%_jot_found%\" %%*"}},
      {"haxe-language-server", {"tar -xf", "for /r", "node \"%_jot_found%\" %%*"}},
      {"kotlin-lsp", {"kotlin-server-262.9593.0.win.zip", "intellij-server.exe"}},
      {"jdtls", {"lombok.jar", "python \"%_jot_found%\" %%*"}},
      {"phpactor", {"copy /Y", "phpactor.phar", "php \"%_jot_found%\" %%*"}},
  };

  for (const auto &family : families)
  {
    std::string id, script, message;
    REQUIRE(e.lsp_install_plan_for_test(family.id, &id, &script, &message));
    REQUIRE(id == family.id);
    REQUIRE(message.find("install started") != std::string::npos);
    for (const auto &marker : family.markers)
    {
      INFO(family.id << " marker: " << marker);
      REQUIRE(script.find(marker) != std::string::npos);
    }
    // Nothing POSIX may reach this path: none of it runs on Windows, and
    // shipping it is how these installs failed there.
    INFO(family.id << " script: " << script.substr(0, 300));
    REQUIRE(script.find("ln -sfn") == std::string::npos);
    REQUIRE(script.find("chmod") == std::string::npos);
    REQUIRE(script.find("unzip") == std::string::npos);
    REQUIRE(script.find("/bin/sh") == std::string::npos);
    REQUIRE(script.find("XDG_DATA_HOME") == std::string::npos);
    // The receipt is written last, so a step that failed can never leave one.
    REQUIRE(script.find(std::string("echo name=") + family.id) != std::string::npos);
  }

  // The same subpath handling on the POSIX renderer, which shares the target.
  unsetenv("JOT_INSTALL_PLATFORM");
  std::string id, script, message;
  REQUIRE(e.lsp_install_plan_for_test("delve", &id, &script, &message));
  REQUIRE(script.find("github.com/go-delve/delve/cmd/dlv@v1.27.1") != std::string::npos);
  REQUIRE(script.find("#cmd") == std::string::npos);

  unsetenv("JOT_LSP_PAYLOAD_DIR");
  fs::remove_all(data);
}
