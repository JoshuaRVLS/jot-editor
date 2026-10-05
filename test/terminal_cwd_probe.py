#!/usr/bin/env python3
"""Probe: which directory a terminal opened with the editor starts in.

The workspace root is the editor's own answer to "which directory": the explorer
walks it, the debugger launches in it, a plugin job's terminal starts there. A
terminal used to inherit jot's process cwd instead, which is the directory jot
was launched from. Those are the same only when the workspace arrived as an
argument, because main chdirs into it: a workspace resumed with no arguments, or
opened later from the home menu, leaves the process where it was, so the shell
came up outside the workspace.

Three scenes, each read through the shell's own report of its directory
(`echo "cwd:$PWD"`, and `cwd2:` after a restart):
  * resume: a recent workspace is seeded and jot runs with no arguments from a
    different directory, so the two agree only if the terminal followed the
    workspace;
  * argument: jot is given the workspace directory, the case that already
    worked, pinned so the fix cannot trade one scene for another;
  * restart: a resumed workspace again, where the shell is exited and the
    panel is clicked to bring it back, and it must come back where it was
    rather than in the launch directory the fix just removed from that path.

Usage: test/terminal_cwd_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 140, 36
WS = "/tmp/jot_tc_ws"
WS2 = "/tmp/jot_tc_ws2"
WS3 = "/tmp/jot_tc_ws3"
LAUNCH = "/tmp/jot_tc_launch"
HOME = "/tmp/jot_tc_home"
CFG = "/tmp/jot_tc_cfg"
CFG2 = "/tmp/jot_tc_cfg2"
CFG3 = "/tmp/jot_tc_cfg3"

# Ctrl+Shift+P opens the command palette; Enter runs what is typed in it. The
# palette rather than a `:termnew` typed at the status line because the
# workspace opens with the explorer focused, where a plain `:` is a tree key.
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"
NEW_TERM = b"termnew"
CWD = b'echo "cwd:$PWD"\r'
CWD2 = b'echo "cwd2:$PWD"\r'
EXIT = b"exit\r"
# A click inside the terminal panel's content, below its bar and above the
# status line at 36 rows. A shell that has exited hands the focus back, so
# typing no longer reaches it: the click is what refocuses the panel and
# restarts the shell.
PANEL_CLICK = b"\x1b[<0;5;31M\x1b[<0;5;31m"
# The shell the palette's command creates, as the session list names it. Waiting
# for this row is waiting for a terminal to exist at all: the bar is drawn for an
# empty view too, and a click landing on that one focuses nothing.
SHELL_ROW = "term 1"


def rows(screen) -> list[str]:
    return [line.strip() for line in screen.text().split("\n")]


def cwd_report(prefix: str, expect: str, into: dict):
    """Waits for the shell's `echo "<prefix>$PWD"` answer, and records it.

    The answer is read here rather than off the final screen: the panel
    scrolls, and by the end of a scene the line a phase waited for may have
    gone, which would read as a shell that never answered. A row is read by
    the marker it carries, not matched whole: the session list's divider runs
    down the panel's right side, so the shell's own rows carry it too.
    """
    wanted = {expect, LAUNCH}

    def phase(screen) -> bool:
        for row in rows(screen):
            if prefix not in row:
                continue
            answer = row.split(prefix, 1)[1].split()[0]
            if answer in wanted:
                into[prefix] = prefix + answer
                return True
        return False

    return phase


def write_workspace(root: str) -> None:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    with open(os.path.join(root, "a.cpp"), "w") as fh:
        fh.write("int a = 1;\n")


def seed_recent_workspace(cfg: str, ws: str) -> None:
    """Points the resume path at `ws`: jot with no arguments reads this list."""
    shutil.rmtree(cfg, ignore_errors=True)
    os.makedirs(os.path.join(cfg, "configs"))
    with open(os.path.join(cfg, "configs", "recent_workspaces.txt"), "w") as fh:
        fh.write(ws + "\n")


def open_terminal(binary: str, args, cwd: str, cfg: str, marker: str, expect: str,
                  restart: bool, dump: bool):
    """Runs one scene and returns (final screen, {prefix: the shell's answer})."""
    report: dict[str, str] = {}
    phases = [
        (0.3, lambda s: any(marker in row for row in rows(s))),
        (0.6, PALETTE),
        (0.8, NEW_TERM),
        (0.6, ENTER),
        # The shell has to exist before the click can refocus it: a click
        # landing on the panel while it has no terminal focuses nothing, and
        # the echo below never reaches a shell.
        (0.4, lambda s: any(SHELL_ROW in row for row in rows(s))),
        # The click is what hands the keys to the panel: the command that
        # creates the terminal and the panel's own focus have to have settled,
        # and a click states it instead of racing the palette's close.
        (0.6, PANEL_CLICK),
        # A shell flushes what is already in the pty when it takes the terminal
        # over (tcsetattr TCSAFLUSH at startup), so the echo waits for the
        # prompt to be on screen: a line typed before that is simply lost.
        (0.3, lambda s: any("$" in row for row in rows(s))),
        (0.2, CWD),
        (0.2, cwd_report("cwd:", expect, report)),
    ]
    if restart:
        phases += [
            (0.8, EXIT),
            (0.6, PANEL_CLICK),
            (1.5, CWD2),
            (0.2, cwd_report("cwd2:", expect, report)),
        ]
    screen = run_in_pty(binary, args, b"", settle=5.0, after=0.6, cols=COLS, rows=ROWS,
                        cfg=cfg, cwd=cwd, phases=phases)
    return screen, report


def check(label: str, prefix: str, expect: str, report: dict, failures: list[str]) -> None:
    if report.get(prefix) != prefix + expect:
        failures.append("%s: wanted %r, the shell reported %r"
                        % (label, prefix + expect, report.get(prefix)))


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("terminal cwd probe: SKIP - no binary at %s" % binary)
        return 2

    # An empty HOME so no shell rc file can `cd` the shell somewhere of its
    # own: what is measured is the directory the editor started it in.
    shutil.rmtree(HOME, ignore_errors=True)
    os.makedirs(HOME)
    os.environ["HOME"] = HOME
    if os.path.exists("/bin/bash"):
        os.environ["SHELL"] = "/bin/bash"

    failures: list[str] = []

    # Scene 1: the workspace is resumed, not passed, so the process directory
    # and the workspace are different directories on purpose.
    write_workspace(WS)
    shutil.rmtree(LAUNCH, ignore_errors=True)
    os.makedirs(LAUNCH)
    seed_recent_workspace(CFG, WS)
    screen, report = open_terminal(binary, [], LAUNCH, CFG, "jot_tc_ws", WS, False, dump)
    if dump:
        print(screen.text())
        print("-" * 70)
    if not any("jot_tc_ws" in row for row in rows(screen)):
        failures.append("resume: the workspace never came up on screen")
    check("resume", "cwd:", WS, report, failures)
    print("resume:   %s" % ("ok" if not failures else "FAILED"))

    # Scene 2: the workspace as an argument (main chdirs into it), which pins
    # the behaviour the resume scene must end up matching.
    write_workspace(WS2)
    shutil.rmtree(CFG2, ignore_errors=True)
    before = len(failures)
    screen, report = open_terminal(binary, [WS2], LAUNCH, CFG2, "jot_tc_ws2", WS2, False, dump)
    if dump:
        print(screen.text())
        print("-" * 70)
    check("argument", "cwd:", WS2, report, failures)
    print("argument: %s" % ("ok" if len(failures) == before else "FAILED"))

    # Scene 3: the shell is exited and the panel is clicked to bring it back,
    # the path where a dead terminal comes back. A resumed workspace again, for
    # the same reason as scene 1: with the workspace passed as an argument the
    # process directory and the workspace are the same, and a shell that
    # restarted in either would read the same.
    write_workspace(WS3)
    seed_recent_workspace(CFG3, WS3)
    before = len(failures)
    screen, report = open_terminal(binary, [], LAUNCH, CFG3, "jot_tc_ws3", WS3, True, dump)
    if dump:
        print(screen.text())
        print("-" * 70)
    check("restart", "cwd:", WS3, report, failures)
    check("restart", "cwd2:", WS3, report, failures)
    print("restart:  %s" % ("ok" if len(failures) == before else "FAILED"))

    if failures:
        print("terminal cwd probe: FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("terminal cwd probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
