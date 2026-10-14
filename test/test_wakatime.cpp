// The WakaTime integration's pure half (features/wakatime.h): when a heartbeat
// is worth sending, what the cli is asked for, and what its answers mean.
//
// All of it is pinned here rather than through a run, because none of it needs
// one: the rule and the two argument lists are the whole contract with
// wakatime-cli, and a mistake in any of them shows up as an editor that quietly
// stops tracking. The end-to-end path (a real spawn, the chip on the bar) is
// test/wakatime_probe.py's job, on a pty with a stub cli.
#include "extras/wakatime.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jot_wakatime;

namespace
{
  Options options_with_key()
  {
    Options options;
    options.api_key = "abc-123";
    options.plugin = "jot jot-wakatime/1.0.0";
    return options;
  }

  std::string joined(const std::vector<std::string> &args)
  {
    return join_args(args);
  }

  long long ms(long long seconds)
  {
    return seconds * 1000;
  }
} // namespace

TEST_CASE("WakaTime: a save, a different file or two minutes earns a heartbeat", "[jot][wakatime]")
{
  const std::string file = "/work/jot/src/main.cpp";
  const std::string other = "/work/jot/src/other.cpp";

  // A save always goes: it is the one event that says the user finished
  // something, and the cli marks it as a write on the dashboard.
  REQUIRE(should_send(ms(1000), ms(900), file, file, true));
  // The file changed, so the new one has to be reported even though the last
  // heartbeat was a moment ago: that heartbeat is what starts its clock.
  REQUIRE(should_send(ms(1000), ms(900), other, file, false));
  // The same file inside the limit is the duplicate the spec says to drop.
  REQUIRE_FALSE(should_send(ms(1000), ms(900), file, file, false));
  // Two minutes on, the same file is worth reporting again: it is what keeps
  // the time on the dashboard moving while the user is still in the file.
  REQUIRE(should_send(ms(121), ms(1), file, file, false));
  REQUIRE(should_send(ms(120), ms(0), file, file, false));
  REQUIRE_FALSE(should_send(ms(119), ms(0), file, file, false));
  // Nothing behind the name is never sent, save or not.
  REQUIRE_FALSE(should_send(ms(1000), 0, "", "", true));
  REQUIRE_FALSE(should_send(ms(1000), 0, "", file, false));
}

TEST_CASE("WakaTime: a heartbeat carries the file, the caret and the write flag", "[jot][wakatime]")
{
  Heartbeat heartbeat;
  heartbeat.entity = "/work/jot/src/main.cpp";
  heartbeat.time_ms = 1759000000123LL;
  heartbeat.is_write = true;
  heartbeat.lineno = 12;
  heartbeat.cursorpos = 5;
  heartbeat.lines_in_file = 300;

  // One command line, shell-quoted: what the worker hands the shell.
  REQUIRE(joined(heartbeat_args(heartbeat, options_with_key()))
          == "--entity '/work/jot/src/main.cpp' --time 1759000000.123 "
             "--plugin 'jot jot-wakatime/1.0.0' --lineno 12 --cursorpos 5 "
             "--lines-in-file 300 --key 'abc-123' --write");

  // Zero is "not known here" for the three numbers that describe the caret, and
  // the cli is left to detect them rather than being told a wrong one.
  Heartbeat bare;
  bare.entity = "/work/jot/notes.md";
  bare.time_ms = 1759000000000LL;
  REQUIRE(joined(heartbeat_args(bare, options_with_key()))
          == "--entity '/work/jot/notes.md' --time 1759000000.000 "
             "--plugin 'jot jot-wakatime/1.0.0' --key 'abc-123'");

  // No key and no url means the cli reads its own config, which is what lets an
  // existing WakaTime setup work with nothing set in JOT.
  Options bare_options;
  bare_options.plugin = "jot jot-wakatime/1.0.0";
  REQUIRE(
      joined(heartbeat_args(bare, bare_options))
      == "--entity '/work/jot/notes.md' --time 1759000000.000 --plugin 'jot jot-wakatime/1.0.0'");

  // A self-hosted server is passed through, and a path with a space cannot be
  // mistaken for two arguments.
  Options hosted = options_with_key();
  hosted.api_url = "https://waka.example.com/api/v1/";
  Heartbeat spaced;
  spaced.entity = "/work/my notes/a b.md";
  spaced.time_ms = 1759000000000LL;
  const std::string line = joined(heartbeat_args(spaced, hosted));
  REQUIRE(line.find("--api-url 'https://waka.example.com/api/v1/'") != std::string::npos);
  REQUIRE(line.find("--entity '/work/my notes/a b.md'") != std::string::npos);

  // An entity-less heartbeat is no command at all.
  REQUIRE(heartbeat_args(Heartbeat{}, options_with_key()).empty());
}

