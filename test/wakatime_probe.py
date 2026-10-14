#!/usr/bin/env python3
"""Probe: the WakaTime integration, against stub wakatime-cli, curl and unzip.

The integration's whole contract is with wakatime-cli: the editor spawns it and
reads what it prints. So the probe puts a stub `wakatime-cli` first on PATH that
appends the argv it was handed to a file, and answers `--today` with a fixed
payload. What the editor actually asked for is then read back from the file the
stub wrote -- the production path (the worker queue, the shell, the plugin spec's
rule), not a seam.

The editor installs that cli when PATH has none, and the install is a curl and an
unpack: the probe stubs those too, as two more PATH entries that write down what
they were handed. The stub unzip unpacks the file the stub curl "downloaded"
using the release's real layout (one binary named after the asset, .exe on
Windows), so a wrong asset or a wrong inner name in the editor fails here rather
than on a user's machine.

Scenes:

  * an edit and a save put two heartbeats in the record, from the cli on PATH --
    the first without `--write`, the second with it and on the absolute path of
    the file in the buffer. The spec's rule is what keeps it at two and not one
    per keystroke -- and nothing is downloaded, because PATH already had one;
  * the record holds a `--today --output json` run, and the bar shows the total
    the stub answered with -- so the fetch, the parse and the chip are one
    chain, on a real screen;
  * a stub whose total reports no time at all ("0 secs", what the cli prints
    while its queued heartbeats are still offline) leaves the locally stored
    total on the bar and the timer glyph beside it -- the chip is the time coded
    today in this workspace, not whatever a delayed integration can say;
  * with no cli on PATH the install runs against the stub curl/unzip: this
    machine's release asset is fetched and unpacked into
    $WAKATIME_HOME/.wakatime/wakatime-cli, and every run after that comes from
    that absolute path;
  * a copy already in the WakaTime home is used as it is, with no second
    download;
  * an install that cannot download is reported once and leaves the chip on the
    local total, with nothing spawned;
  * with `wakatime=false` nothing is spawned and nothing is installed.

Usage: test/wakatime_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import platform
import re
import shutil
import stat
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

ROOT = "/tmp/jot_wakatime_probe"
# Three PATH entries, one per scene shape: a cli to run, curl and unzip to
# install with but no cli, and a curl that cannot download.
BIN = "/tmp/jot_wakatime_probe_bin"
INSTALL_BIN = "/tmp/jot_wakatime_probe_install_bin"
FAIL_BIN = "/tmp/jot_wakatime_probe_fail_bin"
# $WAKATIME_HOME, the directory the installed copy is looked for in and where the
# cli would keep its own config.
HOME = "/tmp/jot_wakatime_probe_home"
# The cli source the stub unzip drops as the "unpacked" binary, kept outside every
# PATH entry so nothing finds it before it is installed.
CLI_STUB = "/tmp/jot_wakatime_probe_cli_stub"
# A plain text file on purpose: no language server and no formatter-on-save, so
# the bar has room for the chip and the scene is about the heartbeat rather than
# about clangd's own tray of chips.
FILE = "notes.txt"
FILE_BODY = "alpha\nbeta\ngamma\n"

TODAY_TEXT = "1 hr 30 mins"
STUB_TODAY = '{"has_team_features":false,"text":"1 hr 30 mins","total_seconds":5400}'
# What the same cli prints while its queued heartbeats have not reached the
# server yet: a total reporting no time at all. The stub can be handed either
# payload, which is how the probe tells an answer from the absence of one.
ZERO_TEXT = "0 secs"
ZERO_TODAY = '{"has_team_features":false,"text":"0 secs","total_seconds":0}'
# Nerd Fonts codepoints the coding chip leads with: nf-md-clock_outline is
# WakaTime's answer, nf-md-timer_outline is the local store's total.
CLOCK_GLYPH = "\U000F0150"
TIMER_GLYPH = "\U000F051B"

# The stub: one line per invocation, the name it was run under and the argv it
# was given, and the payload vscode-wakatime reads for today (`text` at the top
# level). The name comes first because the installed copy is run by absolute
# path: which binary answered is what tells an install from a PATH lookup.
STUB_CLI = """#!/bin/sh
printf '%s|%s\\n' "$0" "$*" >> "$JOT_WAKATIME_RECORD"
today=$JOT_WAKATIME_TODAY
[ -z "$today" ] && today='{"has_team_features":false,"text":"1 hr 30 mins","total_seconds":5400}'
for arg in "$@"; do
  if [ "$arg" = "--today" ]; then
    printf '%s\\n' "$today"
    exit 0
  fi
