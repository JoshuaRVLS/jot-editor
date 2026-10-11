#!/usr/bin/env python3
"""Run the end-to-end probes under test/ and report each one's own verdict.

The probes are the checks that drive the real binary over a pty and assert on
the painted screen: the only coverage of the parts a unit test cannot see. Each
one is a script with its own usage line and its own exit codes, so they were run
one at a time by hand, and a probe whose reader went stale sat red without
anyone noticing -- two of them kept grepping the status line for the `@ server`
chip long after src/render/status_line.cpp trimmed that chip to the language
alone, and both had been passing the whole time on the editor's side.

A probe's own exit codes carry the verdict: 0 pass, 1 fail, 2 skipped (a
prerequisite it does not have -- no clangd, no git, no Xvfb). A probe that runs
past its timeout fails: these drive real processes, so one that hangs is a bug
in the probe rather than a slow machine to keep waiting for.

  tools/run_probes.py
  tools/run_probes.py --jobs 8
  tools/run_probes.py --only 'cpp_*,fold*'
  tools/run_probes.py --skip 'gui_*'        # no display on this box
  tools/run_probes.py --timeout 600        # ctrl_click_probe needs the room
  tools/run_probes.py --jobs 1             # one at a time, for a flaky probe

Exit codes: 0 every probe passed or skipped, 1 at least one failed, 2 no binary.
"""

import argparse
import fnmatch
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BINARY = os.path.join("build", "apps", "jot", "jot")

# A probe's own scheduling is its business (scenes wait for clangd to answer,
# for a finder to finish scanning), so the timeout is a hang detector rather
# than a budget: generous, and overridable for the probes that need more.
DEFAULT_TIMEOUT = 300.0


def probes():
    """Every test/*_probe.py, by bare name, in a stable order."""
    root = os.path.join(REPO, "test")
    names = [
        name[: -len("_probe.py")]
        for name in os.listdir(root)
        if name.endswith("_probe.py")
    ]
    return sorted(names)


def run_one(binary, name, timeout):
    """Run one probe. Returns (status, seconds, detail)."""
    script = os.path.join(REPO, "test", name + "_probe.py")
    started = time.monotonic()
    try:
        result = subprocess.run(
            [sys.executable, script, binary],
            cwd=REPO,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return "TIMEOUT", time.monotonic() - started, "ran past {}s".format(int(timeout))
    seconds = time.monotonic() - started
    status = {0: "PASS", 1: "FAIL", 2: "SKIP"}.get(result.returncode, "ERROR")
    return status, seconds, detail(result.stdout, result.stderr, status)


def detail(stdout, stderr, status):
    """The one line worth repeating from a probe's output.

    A failing or skipping probe says so on a line of its own ("foo probe: FAIL -
    ..."), and that line is the whole point of running it: the summary shows it
    rather than making the reader run the probe again to find out what broke.
    """
    lines = [line.strip() for line in (stdout + stderr).splitlines() if line.strip()]
    for line in reversed(lines):
        if ": FAIL" in line or ": SKIP" in line:
            return line
    if status == "PASS":
        return lines[-1] if lines else ""
    return lines[-1] if lines else "no output"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", nargs="?", default=DEFAULT_BINARY,
                        help="the jot binary the probes boot (default: %(default)s)")
    parser.add_argument("--jobs", type=int, default=4,
                        help="how many probes to run at once (default: %(default)s)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT,
                        help="seconds before a probe is called a hang (default: %(default)s)")
    parser.add_argument("--only", action="append", default=[],
                        help="glob to run, repeatable (default: every probe)")
    parser.add_argument("--skip", action="append", default=[], dest="skip",
                        help="glob to leave out, repeatable")
    args = parser.parse_args()

    binary = os.path.abspath(args.binary)
    if not os.path.exists(binary):
        print("probes: no binary at {} (build first, or pass one)".format(
            os.path.relpath(binary, REPO)))
        return 2

    names = probes()
    if args.only:
        names = [name for name in names
                 if any(fnmatch.fnmatch(name, pattern) for pattern in args.only)]
    if args.skip:
        names = [name for name in names
                 if not any(fnmatch.fnmatch(name, pattern) for pattern in args.skip)]
    if not names:
        print("probes: nothing matched")
        return 2

    results = []
    width = max(len(name) for name in names)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_one, binary, name, args.timeout) for name in names]
        for name, future in zip(names, futures):
            status, seconds, line = future.result()
            results.append((status, name, seconds, line))
            print("{:<7} {:<{}} {:6.1f}s  {}".format(
                status, name, width, seconds, line), flush=True)

    failed = [row for row in results if row[0] in ("FAIL", "TIMEOUT", "ERROR")]
    skipped = [row for row in results if row[0] == "SKIP"]
    passed = len(results) - len(failed) - len(skipped)
    if failed:
        print("probes: {} of {} failed: {}".format(
            len(failed), len(results), ", ".join(row[1] for row in failed)))
        return 1
    print("probes: {} passed, {} skipped, 0 failed ({}, {} at a time)".format(
        passed, len(skipped), args.binary, args.jobs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
