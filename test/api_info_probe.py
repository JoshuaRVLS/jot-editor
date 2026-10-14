#!/usr/bin/env python3
"""Probe: the Lua API describes itself, and the two answers agree.

The surface a plugin is told to gate on (jot.capabilities) used to be a list
kept by hand next to the bindings, and the two drifted: it named namespaces that
do not exist and missed ones that do. The description is now read off the table
that init() built, so what is asserted here is that both readers of it are
reading the same build:

  * `jot --api-info` prints the surface to a pipe, with no terminal setup in the
    output (the editor's constructor paints the alternate screen), one sorted
    callable per line behind a header carrying the counts;
  * inside a session, jot.api.functions() and jot.api.namespaces() report those
    same two counts, jot.api.has() answers for a name that is there and one that
    is not, and jot.capabilities() carries the namespaces the hand-written list
    had missed.

Exit 0 pass, 1 fail, 2 skip.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

BINARY = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
CFG = "/tmp/jot_api_info_probe_cfg"
DUMP_CFG = "/tmp/jot_api_info_probe_dump"
WORK = "/tmp/jot_api_info_probe_work"

# Names a plugin reaches for, and the ones the hand-written list had missed.
WANTED = [
    "buffer.apply_edit",
    "decoration.set",
    "policy.set",
    "undo.tree",
    "screen.write",
    "ui.float.open",
    "events.subscribe",
    "keymap.set",
    "timer.set_timeout",
    "file.write",
    "capabilities",
    "autocmd",
    "command",
    "api.has",
]
# The hand-written list named namespaces that do not exist ("selection",
# "edit") and missed these, which do.
MISSED_BY_THE_OLD_LIST = ["decoration", "policy", "autocmd", "syntax", "preview", "emmet"]

HEADER = re.compile(r"^# jot Lua API (\S+): (\d+) namespaces, (\d+) functions$")

INIT_LUA = r"""
local caps = jot.capabilities()
local info = jot.api.info()

local function yes(name)
  return caps[name] and "y" or "n"
end

local missing = {}
for _, name in ipairs({ "decoration", "policy", "autocmd", "syntax", "preview", "emmet" }) do
  if not caps[name] then
    missing[#missing + 1] = name
  end
end

jot.screen.on_paint(function(w, h)
  if w < 60 or h < 14 then
    return
  end
  jot.screen.write(2, 3, "VER=" .. tostring(info.version) .. "|" .. tostring(jot.api_version))
  jot.screen.write(2, 4, "COUNTS=" .. tostring(#jot.api.namespaces())
                   .. "," .. tostring(#jot.api.functions()))
  jot.screen.write(2, 5, "CAPS=" .. yes("decoration") .. yes("ui") .. yes("policy") .. yes("autocmd"))
  jot.screen.write(2, 6, "MISSING=" .. (#missing == 0 and "none" or table.concat(missing, ",")))
  jot.screen.write(2, 7, "HAS=" .. (jot.api.has("buffer.apply_edit") and "y" or "n")
                   .. "," .. (jot.api.has("buffer.no_such_thing") and "y" or "n")
                   .. "," .. (jot.api.has("ui.float.open") and "y" or "n"))
end)
"""


def dump() -> tuple[int, str, str]:
    """Run `jot --api-info` on a pipe, with a config dir of its own."""
    shutil.rmtree(DUMP_CFG, ignore_errors=True)
    os.makedirs(DUMP_CFG)
    env = dict(os.environ, JOT_CONFIG_HOME=DUMP_CFG, JOT_CACHE_HOME=DUMP_CFG, HOME=DUMP_CFG)
    result = subprocess.run([os.path.abspath(BINARY), "--api-info"],
                            capture_output=True, text=True, env=env, cwd=WORK or None, timeout=120)
    return result.returncode, result.stdout, result.stderr


def main() -> int:
    if not os.path.exists(BINARY):
        print(f"skip: {BINARY} is not built")
        return 2
    for path in (CFG + "/lua", CFG + "/plugins", WORK):
        os.makedirs(path, exist_ok=True)
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(INIT_LUA)
    target = os.path.join(WORK, "probe.txt")
    with open(target, "w") as fh:
        fh.write("api info probe\n")

    failures: list[str] = []

    # --- 1. the dump on a pipe ---------------------------------------------
    code, out, err = dump()
    if code != 0:
        failures.append(f"jot --api-info exited {code}: {err.strip()[:200]}")
    if "\x1b" in out:
        failures.append("the dump carries terminal escapes, so the editor's setup leaked into it")
    lines = [line for line in out.split("\n") if line.strip()]
    if not lines:
        failures.append("the dump is empty")
        print("api info probe: FAIL")
        for line in failures:
            print("  - %s" % line)
        return 1
    match = HEADER.match(lines[0])
    if not match:
        failures.append(f"the header does not read as one: {lines[0]!r}")
    body = lines[1:]
    if body != sorted(body):
        failures.append("the function list is not sorted")
    if len(set(body)) != len(body):
        failures.append("the function list repeats a name")
    for name in WANTED:
        if name not in body:
            failures.append(f"the dump does not carry {name}")

    # --- 2. the same answers from inside a session -------------------------
    screen = run_in_pty(BINARY, [target], b"", settle=3.0, after=2.0, cfg=CFG, cwd=WORK)
    rows = ["".join(cells).rstrip() for cells in screen.cells]

    def row(index: int) -> str:
        return rows[index]

    def field(index: int, key: str) -> str:
        text = row(index)
        at = text.find(key + "=")
        if at < 0:
            failures.append(f"row {index} has no {key}: {text!r}")
            return ""
        return text[at + len(key) + 1:].split("  ")[0].strip()

    if field(3, "VER") not in (None, ""):
        version = field(3, "VER")
        if match and version != match.group(1) + "|" + match.group(1):
            failures.append(f"the session reports version {version!r}, the dump {match.group(1)!r}")
    counts = field(4, "COUNTS")
    if match and counts:
        if counts != f"{match.group(2)},{match.group(3)}":
            failures.append(
                f"the session counts {counts}, the dump header {match.group(2)},{match.group(3)}")
    if field(5, "CAPS") not in (None, "", "yyyy"):
        failures.append(f"jot.capabilities() answered {field(5, 'CAPS')!r}, wanted yyyy")
    if field(6, "MISSING") not in (None, "", "none"):
        failures.append(f"namespaces missing from jot.capabilities(): {field(6, 'MISSING')}")
    if field(7, "HAS") not in (None, "", "y,n,y"):
        failures.append(f"jot.api.has answered {field(7, 'HAS')!r}, wanted y,n,y")

    if failures:
        print("api info probe: FAIL")
        for line in failures:
            print("  - %s" % line)
        print("--- screen ---")
        print(screen.text())
        return 1
    print("ok: --api-info and jot.api agree; %d functions, %s"
          % (len(body), match.group(0)[2:] if match else "no header"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