done
exit 0
"""

# The download: it records what it was asked for, and where it was told to write
# the archive it puts the URL, which is all the stub unzip needs to know the
# asset's name.
STUB_CURL = """#!/bin/sh
printf 'curl|%s\\n' "$*" >> "$JOT_WAKATIME_RECORD"
out=""; url=""; prev=""
for arg in "$@"; do
  [ "$prev" = "-o" ] && out="$arg"
  case "$arg" in http*) url="$arg" ;; esac
  prev="$arg"
done
[ -n "$out" ] && printf '%s\\n' "$url" > "$out"
exit 0
"""

# A download that fails, for the scene where the machine is offline. Recorded the
# same way, so the probe can tell an attempt from a silent skip.
STUB_CURL_FAIL = """#!/bin/sh
printf 'curl|%s\\n' "$*" >> "$JOT_WAKATIME_RECORD"
exit 22
"""

# The unpack, using the release's real layout: the archive holds exactly one
# binary named after the asset, without the .zip and with .exe on Windows.
STUB_UNZIP = """#!/bin/sh
printf 'unzip|%s\\n' "$*" >> "$JOT_WAKATIME_RECORD"
dir=""; archive=""; prev=""
for arg in "$@"; do
  [ "$prev" = "-d" ] && dir="$arg"
  case "$arg" in *.zip) archive="$arg" ;; esac
  prev="$arg"
done
[ -z "$dir" ] && dir="."
name=$(basename "$(cat "$archive" 2>/dev/null)")
name=${name%.zip}
case "$name" in *windows*) name="$name.exe" ;; esac
[ -z "$name" ] && exit 1
cp "$JOT_WAKATIME_CLI_STUB" "$dir/$name" && chmod +x "$dir/$name"
"""


def host_asset() -> str:
    """The release asset this machine needs, by the same naming the editor uses."""
    os_name = {"Linux": "linux", "Darwin": "darwin", "Windows": "windows"}.get(
        platform.system(), "")
    arch = {
        "x86_64": "amd64", "amd64": "amd64", "AMD64": "amd64",
        "aarch64": "arm64", "arm64": "arm64", "ARM64": "arm64",
        "i686": "386", "x86": "386",
        "armv7l": "arm", "arm": "arm", "riscv64": "riscv64",
    }.get(platform.machine(), "")
    if not os_name or not arch:
        return ""
    return f"wakatime-cli-{os_name}-{arch}.zip"


def write_stub(path: str, body: str) -> None:
    with open(path, "w") as fh:
        fh.write(body)
    os.chmod(path, os.stat(path).st_mode | stat.S_IEXEC | stat.S_IXGRP | stat.S_IXOTH)


def write_workspace() -> None:
    for directory in (ROOT, BIN, INSTALL_BIN, FAIL_BIN):
        shutil.rmtree(directory, ignore_errors=True)
        os.makedirs(directory)
    with open(os.path.join(ROOT, FILE), "w") as fh:
        fh.write(FILE_BODY)
    write_stub(CLI_STUB, STUB_CLI)
    write_stub(os.path.join(BIN, "wakatime-cli"), STUB_CLI)
    for directory, curl in ((BIN, STUB_CURL), (INSTALL_BIN, STUB_CURL), (FAIL_BIN, STUB_CURL_FAIL)):
        write_stub(os.path.join(directory, "curl"), curl)
        write_stub(os.path.join(directory, "unzip"), STUB_UNZIP)


def installed_cli(home: str) -> str:
    return os.path.join(home, ".wakatime", "wakatime-cli")


def preinstall(home: str) -> None:
    """A managed copy from an earlier run: the stub, where the editor looks."""
    os.makedirs(os.path.dirname(installed_cli(home)), exist_ok=True)
    write_stub(installed_cli(home), STUB_CLI)


def seed_local_total(cfg: str, ms: int) -> None:
    """The local store's line for this workspace and day, as an earlier session
    would have written it, so the chip's fallback has a real number to show."""
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    key = f"{ROOT}\t{time.strftime('%Y-%m-%d')}"
    with open(os.path.join(cfg, "configs", "coding_time.tsv"), "w") as fh:
        fh.write(f"{key}\t{ms}\n")