TEST_CASE("WakaTime: today's total is asked for as json", "[jot][wakatime]")
{
  REQUIRE(
      joined(today_args(options_with_key()))
      == "--today --output json --timeout 15 --plugin 'jot jot-wakatime/1.0.0' --key 'abc-123'");

  Options bare_options;
  REQUIRE(joined(today_args(bare_options)) == "--today --output json --timeout 15");

  Options hosted;
  hosted.api_url = "https://waka.example.com/api/v1/";
  REQUIRE(joined(today_args(hosted))
          == "--today --output json --timeout 15 --api-url 'https://waka.example.com/api/v1/'");
}

TEST_CASE("WakaTime: the total is the top-level text field", "[jot][wakatime]")
{
  // What the cli prints for a normal day. vscode-wakatime reads the same field.
  REQUIRE(today_text(R"({"text":"1 hr 24 mins","total_seconds":5040})") == "1 hr 24 mins");
  REQUIRE(today_text(R"({"has_team_features":false,"text":"12 mins"})") == "12 mins");
  // Surrounding whitespace is not part of the label.
  REQUIRE(today_text(R"({"text":"  4 hrs  ")") == "4 hrs");

  // The categories carry their own `text`, and the one the bar wants is the
  // grand total: a nested field is skipped whole, not read.
  REQUIRE(today_text(R"({"categories":[{"name":"Coding","text":"9 hrs"}],"text":"1 hr"})")
          == "1 hr");
  // The field after the nested one, so the skip has to land exactly right.
  REQUIRE(today_text(R"({"categories":[{"text":"9 hrs"}],"text":"1 hr","x":1})") == "1 hr");
  // A brace inside a nested string cannot close the nested object early.
  REQUIRE(today_text(R"({"a":{"b":"}"},"text":"2 hrs"})") == "2 hrs");
  // An escaped quote in a name is part of the value, not the end of it.
  REQUIRE(today_text(R"({"a":"say \"hi\"" ,"text":"3 hrs"})") == "3 hrs");

  // Empty is the honest answer for everything else: an offline cli prints
  // nothing, a refusal prints something else, and a non-string `text` is not a
  // label. The caller falls back to the local total on any of them.
  REQUIRE(today_text("") == "");
  REQUIRE(today_text("\n") == "");
  REQUIRE(today_text("not json at all") == "");
  REQUIRE(today_text(R"({"errors":["no api key"]})") == "");
  REQUIRE(today_text(R"({"text":5})") == "");
  REQUIRE(today_text(R"({"text":null})") == "");
  REQUIRE(today_text(R"({"text":)") == "");
}

TEST_CASE("WakaTime: the api key is read out of the cli's own config", "[jot][wakatime]")
{
  // The shape wakatime-cli writes: a [settings] section, key = value.
  REQUIRE(cfg_api_key("[settings]\napi_key = abc-123\n") == "abc-123");
  REQUIRE(cfg_api_key("  [settings]\n  api_key   =   abc-123  \n") == "abc-123");
  // Comments and blank lines are not values, and a # inside one is not a
  // comment at all (an API key can hold anything).
  REQUIRE(cfg_api_key("[settings]\n# api_key = ignored\n; api_key = ignored\n"
                      "api_key = a#b\n")
          == "a#b");

  // Another section's key is not the one the cli would use either.
  REQUIRE(cfg_api_key("[other]\napi_key = nope\n") == "");
  REQUIRE(cfg_api_key("[settings]\ndebug = true\n[other]\napi_key = nope\n") == "");
  // The real file keeps its settings first; anything after the key is not it.
  REQUIRE(cfg_api_key("[settings]\napi_key = first\napi_key = second\n") == "first");

  REQUIRE(cfg_api_key("") == "");
  REQUIRE(cfg_api_key("[settings]\ndebug = true\n") == "");
  REQUIRE(cfg_api_key("api_key = no_section\n") == "");
  REQUIRE(cfg_api_key("[settings]\napi_key\n") == "");
  REQUIRE(cfg_api_key("[settings]\napi_key =\n") == "");
}

