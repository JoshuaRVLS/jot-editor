#!/usr/bin/env python3
"""Probe: an on-save prettier run leaves the highlighting matching the bytes.

`prettier_on_save` runs the formatter after the buffer is written and then swaps
the formatted lines into the buffer through the external-rewrite path: the lines
are replaced and the syntax cache is invalidated, with no edit hook snapshotting
the text the tree-sitter tree was parsed from. The invalidation has to mark the
tree out of sync too (Editor::invalidate_syntax_cache). It once dropped only the
per-line colour cache, so the next paint trusted a tree describing the replaced
bytes: every highlight past a prettier reflow came from the old offsets, and
stayed that way (nothing marks the tree out of sync again).

The probe opens a 220-line TypeScript file with 4-space indentation and a long
call, saves it with `reindent_on_save` off so prettier is the only rewrite,
scrolls to lines no frame had painted before the save, and compares what it sees
with the same bytes opened fresh from disk.

Usage: test/prettier_save_syntax_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, or prettier did not run).
"""
from __future__ import annotations

import os
import re
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

LINES = 220
CODE_ROW = re.compile(r"^\s*(\d+)\s")


def probe_text() -> str:
    """A TS file prettier will restyle: 4-space steps and overlong calls."""
    out = ["function probe(): number {"]
    for i in range(1, LINES - 1):
        if i % 3 == 0:
            out.append('    const value%d = call(%d, "text %d", %d);' % (i, i, i, i * 2))
        elif i % 3 == 1:
            out.append('    const text%d = "note %d with words in it";' % (i, i))
        else:
            out.append("    // line %d" % i)
    out.append("    return 0;")
    out.append("}")
    # One call long enough that prettier has to break it, so the rewrite is not
    # only a change of indentation width.
    out.insert(2, '    const long = call(alpha, beta, gamma, delta, epsilon, "zeta");')
    return "\n".join(out) + "\n"


def write_file(path: str, text: str) -> None:
    with open(path, "w") as fh:
        fh.write(text)


def code_rows(screen):
    """The painted code rows as (line number, text, colours)."""
    rows = screen.text().splitlines()
    out = []
    for y, text in enumerate(rows):
        match = CODE_ROW.match(text)
        if not match:
            continue
        start = match.start(1)
        out.append((int(match.group(1)), text.rstrip(), list(screen.fg[y][start:len(text)])))
    return out


def wait_for(needle: str):
    def check(screen) -> bool:
        return needle in screen.text()

    return check


def capture_after(seconds: float):
    shots = {}
    state = {"t0": None}

    def check(screen) -> bool:
        if state["t0"] is None:
            state["t0"] = time.time()
        if time.time() - state["t0"] > seconds:
            shots["rows"] = code_rows(screen)
            return True
        return False

    return check, shots


def run(binary: str, path: str, cfg: str, cwd: str, save: bool):
    """Opens the file, saves when asked, scrolls, and returns the code rows."""
    phases = []
    if save:
        phases.append((1.5, b"\x13"))
        # The formatter runs after the write; wait for its status line rather
        # than guessing how long npx takes.
        phases.append((0.0, wait_for("formatted: prettier")))
    for _ in range(12):
        phases.append((0.35, b"\x1b[6~"))
    phases.append((0.4, b"\x1b[6~"))
    check, shots = capture_after(1.5)
    phases.append((0.0, check))
    # The child runs in the probe's own directory, so the formatter it shells
    # out to cannot pick up a config from the repository the probe runs in.
    run_in_pty(binary, [path], b"", settle=2.5, after=0.5, cols=110, rows=26,
               cfg=cfg, cwd=cwd, phases=phases, until_timeout=180.0)
    return shots.get("rows", [])


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("prettier save syntax probe: SKIP - no binary at %s" % binary)
        return 2

    work = "/tmp/jot_prettier_save_syntax_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    path = os.path.join(work, "probe.ts")

    cfg = os.path.join(work, "cfg")
    os.makedirs(os.path.join(cfg, "configs"), exist_ok=True)
    # The built-in reindent off, so the rewrite under test is prettier's alone.
    with open(os.path.join(cfg, "configs", "settings.conf"), "w") as fh:
        fh.write("reindent_on_save = false\n")
        fh.write("trim_trailing_whitespace_on_save = false\n")
        fh.write("auto_save = false\n")

    original = probe_text()
    write_file(path, original)
    after_save = run(binary, path, cfg, work, save=True)
    with open(path) as fh:
        saved = fh.read()

    if saved == original:
        print("prettier save syntax probe: SKIP - prettier did not rewrite the file"
              " (no npx/prettier here?)")
        return 2

    fresh = run(binary, path, cfg, work, save=False)

    if not after_save or not fresh:
        print("prettier save syntax probe: FAIL - no code rows were painted")
        return 1

    fresh_by_line = {num: (text, colors) for num, text, colors in fresh}
    mismatches = []
    for num, text, colors in after_save:
        other = fresh_by_line.get(num)
        if other is None:
            continue
        if other[0] != text or other[1] != colors:
            mismatches.append((num, text, colors, other))

    if dump:
        print("--- after save ---")
        for num, text, colors in after_save:
            print(num, repr(text[:60]), colors[:12])
        print("--- fresh ---")
        for num, text, colors in fresh:
            print(num, repr(text[:60]), colors[:12])

    if mismatches:
        num, text, colors, other = mismatches[0]
        print("prettier save syntax probe: FAIL - %d row(s) highlighted differently"
              " after the save" % len(mismatches))
        print("  line %d: %r" % (num, text[:70]))
        print("  after save: %s" % (colors,))
        print("  fresh open: %s" % (other[1],))
        return 1

    print("prettier save syntax probe: PASS - %d rows match a fresh open"
          % len(after_save))
    return 0


if __name__ == "__main__":
    sys.exit(main())
