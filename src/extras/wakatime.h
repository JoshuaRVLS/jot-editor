#ifndef JOT_FEATURES_WAKATIME_H
#define JOT_FEATURES_WAKATIME_H

// WakaTime coding-time tracking (https://wakatime.com/help/creating-plugin).
//
// The integration is the plugin split the spec describes: this editor is the
// editor half, and wakatime-cli is the half that knows the language, the
// project and the branch. So nothing here talks to the API itself -- it runs
// the cli and reads what comes back, exactly like vscode-wakatime does, which
// is also what keeps an existing ~/.wakatime.cfg useful.
//
// Two things live here, and both are pure so the tests can pin them with no
// cli on the machine and no network:
//
//   * when a heartbeat is worth sending (the spec's "two minutes, or the file
//     changed, or a save") and what arguments it carries;
//   * what the statusline's activity chip says -- today's total once the cli
//     has answered, the local session duration until then;
//   * which release asset this machine needs and the script that unpacks it,
//     for the install the editor runs when PATH has no cli at all.
#include <string>
#include <vector>

namespace jot_wakatime
{
  // The plugin spec's rate limit: with nothing else to go on, one heartbeat
  // every two minutes for the file in front of the user. Below it, the same
  // file and no save is what the cli would call a duplicate.
  constexpr long long kHeartbeatRateLimitMs = 120000;

  // The cli's own exit codes for "could not reach the API, queued offline"
  // (102) and "no API key configured" (112). The spec's plugins treat both as
  // a non-error: the heartbeat is not lost, it is waiting.
  bool exit_is_offline(int exit_code);

  struct Options
  {
    // Passed as --key when set. Left empty, wakatime-cli reads api_key from
    // ~/.wakatime.cfg itself, which is what makes a machine that already has
    // WakaTime set up work with no JOT setting at all.
    std::string api_key;
    // Passed as --api-url when set, for a self-hosted or WakaTime-compatible
    // server. Empty leaves the cli its own default.
    std::string api_url;
    // What the cli reports this editor as (--plugin), the User-Agent WakaTime
    // shows on the plugin page.
    std::string plugin;
  };

  // One heartbeat, already resolved against the editor: the file that is in
  // front of the user and where in it the caret sits.
  struct Heartbeat
  {
    std::string entity;    // absolute path; an empty one is never sent
    long long time_ms = 0; // unix epoch ms, when the event happened
    bool is_write = false;
    int lineno = 0;        // 1-based caret row; 0 leaves it to the cli
    int cursorpos = 0;     // 1-based caret column
    int lines_in_file = 0; // 0 leaves the count to the cli
  };

  // True when this event deserves a heartbeat: a save, a different file, or
  // two minutes since the last one. Every other keystroke is dropped here, on
  // whoever's thread called, so the cli is never spawned per keystroke.
  bool should_send(long long now_ms,
                   long long last_sent_ms,
                   const std::string &entity,
                   const std::string &last_entity,
                   bool is_write);

  // The cli arguments for one heartbeat, values shell-quoted and without the
  // binary itself. Empty for an entity-less heartbeat, which callers read as
  // "nothing to send".
  std::vector<std::string> heartbeat_args(const Heartbeat &heartbeat, const Options &options);

  // The cli arguments that ask for dashboard time for today, as one JSON
  // object on stdout (vscode-wakatime reads it the same way). The timeout is
  // short because this runs while the user is looking at the bar it feeds.
  std::vector<std::string> today_args(const Options &options);

  // Joins args into one shell command line, quoting each value.
  std::string join_args(const std::vector<std::string> &args);

  // The `text` field of --today's top-level JSON object, its total for today
  // ("1 hr 24 mins"). Nested objects are skipped whole, so a `text` inside
  // `categories` cannot be mistaken for the total; a missing or non-string
  // field reads as empty, which is the caller's signal to fall back.
  std::string today_text(const std::string &cli_stdout);

  // The activity chip's label: the cli's total when there is one, else the
  // local session duration it stands in for, so the chip is never blank.
  std::string activity_label(const std::string &today, const std::string &fallback);

  // The api_key out of a ~/.wakatime.cfg's `[settings]` section (up to the next
  // section header, `key = value`, `#` and `;` comments). Empty when there is
  // none, which is how the editor knows tracking cannot work yet.
  std::string cfg_api_key(const std::string &cfg_text);

  // Where wakatime-cli keeps its config: $WAKATIME_HOME, else the home
  // directory, then .wakatime.cfg. The same file and the same two variables
  // the cli itself reads, so JOT and an existing WakaTime setup agree.
  std::string cfg_path();

  // ---------------------------------------------------------------------------
  // Installing the cli
  // ---------------------------------------------------------------------------
  //
  // The integration is only as good as the binary behind it, so the editor
  // fetches one the first time the user turns WakaTime on and PATH has none.
  // Nothing here runs a command: this half picks the asset this machine needs
  // and writes the script that installs it, and the editor runs that script off
  // the keystroke path.

  // The release asset for a machine: wakatime-cli-<os>-<arch>.zip, where
  // `platform` is the installers' tag ("linux", "mac", "win") and `machine` is
  // what the machine calls its architecture -- `uname -m` on POSIX,
  // %PROCESSOR_ARCHITECTURE% on Windows, either case. Empty when that pair has
  // no build, which is the caller's cue that there is nothing to install here.
  std::string asset_name(const std::string &platform, const std::string &machine);

  // The file inside that asset: the asset's own name without .zip, and .exe on
  // Windows. This is the release's layout rather than a guess -- the asset
  // wakatime-cli-linux-amd64.zip holds one executable, wakatime-cli-linux-amd64.
  // Empty for a name that is not an asset.
  std::string asset_binary(const std::string &asset, const std::string &platform);

  // What the installed binary is called: the name the cli answers to on PATH,
  // so a managed copy reads the same to this editor and to every other WakaTime
  // plugin on the machine.
  std::string cli_name(const std::string &platform);

  // Where that copy lives: $WAKATIME_HOME, else the home directory, then
  // .wakatime -- the directory the cli itself treats as home, which is what
  // makes an install here shared with the other editors' plugins. Empty when
  // there is no home to install into.
  std::string install_dir();

  // install_dir()/cli_name(platform), empty when there is no home.
  std::string managed_cli_path(const std::string &platform);

  // The download URL: the release repo's `latest/download/<asset>` redirect, so
  // the newest build is always the one fetched and there is no version to keep
  // current.
  std::string release_url(const std::string &asset);

  // The one command that installs: make the directory, fetch the asset, unpack
  // it, rename it to cli_name() and drop the archive -- each step chained, so
  // the exit status is the install's own verdict. Empty when there is no asset
  // or no directory to install into. The fetch is bounded, because this runs on
  // the worker queue and a stalled network must not hold the git status and the
  // LSP scans behind it.
  std::string
  install_script(const std::string &platform, const std::string &dir, const std::string &asset);
} // namespace jot_wakatime

#endif // JOT_FEATURES_WAKATIME_H
