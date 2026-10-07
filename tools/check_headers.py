#!/usr/bin/env python3
"""Check that every header under src/ compiles on its own.

A header that only compiles because something else was included first is a
trap: it makes include order load-bearing and it hides what the header really
needs. This compiles each one as the only include of an otherwise empty
translation unit, using the flags the build itself uses, taken from
build/compile_commands.json (so the check sees exactly the defines and include
directories the real compilation does).

Like the module check, the answer is a ratchet. Some headers under
src/editor/api/ are fragments of a class body rather than headers, and they
cannot compile alone by construction; they are recorded in the baseline and
disappear when the Editor stops being assembled from fragments.

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release    # so build/compile_commands.json exists
  tools/check_headers.py --list
  tools/check_headers.py --write-baseline
  tools/check_headers.py
"""

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(REPO, "docs", "refactor", "header-baseline.txt")
COMPILE_COMMANDS = os.path.join(REPO, "build", "compile_commands.json")

# Flags the standalone check needs from the real compile line. The include and
# define flags carry their value in the same token (-I<dir>, -D<name>), so they
# match by prefix; -isystem puts the directory in the next token.
WANTED_PREFIX = ("-I", "-D", "-std=", "-f", "-m")


def load_flags():
    if not os.path.isfile(COMPILE_COMMANDS):
        sys.exit(
            "{} is missing: configure a build first "
            "(cmake -S . -B build -DCMAKE_BUILD_TYPE=Release)".format(
                os.path.relpath(COMPILE_COMMANDS, REPO)
            )
        )
    with open(COMPILE_COMMANDS, "r", encoding="utf-8") as handle:
        entries = json.load(handle)

    # Union of every entry's flags. The engine, the GUI and the Lua bridge do
    # not carry the same -I and -D, and a header has to satisfy all of its
    # consumers, so the widest set is the honest one.
    flags = []
    seen = set()
    compiler = "c++"
    for entry in entries:
        if "command" not in entry:
            continue
        if entry["file"].endswith((".c", ".m")) and not entry["file"].endswith(".cpp"):
            continue
        tokens = shlex.split(entry["command"])
        compiler = tokens[0]
        index = 1
        while index < len(tokens):
            token = tokens[index]
            if token in ("-o", "-c", "-MF", "-MT", "-MQ", "-MD", "-MMD"):
                index += 2 if token in ("-o", "-MF", "-MT", "-MQ") else 1
                continue
            if token == "-isystem":
                # The directory is its own argv element, and the flag itself has
                # to be repeated for every directory: -isystem takes one path,
                # so a deduplicated flag would turn the next path into an input
                # file and the include directory would never be searched.
                if index + 1 < len(tokens):
                    pair = (token, tokens[index + 1])
                    if pair not in seen:
                        seen.add(pair)
                        flags.extend(pair)
                index += 2
                continue
            if token.startswith(WANTED_PREFIX) and token not in seen:
                seen.add(token)
                flags.append(token)
            index += 1
    return compiler, flags


def headers():
    found = []
    for root, dirs, files in os.walk(os.path.join(REPO, "src")):
        for name in sorted(files):
            if name.endswith((".h", ".hpp")):
                full = os.path.join(root, name)
                found.append(os.path.relpath(full, REPO).replace(os.sep, "/"))
    return sorted(found)


def check_one(rel, compiler, flags):
    """Compile the header alone. Returns None when it is self-contained."""
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "probe.cpp")
        # src/ is on the include path, so the spelling the compiler sees drops
        # the leading component: base/string_util.h, tools/telescope.h, ...
        spelling = rel[len("src/") :] if rel.startswith("src/") else rel
        with open(probe, "w", encoding="utf-8") as handle:
            handle.write('#include "{}"\n'.format(spelling))
        command = [compiler, "-fsyntax-only"] + flags + [probe]
        try:
            result = subprocess.run(
                command, capture_output=True, text=True, cwd=REPO, timeout=120
            )
        except subprocess.TimeoutExpired:
            return "timed out"
        if result.returncode == 0:
            return None
        output = (result.stderr or result.stdout).strip().splitlines()
        for line in output:
            if " error:" in line:
                # Strip the temp path, the absolute repo prefix and the column
                # noise: the message is the part a reader can act on, and the
                # baseline has to read the same on every machine.
                cleaned = re.sub(r"^.*?probe\.cpp[:0-9]*:?\s*", "", line)
                cleaned = cleaned.replace(REPO + os.sep, "").replace(REPO + "/", "")
                return cleaned.strip()
        return output[0] if output else "compiler failed"


def read_baseline():
    recorded = {}
    if not os.path.isfile(BASELINE):
        return recorded
    with open(BASELINE, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if line and not line.startswith("#"):
                path, _, _ = line.partition("\t")
                recorded[path] = True
    return recorded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list the headers that fail, with the message")
    parser.add_argument("--write-baseline", action="store_true", help="record the headers that fail today")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()

    compiler, flags = load_flags()
    all_headers = headers()
    self_contained, failures = [], {}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda rel: (rel, check_one(rel, compiler, flags)), all_headers))

    for rel, message in results:
        if message is None:
            self_contained.append(rel)
        else:
            failures[rel] = message

    if args.write_baseline:
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        with open(BASELINE, "w", encoding="utf-8") as handle:
            handle.write("# Headers that do not compile on their own, as of the phase that added this check.\n")
            handle.write("# Generated by tools/check_headers.py --write-baseline.\n")
            handle.write("# Pay these down by adding the missing include, never by deleting the line.\n")
            handle.write("# The src/editor/api/ entries are not headers at all: they are fragments of\n")
            handle.write("# the Editor class body, so they cannot compile alone by construction. They\n")
            handle.write("# disappear when the Editor stops being assembled from fragments.\n")
            for rel in sorted(failures):
                handle.write("{}\t{}\n".format(rel, failures[rel]))
        print(
            "headers: baseline written, {} of {} headers do not stand alone".format(
                len(failures), len(all_headers)
            )
        )
        return 0

    if args.list:
        for rel in sorted(failures):
            print("{}\n    {}".format(rel, failures[rel]))
        print(
            "headers: {} of {} stand alone, {} do not".format(
                len(self_contained), len(all_headers), len(failures)
            )
        )
        return 0

    recorded = read_baseline()
    new = sorted(set(failures) - set(recorded))
    fixed = sorted(set(recorded) - set(failures))

    for rel in fixed:
        print("headers: fixed, remove from the baseline: {}".format(rel))
    if new:
        for rel in new:
            print("headers: does not compile on its own: {}\n    {}".format(rel, failures[rel]))
        print(
            "headers: FAIL, {} header(s) stopped standing alone ({} were already on the baseline)".format(
                len(new), len(set(failures) & set(recorded))
            )
        )
        return 1

    print(
        "headers: ok, {} of {} stand alone, {} on the baseline ({})".format(
            len(self_contained),
            len(all_headers),
            len(failures),
            os.path.relpath(BASELINE, REPO),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