def write_settings(cfg: str, lines: list[str]) -> None:
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        for line in lines:
            fh.write(line + "\n")


def recorded(record: str, want: int, timeout: float = 2.0) -> list[str]:
    """The stubs' recorded lines, once there are at least `want` of them."""
    deadline = time.time() + timeout
    lines: list[str] = []
    while True:
        lines = []
        if os.path.exists(record):
            with open(record) as fh:
                lines = [line.rstrip("\n") for line in fh if line.strip()]
        if len(lines) >= want or time.time() >= deadline:
            return lines
        time.sleep(0.05)


def run(binary: str, cfg: str, record: str, home: str, keys: bytes = b"",
        path: str = BIN + ":/usr/bin:/bin", phases=None, settle: float = 2.5,
        after: float = 0.5, until_timeout: float = 10.0, sink: list = None,
        env_extra: dict = None):
    env = {
        "PATH": path,
        "JOT_WAKATIME_RECORD": record,
        "WAKATIME_HOME": home,
        "JOT_WAKATIME_CLI_STUB": CLI_STUB,
    }
    env.update(env_extra or {})
    # `sink` collects every byte the child wrote. A toast is painted once and
    # fades, so the report can be gone from the final screen while its text is
    # still the record of what the editor said -- which is what is being asked.
    on_output = None
    if sink is not None:
        on_output = lambda _fd, data: sink.append(data)
    # Wide enough that no chip is dropped for space: what is being read here is
    # the chip's content, not the bar's priority order (status_clock_probe.py
    # covers the drop).
    return run_in_pty(binary, [os.path.join(ROOT, FILE)], keys, settle=settle,
                      after=after, cols=150, rows=30, cfg=cfg, cwd=ROOT, env=env,
                      phases=phases, until_timeout=until_timeout, on_output=on_output)


def curl_runs(lines) -> list[str]:
    return [line for line in lines if line.startswith("curl|")]


def unzip_runs(lines) -> list[str]:
    return [line for line in lines if line.startswith("unzip|")]


def cli_runs(lines) -> list[str]:
    """The lines a wakatime-cli wrote, which is every line that is not a stub."""
    return [line for line in lines if not line.startswith(("curl|", "unzip|"))]


def heartbeat_runs(lines) -> list[str]:
    return [line for line in cli_runs(lines) if "--entity" in line]


def today_runs(lines) -> list[str]:
    return [line for line in cli_runs(lines) if "--today" in line]


