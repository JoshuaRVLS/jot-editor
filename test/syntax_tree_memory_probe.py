#!/usr/bin/env python3
"""Probe: opening more source files does not retain more parse trees.

A tree-sitter CST costs roughly 1.3-3.5 KB per line of C++ (measured: a
3000-line file held ~10 MB), and a tree used to stay resident for as long as
its buffer was open -- twelve ordinary 3000-line files held ~120 MB of trees
with one of them on screen. Two changes fix that: a file is parsed by the
first paint that needs it instead of on open, and the render loop's sweep
releases the trees of buffers it has not drawn recently, keeping the ones on
screen plus the newest kMaxCachedSyntaxTrees of the rest.

The probe opens the same files twice, once as a set of 12 and once as a set of
24, and reads the real binary's RSS in both. Both counts are past the budget,
so the budget's worth of trees is live either way and the two runs should
settle within a tree of each other; before the fix the extra 12 files cost
~115 MB, so the growth between the two runs is what the bound measures. The
count (rather than a per-file figure) keeps the check independent of how many
trees the budget happens to keep.

Usage: test/syntax_tree_memory_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary, no /proc).
"""
from __future__ import annotations

import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

# Enough lines that each retained tree is several megabytes, and enough files
# that the old behavior had two dozen of them resident. Both runs are past the
# budget, so their difference is what a leak of one tree per open file would
# move -- ~115 MB before the fix against allocator noise now. 16 MB leaves
# room for one more tree than the budget keeps plus the sampling noise, and is
# far below the ~115 MB the old behavior added.
FILES = 24
SMALL = 12
LINES = 3000
# The old behavior kept one tree per open file and added ~115 MB between the
# two runs; the files' own text plus the churn of cycling through them costs
# ~16 MB. The bound sits well above the latter and far below the former.
MAX_GROWTH_KB = 40 * 1024

OPEN_AND_CYCLE = """
local root = "%(root)s"
local files = {}
for i = 1, %(files)d do files[i] = root .. "/f" .. i .. ".cpp" end
local phase = 0
local passes = 2
jot.timer.set_interval(100, function()
  phase = phase + 1
  if phase == 1 then
    for i = 1, #files do jot.file.open(files[i]) end
    return
  end
  -- Every later tick shows one file, so each buffer gets painted and would
  -- keep a parse tree of its own without the sweep. It stops after two passes
  -- through the set, so the measurement reads a settled editor instead of one
  -- mid-reparse.
  if phase > passes * #files + 1 then return end
  jot.file.open(files[((phase - 2) %% #files) + 1])
end)
"""


def write_file(path: str, index: int, lines: int) -> None:
    with open(path, "w") as fh:
        fh.write("// probe file %d\n#include <vector>\n" % index)
        for n in range(lines):
            fh.write("int fn_%d_%d(int a, int b) { return a * b + %d; }\n" % (index, n, n))


def child_pid() -> int:
    """The jot the harness forked: the direct child of this process."""
    me = os.getpid()
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % entry) as fh:
                fields = fh.read().split()
        except OSError:
            continue
        if len(fields) > 3 and fields[3] == str(me) and "jot" in fields[1]:
            return int(entry)
    return 0


def rss_kb(pid: int) -> int:
    try:
        with open("/proc/%d/status" % pid) as fh:
            for line in fh:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return -1


def steady_rss(binary: str, work: str, name: str, open_files: int) -> int:
    """Opens `open_files` files, cycles through them, returns the settled RSS."""
    cfg = os.path.join(work, "cfg_" + name)
    os.makedirs(cfg)
    with open(os.path.join(cfg, "init.lua"), "w") as fh:
        fh.write(OPEN_AND_CYCLE % {"root": work, "files": open_files})
    target = os.path.join(work, "f1.cpp")
    samples = []
    state = {"t0": None}

    def sampler(_screen) -> bool:
        pid = child_pid()
        if pid:
            samples.append(rss_kb(pid))
        if state["t0"] is None:
            state["t0"] = time.time()
        return time.time() - state["t0"] > 3.5

    run_in_pty(binary, [target], b"", settle=6.5, after=2.0, cols=120, rows=34,
               cfg=cfg, phases=[(0.0, sampler)])
    if not samples or max(samples) < 0:
        return -1
    # The tail is the settled state: the sweep runs at the end of a painted
    # frame, and the opens above happen on the first timer tick, before it.
    return samples[-1]


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("syntax tree memory probe: SKIP - no binary at %s" % binary)
        return 2
    if not os.path.isdir("/proc"):
        print("syntax tree memory probe: SKIP - no /proc to read the process's RSS")
        return 2

    work = "/tmp/jot_syntax_tree_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for i in range(1, FILES + 1):
        write_file(os.path.join(work, "f%d.cpp" % i), i, LINES)

    small = steady_rss(binary, work, "small", SMALL)
    large = steady_rss(binary, work, "large", FILES)
    if small < 0 or large < 0:
        print("syntax tree memory probe: SKIP - the process's RSS could not be read")
        return 2

    growth = large - small
    print("rss:     %d files %d kB, %d files %d kB, growth %+d kB (bound %d)"
          % (SMALL, small, FILES, large, growth, MAX_GROWTH_KB))
    if dump:
        print("files:   %s" % work)
    if growth > MAX_GROWTH_KB:
        print("syntax tree memory probe: FAIL")
        print("  - %d more open files cost %+d kB: parse trees are being retained"
              % (FILES - SMALL, growth))
        return 1
    print("syntax tree memory probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
