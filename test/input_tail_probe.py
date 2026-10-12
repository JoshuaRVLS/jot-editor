#!/usr/bin/env python3
"""Probe: the tail of an escape sequence the editor abandoned never becomes text.

The reader consumes a terminal reply byte by byte, and two of its paths give up
part-way: the size probe stops at its deadline, and a mouse report whose next
byte is more than 5 ms away stops there. Both had already eaten the head of the
sequence. Its tail stayed in the pty, where the next read took it for typing --
a DSR position reply ("ESC [ 12 ; 9 R") showing up in the buffer as `9R`, a mouse
report ("ESC [ < 0 ; 12 ; 3 M") as `;12;3M`. This drives the real binary and types
into a file after splitting a report across a gap, then reads the buffer back.

The same tail is typed when the gap is not milliseconds but a busy frame. A
terminal writes a report in one call, the pty hands it over in pieces while the
app is not draining it, and the app can spend seconds drawing before it reads
again. Those tails -- "35;30;19M" from a pointer storm, ";5u" from a held key,
the body of a report the pty chopped right after `ESC [` -- are what a claim has
to outlive, so the cases below split reports by that much as well.

Usage: test/input_tail_probe.py [path-to-jot]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 100, 24
SOURCE = "alpha\nbeta\ngamma\n"

# The two truncations, split where the reader times out: the head as one write,
# the tail late. An SGR mouse press, and a DSR cursor-position reply.
MOUSE_HEAD, MOUSE_TAIL = b"\x1b[<0;5", b";12;3M"
CPR_HEAD, CPR_TAIL = b"\x1b[1", b"2;9R"

# What a leaked tail looks like once it is buffer text: the parameters and the
# final byte, with the `ESC [` the reader ate no longer in front of them. A key
# report's tail is its parameters and then a `u`, and where the chop fell
# decides how much of the parameters is left (`105;5u`, `;5u`, `5u`).
LEAKED = re.compile(r"[;0-9]{2,}[Mm]|[;:0-9]{2,}u")


def leaked_line(screen) -> str:
    """The first line carrying a leaked report tail, or "" if there is none."""
    for line in screen.text().split("\n"):
        if LEAKED.search(line):
            return line.strip()[:70]
    return ""


def workspace(tmp: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    path = os.path.join(tmp, "a.txt")
    with open(path, "w") as fh:
        fh.write(SOURCE)
    return path


def run(binary: str, tmp: str, path: str, head: bytes, tail: bytes, typed: bytes,
        after: float = 0.05):
    """Splits `head` and `tail` across `after` seconds, then types `typed`."""
    return run_in_pty(binary, [path], head, settle=2.5, after=after,
                      cfg=tmp + "_cfg", cwd=tmp, cols=COLS, rows=ROWS,
                      phases=[(0.02, tail), (0.3, typed)])


def explorer_pointer_storm(binary: str, tmp: str) -> list:
    """The field flow: the explorer's `a`, and a report's tail a frame late.

    `a` in the explorer opens the command palette on `mkfile `, and that first
    frame is a stall: the reports the terminal had queued come apart across it,
    and the run that filled a file with `35;30;19M...` started here -- the name
    that arrived with a report welded to its front (`35;30;19Mmade.txt`).

    What this asserts is the input reader's side of it: no report's tail is
    typed. Where the name typed after the storm lands is reported and not
    asserted, because that is another surface's contract: it needs the sidebar
    to hold the focus (test/sidebar_root_probe.py) and no startup popup to be
    up, and a machine whose query compiles fail gets a modal notice a few
    seconds in that swallows whatever is typed under it.
    """
    root = os.path.join(tmp, "workspace")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "child"))
    failures = []

    # A language whose bundled query would not compile is reported on a popup a
    # few seconds into the run (see runtime/lua/treesitter/init.lua), and that
    # popup is modal: whatever is typed under it never reaches the surface. The
    # wait below covers a notice that has already shown; when one lands later,
    # the pass is simply repeated, so a machine whose query compiles are slow
    # decides nothing here either way.
    seen = []
    for attempt in range(2):
        cfg = tmp + "_root_cfg%d" % attempt
        state = {"notice": False, "start": time.monotonic()}

        def notice_done(screen) -> bool:
            if "bundled highlight" in screen.text():
                state["notice"] = True
                return False
            return state["notice"] or time.monotonic() - state["start"] > 5.0

        screen = run_in_pty(binary, [root], b"", settle=2.5, after=0.2, cols=COLS,
                            rows=ROWS, cfg=cfg, cwd=root,
                            phases=[(0.5, notice_done),
                                    (0.2, b"\x1b[<0;9;2M\x1b[<0;9;2m"),
                                    (0.2, b"j"), (0.2, b"k"), (0.3, b"a"),
                                    (0.3, b"\x1b[<"), (1.6, b"35;30;19M"),
                                    (0.3, b"made.txt"), (0.3, b"\r"),
                                    # A slow machine paints its frame late, so wait
                                    # for the name rather than reading right after
                                    # the keystroke.
                                    (0.2, lambda s: "made.txt" in s.text())])
        text = screen.text()
        where = leaked_line(screen)
        print("explorer pointer storm: report tail on screen: %s (attempt %d)"
              % (where or "no", attempt + 1))
        if where:
            failures.append("explorer pointer storm: a report's tail was typed: %r" % where)
            return failures
        seen.append("made.txt" in text)

    print("explorer pointer storm: the name typed after the storm landed: %s" % any(seen))
    return failures


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print("input tail probe: SKIP - no binary at %s" % binary)
        return 2

    tmp = "/tmp/jot_input_tail_probe"
    path = workspace(tmp)
    failures = []

    cases = [
        ("truncated mouse report", MOUSE_HEAD, MOUSE_TAIL, b";12;3M"),
        ("truncated position reply", CPR_HEAD, CPR_TAIL, b"2;9R"),
    ]
    for label, head, tail, leaked in cases:
        screen = run(binary, tmp, path, head, tail, b"zz")
        text = screen.text()
        typed = "zz" in text
        print("%s: tail %r on screen: %s" % (label, leaked.decode(), leaked.decode() in text))
        if not typed:
            failures.append("%s: the text typed after the sequence never reached the buffer"
                            % label)
        if leaked.decode() in text:
            failures.append("%s: the sequence tail %r was read back as typed text"
                            % (label, leaked.decode()))

    # A report whose tail is a whole frame late. The terminal's write was
    # chopped by a full pty while the app was drawing, so the head and the tail
    # are read seconds apart; the tail is still machine traffic and must not be
    # typed, and the keystrokes that follow it must still land.
    late = [
        ("mouse report tail after a busy frame", b"\x1b[<", b"35;30;19M", "35;30;19M", 1.5),
        ("key report tail after a busy frame", b"\x1b[105", b";5u", ";5u", 1.5),
        ("report chopped after ESC [", b"\x1b[", b"<35;30;19M", "<35;30;19M", 0.05),
    ]
    for label, head, tail, leaked, gap in late:
        screen = run(binary, tmp, path, head, tail, b"zz", after=gap)
        text = screen.text()
        print("%s: tail %r on screen: %s" % (label, leaked, leaked in text))
        if "zz" not in text:
            failures.append("%s: the text typed after the sequence never reached the buffer"
                            % label)
        if leaked in text:
            failures.append("%s: the sequence tail %r was read back as typed text"
                            % (label, leaked))

    # Holding those claims longer must not eat keystrokes whose bytes look like
    # a report's: the brackets, digits and semicolons a person types still land.
    screen = run(binary, tmp, path, b"\x1b[<35;30;19M\x1b[<0;4;1M\x1b[<0;4;1m", b"",
                 b" int a[12];")
    typed_like_a_report = "int a[12];" in screen.text()
    print("typing that looks like a report's bytes: on screen: %s" % typed_like_a_report)
    if not typed_like_a_report:
        failures.append("typing that looks like a report's bytes did not reach the buffer")

    # A report that arrives whole still has to be one mouse event and no text.
    whole = b"\x1b[<0;5;7M\x1b[<0;5;7m"
    screen = run(binary, tmp, path, whole, b"", b"zz")
    text = screen.text()
    print("complete mouse report: click on screen: %s" % (";7M" in text))
    if "zz" not in text:
        failures.append("a complete mouse report swallowed the next keystroke")
    if ";7M" in text:
        failures.append("a complete mouse report was read back as typed text")

    failures.extend(explorer_pointer_storm(binary, tmp))

    if failures:
        print("input tail probe: FAIL")
        for failure in failures:
            print("  - %s" % failure)
        return 1
    print("input tail probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