def argv0(line: str) -> str:
    return line.split("|", 1)[0]


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"wakatime probe: SKIP - no binary at {binary}")
        return 2

    write_workspace()
    failures: list[str] = []
    # The install scenes need PATH to really have no cli: if this machine has one
    # in /usr/bin, every one of them would find that instead of installing.
    host_cli = shutil.which("wakatime-cli")
    if host_cli:
        print(f"wakatime probe: note - this machine has {host_cli}, "
              "skipping the install scenes")

    # ── Scene 1: tracking on, one edit and one save, cli already on PATH ─────
    cfg = "/tmp/jot_wakatime_probe_cfg_on"
    record = "/tmp/jot_wakatime_probe_record_on"
    home = HOME + "_on"
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(record):
        os.remove(record)
    # `x` deletes a character, which is a keystroke (a heartbeat without
    # --write) and leaves the buffer modified; the auto-save interval then
    # writes it out, which is the spec's is_write heartbeat. Both come from the
    # real paths -- the input dispatch and Editor::save_buffer_at -- so nothing
    # here is a seam the editor does not already have.
    # The coding chip ships off (workspace/config.cpp holds both time chips off
    # by default), so the scenes that read it ask for it: what is being checked
    # here is which source the chip reports, not whether it is on to begin with
    # (status_clock_probe.py covers the default).
    write_settings(cfg, ["wakatime=true", "wakatime_api_key=probe-key",
                         "status_coding_time=true",
                         "auto_save=true", "auto_save_interval_ms=200"])
    screen = run(binary, cfg, record, home, keys=b"x", phases=[
        (0.5, lambda _s, r=record: len(heartbeat_runs(recorded(r, 1, 0.05))) >= 1),
        (0.5, lambda _s, r=record: len(heartbeat_runs(recorded(r, 2, 0.05))) >= 2),
    ])
    if dump:
        print(screen.text())
        print("-" * 70)

    lines = recorded(record, 2, 1.0)
    hearts = heartbeat_runs(lines)
    today = today_runs(lines)
    entity = os.path.join(ROOT, FILE)
    print(f"wakatime probe: {len(hearts)} heartbeat(s), {len(today)} today run(s)")
    for line in lines:
        print(f"    {line}")

    if len(today) < 1:
        failures.append("the editor never asked the cli for today's total")
    elif "--output json" not in today[0]:
        failures.append(f"today's total was not asked for as json: {today[0]!r}")
    for line in cli_runs(lines):
        if "--key 'probe-key'" not in line and "--key probe-key" not in line:
            failures.append(f"a run is missing the configured api key: {line!r}")
        if "jot-wakatime" not in line:
            failures.append(f"a run is missing the plugin id: {line!r}")
        # The shell reports the path it resolved, so a run of the cli on PATH
        # names the PATH entry: anything else would mean the editor replaced it
        # with a copy of its own.
        if argv0(line) != os.path.join(BIN, "wakatime-cli"):
            failures.append(f"a run did not use the cli from PATH: {line!r}")
    # PATH already has one, so the editor has nothing to install and must not go
    # looking for something to download.
    if curl_runs(lines) or unzip_runs(lines):
        failures.append(f"an install ran with a cli already on PATH: {lines!r}")

    if not hearts:
        failures.append("the edit and the save produced no heartbeat at all")
    else:
        keystrokes = [line for line in hearts if "--write" not in line]
        writes = [line for line in hearts if "--write" in line]
        if not keystrokes:
            failures.append(f"the keystroke produced no heartbeat: {hearts!r}")
        if not writes:
            failures.append("the save produced no --write heartbeat")
        for line in hearts:
            if entity not in line:
                failures.append(f"a heartbeat is not for {entity}: {line!r}")
            if "--time " not in line:
                failures.append(f"a heartbeat has no timestamp: {line!r}")

    # The heartbeat rule is what keeps this from being one spawn per keystroke,
    # so the count has to stay at the events that earned one: the keystroke and
    # the save. A third would mean the rule let a duplicate through.
    if len(hearts) > 3:
        failures.append(f"{len(hearts)} heartbeats from one keystroke and one save")

    # The other half of the chain: what the cli answered has to reach the bar.
    if TODAY_TEXT not in screen.text():
        failures.append(f"the bar does not show the cli's total ({TODAY_TEXT})")
    elif CLOCK_GLYPH not in screen.text():
        failures.append("the chip shows WakaTime's total without its glyph")
    if TIMER_GLYPH in screen.text():
        failures.append("the local-store glyph is up while WakaTime has an answer")

    # ── Scene 1b: a cli that can only say "0 secs" ────────────────────────────
    # The heartbeats the cli is holding have not reached the server yet, which is
    # what its own offline queue looks like from here, and what an account with
    # nothing logged yet says. Either way there is no total to put on the bar: the
    # local store counted the same minutes, so the chip keeps that number and the
    # glyph that names it rather than sitting on a zero through real work.
    cfg = "/tmp/jot_wakatime_probe_cfg_zero"
    record = "/tmp/jot_wakatime_probe_record_zero"
    home = HOME + "_zero"
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(record):
        os.remove(record)
    write_settings(cfg, ["wakatime=true", "wakatime_api_key=probe-key",
                         "status_coding_time=true"])
    seed_local_total(cfg, 9 * 60 * 1000)
    zero = run(binary, cfg, record, home, keys=b"x", after=1.5,
               env_extra={"JOT_WAKATIME_TODAY": ZERO_TODAY})
    if dump:
        print(zero.text())
        print("-" * 70)
    if not today_runs(recorded(record, 1, 1.0)):
        failures.append("the cli was never asked for today's total in the zero scene")
    if ZERO_TEXT in zero.text():
        failures.append("the bar sat on the cli's zero while the local store held 9m")
    # The chip is the last thing on the row, so its trailing space is not part of
    # the extracted text: the label is matched where it starts.
    if not re.search(r"\s9m\b", zero.text()):
        failures.append("the locally stored total is not what the chip shows when the "
                        "cli reports no time")
    if TIMER_GLYPH not in zero.text():
        failures.append("the chip does not say it is showing the local total when the "
                        "cli reports no time")
    if CLOCK_GLYPH in zero.text():
        failures.append("the chip claims WakaTime's total while the cli reported none")

    # ── Scene 2: tracking off, nothing spawned and the local total shown ─────
    cfg = "/tmp/jot_wakatime_probe_cfg_off"
    record = "/tmp/jot_wakatime_probe_record_off"
    if os.path.exists(record):
        os.remove(record)
    write_settings(cfg, ["wakatime=false", "wakatime_api_key=probe-key",
                         "status_coding_time=true"])
    off = run(binary, cfg, record, HOME + "_off", keys=b"x", after=1.5)
    if dump:
        print(off.text())
        print("-" * 70)
    if recorded(record, 1, 0.5):
        failures.append(f"the cli ran with the extension off: {recorded(record, 1)}")
    if TIMER_GLYPH not in off.text():
        failures.append("the local total is not on the bar with WakaTime off")
    if TODAY_TEXT in off.text():
        failures.append("the bar shows a WakaTime total with the extension off")
    if not re.search(r"\s\d+(s|m|h \d+m)", off.text()):
        failures.append("the chip carries no local total with WakaTime off")

    if not host_cli:
        asset = host_asset()
        if not asset:
            print("wakatime probe: note - no release asset for this machine, "
                  "skipping the install scenes")
        else:
            failures.extend(install_scenes(binary, dump, asset))

    if failures:
        for failure in failures:
            print(f"wakatime probe: FAIL - {failure}")
        return 1
    print("wakatime probe: PASS")
    return 0