TEST_CASE("WakaTime: the cli's config path follows its own two variables", "[jot][wakatime]")
{
  unsetenv("WAKATIME_HOME");
  setenv("WAKATIME_HOME", "/tmp/jot_wakatime_home", 1);
  REQUIRE(cfg_path() == "/tmp/jot_wakatime_home/.wakatime.cfg");

  // Unset, it is the home directory, which is where the cli looks too.
  unsetenv("WAKATIME_HOME");
  const char *home = std::getenv("HOME");
  if (home && *home)
  {
    REQUIRE(cfg_path() == std::string(home) + "/.wakatime.cfg");
  }
}

TEST_CASE("WakaTime: the release asset is the one this machine needs", "[jot][wakatime]")
{
  // The release's own naming, one zip per os and arch. The machine strings
  // differ per platform and that is the trap this table pins: uname prints
  // x86_64 and aarch64, mac prints arm64, and Windows stores AMD64.
  REQUIRE(asset_name("linux", "x86_64") == "wakatime-cli-linux-amd64.zip");
  REQUIRE(asset_name("linux", "aarch64") == "wakatime-cli-linux-arm64.zip");
  REQUIRE(asset_name("linux", "riscv64") == "wakatime-cli-linux-riscv64.zip");
  REQUIRE(asset_name("linux", "armv7l") == "wakatime-cli-linux-arm.zip");
  REQUIRE(asset_name("linux", "i686") == "wakatime-cli-linux-386.zip");
  REQUIRE(asset_name("mac", "x86_64") == "wakatime-cli-darwin-amd64.zip");
  REQUIRE(asset_name("mac", "arm64") == "wakatime-cli-darwin-arm64.zip");
  REQUIRE(asset_name("win", "AMD64") == "wakatime-cli-windows-amd64.zip");
  REQUIRE(asset_name("win", "ARM64") == "wakatime-cli-windows-arm64.zip");
  REQUIRE(asset_name("win", "x86") == "wakatime-cli-windows-386.zip");

  // No build is a real answer: an architecture the project does not ship, or a
  // platform nothing installs on, is an empty asset rather than a wrong one, and
  // the editor reports that instead of downloading something useless.
  REQUIRE(asset_name("linux", "sparc64").empty());
  REQUIRE(asset_name("linux", "").empty());
  REQUIRE(asset_name("plan9", "x86_64").empty());
}

TEST_CASE("WakaTime: an asset holds one binary, named after itself", "[jot][wakatime]")
{
  // Checked against a real release rather than guessed:
  // wakatime-cli-linux-amd64.zip holds exactly wakatime-cli-linux-amd64, and the
  // Windows asset adds the .exe.
  REQUIRE(asset_binary("wakatime-cli-linux-amd64.zip", "linux") == "wakatime-cli-linux-amd64");
  REQUIRE(asset_binary("wakatime-cli-darwin-arm64.zip", "mac") == "wakatime-cli-darwin-arm64");
  REQUIRE(asset_binary("wakatime-cli-windows-amd64.zip", "win")
          == "wakatime-cli-windows-amd64.exe");
  REQUIRE(asset_binary("checksums_sha256.txt", "linux").empty());
  REQUIRE(asset_binary("", "linux").empty());

  // The name it is filed under is the one it answers to on PATH, so a managed
  // copy reads the same to this editor and to every other WakaTime plugin.
  REQUIRE(cli_name("linux") == "wakatime-cli");
  REQUIRE(cli_name("mac") == "wakatime-cli");
  REQUIRE(cli_name("win") == "wakatime-cli.exe");
}

TEST_CASE("WakaTime: the install goes into the cli's own home", "[jot][wakatime]")
{
  unsetenv("WAKATIME_HOME");
  setenv("WAKATIME_HOME", "/tmp/jot_wakatime_home", 1);
  REQUIRE(install_dir() == "/tmp/jot_wakatime_home/.wakatime");
  REQUIRE(managed_cli_path("linux") == "/tmp/jot_wakatime_home/.wakatime/wakatime-cli");
  REQUIRE(managed_cli_path("win") == "/tmp/jot_wakatime_home/.wakatime/wakatime-cli.exe");

  // No WAKATIME_HOME and it is the home directory, the same place the cli's own
  // config comes from.
  unsetenv("WAKATIME_HOME");
  const char *home = std::getenv("HOME");
  if (home && *home)
  {
    REQUIRE(install_dir() == std::string(home) + "/.wakatime");
  }
}

TEST_CASE("WakaTime: the install script fetches the asset and unpacks it", "[jot][wakatime]")
{
  const std::string asset = "wakatime-cli-linux-amd64.zip";
  REQUIRE(release_url(asset)
          == "https://github.com/wakatime/wakatime-cli/releases/latest/download/" + asset);
  REQUIRE(release_url("").empty());

  // POSIX: make the directory, fetch, unpack (tar as the fallback for a machine
  // without unzip), rename to the plain name, set the exec bit, drop the
  // archive. A directory with a space is one shell word throughout.
  const std::string sh = install_script("linux", "/tmp/my home", asset);
  REQUIRE(sh.find("mkdir -p '/tmp/my home'") == 0);
  REQUIRE(sh.find("curl -fsSL --max-time ") != std::string::npos);
  REQUIRE(sh.find(" -o '/tmp/my home/wakatime-cli.zip' '" + release_url(asset) + "'")
          != std::string::npos);
  REQUIRE(sh.find("unzip -oq '/tmp/my home/wakatime-cli.zip'") != std::string::npos);
  REQUIRE(sh.find("tar -xf '/tmp/my home/wakatime-cli.zip'") != std::string::npos);
  REQUIRE(sh.find("mv '/tmp/my home/wakatime-cli-linux-amd64' '/tmp/my home/wakatime-cli'")
          != std::string::npos);
  REQUIRE(sh.find("chmod 755 '/tmp/my home/wakatime-cli'") != std::string::npos);
  REQUIRE(sh.find("rm -f '/tmp/my home/wakatime-cli.zip'") != std::string::npos);

  // Windows: the tools a fresh install already has, and none it does not -- no
  // unzip, no chmod, no /bin/sh, the same rule the LSP installers are held to.
  const std::string cmd = install_script("win", "C:\\Users\\me\\.wakatime", asset);
  REQUIRE(cmd.find("curl -fsSL --max-time ") != std::string::npos);
  REQUIRE(cmd.find("tar -xf ") != std::string::npos);
  REQUIRE(cmd.find("move /Y ") != std::string::npos);
  REQUIRE(cmd.find("del ") != std::string::npos);
  REQUIRE(cmd.find("unzip") == std::string::npos);
  REQUIRE(cmd.find("chmod") == std::string::npos);
  REQUIRE(cmd.find("/bin/sh") == std::string::npos);
  // The directory may already be there from an earlier run, and that is not a
  // failure: mkdir's complaint is silenced and the chain carries on.
  REQUIRE(cmd.find("2>NUL & curl") != std::string::npos);

  // Nothing installable is no script at all: the caller reports it instead of
  // running something that cannot work.
  REQUIRE(install_script("linux", "/tmp/x", "checksums_sha256.txt").empty());
  REQUIRE(install_script("linux", "", asset).empty());
}

TEST_CASE("WakaTime: offline and missing-key exit codes are not failures", "[jot][wakatime]")
{
  // The two codes the spec's plugins treat as "queued, not lost", plus the
  // ordinary ones that are real failures.
  REQUIRE(exit_is_offline(102));
  REQUIRE(exit_is_offline(112));
  REQUIRE_FALSE(exit_is_offline(0));
  REQUIRE_FALSE(exit_is_offline(1));
  REQUIRE_FALSE(exit_is_offline(127));
}

TEST_CASE("WakaTime: the chip falls back to the local total", "[jot][wakatime]")
{
  REQUIRE(activity_label("1 hr 24 mins", "12m") == "1 hr 24 mins");
  REQUIRE(activity_label("", "12m") == "12m");
  REQUIRE(activity_label("", "") == "");
  // A total reporting no time at all is not an answer: the cli prints one while
  // its queued heartbeats are still offline, and the local store counted the
  // same minutes, so it is what the chip shows instead.
  REQUIRE(activity_label("0 secs", "12m") == "12m");
  REQUIRE(activity_label("0 mins", "12m") == "12m");
  REQUIRE(activity_label("0 hrs 0 mins", "12m") == "12m");
  REQUIRE(activity_label("0 secs", "") == "");
  // A worded total with no digits is a real answer and stays.
  REQUIRE(activity_label("less than a minute", "12m") == "less than a minute");
}