def install_scenes(binary: str, dump: bool, asset: str) -> list[str]:
    """The three scenes that need a machine with no wakatime-cli on PATH."""
    failures: list[str] = []
    entity = os.path.join(ROOT, FILE)

    # ── Scene 3: no cli on PATH, so the editor installs one ──────────────────
    cfg = "/tmp/jot_wakatime_probe_cfg_install"
    record = "/tmp/jot_wakatime_probe_record_install"
    home = HOME + "_install"
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(record):
        os.remove(record)
    write_settings(cfg, ["wakatime=true", "wakatime_api_key=probe-key",
                         "status_coding_time=true"])
    sink: list = []
    # No key is sent until the install has landed: a keystroke before the cli is
    # ready is dropped on purpose (nothing can be reported yet), so the scene
    # waits for the binary rather than racing it, and only then types.
    install = run(binary, cfg, record, home, keys=b"", sink=sink, path=INSTALL_BIN + ":/usr/bin:/bin",
                  until_timeout=20.0, phases=[
                      (0.5, lambda _s, h=home: os.path.exists(installed_cli(h))),
                      (0.8, b"y"),
                      (0.5, lambda _s, r=record: len(heartbeat_runs(recorded(r, 1, 0.05))) >= 1),
                  ])
    if dump:
        print(install.text())
        print("-" * 70)
    said = b"".join(sink).decode("utf-8", "replace")
    lines = recorded(record, 1, 1.0)
    cli_path = installed_cli(home)
    print(f"wakatime probe: install scene ran {len(curl_runs(lines))} curl, "
          f"{len(unzip_runs(lines))} unzip, {len(cli_runs(lines))} cli run(s)")
    for line in lines:
        print(f"    {line}")

    downloads = curl_runs(lines)
    url = f"releases/latest/download/{asset}"
    if not downloads:
        failures.append("no cli on PATH, and nothing was downloaded")
    elif url not in downloads[0]:
        failures.append(f"the install did not fetch this machine's asset ({asset}): {downloads[0]!r}")
    if not unzip_runs(lines):
        failures.append("the download was never unpacked")
    if not os.path.exists(cli_path):
        failures.append(f"the install left no cli at {cli_path}")
    elif not os.access(cli_path, os.X_OK):
        failures.append(f"the installed cli is not executable: {cli_path}")
    # Every run after the install goes to the managed copy, by absolute path: on
    # a machine whose PATH has no cli, a bare name would not be found at all.
    installed_hearts = [line for line in heartbeat_runs(lines) if argv0(line) == cli_path]
    if not installed_hearts:
        failures.append(f"the heartbeat did not come from the installed cli: {cli_runs(lines)!r}")
    for line in installed_hearts:
        if entity not in line:
            failures.append(f"a heartbeat is not for {entity}: {line!r}")
    if "installed wakatime-cli" not in said:
        failures.append("the install was never reported to the user")
    if TIMER_GLYPH in install.text():
        failures.append("the chip is on the local total with the cli installed and answering")

    # ── Scene 4: a copy from an earlier run is used, not re-downloaded ───────
    cfg = "/tmp/jot_wakatime_probe_cfg_present"
    record = "/tmp/jot_wakatime_probe_record_present"
    home = HOME + "_present"
    shutil.rmtree(home, ignore_errors=True)
    preinstall(home)
    if os.path.exists(record):
        os.remove(record)
    write_settings(cfg, ["wakatime=true", "wakatime_api_key=probe-key",
                         "status_coding_time=true"])
    present = run(binary, cfg, record, home, keys=b"x", path=INSTALL_BIN + ":/usr/bin:/bin",
                  phases=[(0.5, lambda _s, r=record: len(heartbeat_runs(recorded(r, 1, 0.05))) >= 1)])
    if dump:
        print(present.text())
        print("-" * 70)
    lines = recorded(record, 1, 1.0)
    print(f"wakatime probe: already-installed scene ran {len(heartbeat_runs(lines))} heartbeat(s)")
    for line in lines:
        print(f"    {line}")
    if curl_runs(lines) or unzip_runs(lines):
        failures.append(f"the cli was downloaded again over the installed copy: {lines!r}")
    if not [line for line in heartbeat_runs(lines) if argv0(line) == installed_cli(home)]:
        failures.append(f"the installed copy was not the one run: {cli_runs(lines)!r}")
    if TODAY_TEXT not in present.text():
        failures.append("the installed cli's total did not reach the bar")

    # ── Scene 5: the install cannot download ────────────────────────────────
    # A machine that is offline, and the shape of that has to be honest: one
    # report, no spawns, and the local total still on the bar.
    cfg = "/tmp/jot_wakatime_probe_cfg_failed"
    record = "/tmp/jot_wakatime_probe_record_failed"
    home = HOME + "_failed"
    shutil.rmtree(home, ignore_errors=True)
    if os.path.exists(record):
        os.remove(record)
    write_settings(cfg, ["wakatime=true", "wakatime_api_key=probe-key",
                         "status_coding_time=true"])
    sink = []
    failed = run(binary, cfg, record, home, keys=b"x", sink=sink, after=2.0,
                 path=FAIL_BIN + ":/usr/bin:/bin")
    if dump:
        print(failed.text())
        print("-" * 70)
    said = b"".join(sink).decode("utf-8", "replace")
    lines = recorded(record, 1, 1.0)
    print(f"wakatime probe: failed-install scene ran {len(curl_runs(lines))} curl run(s)")
    for line in lines:
        print(f"    {line}")
    if not curl_runs(lines):
        failures.append("a failed download was not even attempted")
    if "could not install wakatime-cli" not in said:
        failures.append("a failed install was not reported to the user")
    if cli_runs(lines):
        failures.append(f"a run happened without a cli: {cli_runs(lines)!r}")
    if os.path.exists(installed_cli(home)):
        failures.append("a failed install still left a cli behind")
    if TIMER_GLYPH not in failed.text():
        failures.append("the chip lost its local fallback when the install failed")
    if TODAY_TEXT in failed.text():
        failures.append("the chip shows a WakaTime total with no cli at all")

    return failures


if __name__ == "__main__":
    sys.exit(main())
