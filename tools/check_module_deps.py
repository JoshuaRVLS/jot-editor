#!/usr/bin/env python3
"""Check that no module includes one that sits above it.

The directories under src/ are the module boundaries (docs/ARCHITECTURE.md).
A module may include itself and anything below it; an include that points
upward is what the refactor exists to remove. This walks every C++ source in
the tree, resolves each quoted include the way the compiler does (the including
file's own directory first, then the include directories src/CMakeLists.txt
hands to the targets), works out which module each end lives in, and reports
the edges that point the wrong way.

Because the tree is mid-refactor, the default mode is a ratchet: violations are
compared against docs/refactor/module-dep-baseline.txt and only *new* ones fail
the run. Existing edges have to be removed by moving the code, not here.

  tools/check_module_deps.py                 compare against the baseline
  tools/check_module_deps.py --list          list every violation, grouped
  tools/check_module_deps.py --write-baseline   re-record the baseline

The module for a file is its first directory under src/.
"""

import argparse
import os
import re
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(REPO, "docs", "refactor", "module-dep-baseline.txt")

# Lowest first. A file in module M may include M and anything before it.
MODULE_ORDER = [
    "base",
    "text",
    "syntax",
    "ui",
    "render",
    "input",
    "tools",
    "workspace",
    "lsp",
    "dap",
    "terminal",
    "git",
    "extras",
    # core/ holds the Editor class, so it takes the slot editor/ used to have.
    "core",
    # The domains editor/ used to keep inside itself are editor implementation
    # code: they include core/editor.h and sit just above it.
    "buffers",
    "panes",
    "commands",
    "events",
    "surfaces",
    "gui",
    "lua",
]
RANK = {name: i for i, name in enumerate(MODULE_ORDER)}

# The directories src/CMakeLists.txt puts on the include path, in the order the
# targets get them. Kept in step with jot_configure_object_target.
INCLUDE_DIRS = [
    "src",
    "src/base",
    "src/core",
    "src/input",
    "src/render",
    "src/text",
    "src/extras",
    "src/syntax",
    "src/dap",
    "src/lsp",
    "src/terminal",
    "src/ui",
    "src/workspace",
    "src/git",
    "src/lua",
    "src/buffers",
    "src/panes",
    "src/commands",
    "src/events",
    "src/surfaces",
    "src/gui",
]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".h", ".hpp")

# Quoted includes whose first path component belongs to a third-party library
# rather than this tree, so they are not expected to resolve under src/. These
# mirror the include roots CMake hands the compilers: third_party/glad,
# third_party/libvterm/include, /usr/include/SDL2, /usr/include/freetype2 and
# /usr/include/lua5.4. Nothing in the tree needs one, so the set is empty; it
# exists so an exception can be named instead of loosening the check.
#
# Naming a directory that exists under src/ here would stop the rule in this
# file from ever seeing that module, so _reject_shadowing_roots() refuses it.
SYSTEM_INCLUDE_ROOTS = frozenset()


def _reject_shadowing_roots():
    """Refuse an exception that would hide a real module from the check."""
    shadowing = sorted(
        root for root in SYSTEM_INCLUDE_ROOTS if os.path.isdir(os.path.join(REPO, "src", root))
    )
    if shadowing:
        print(
            "module deps: FAIL, SYSTEM_INCLUDE_ROOTS names module directories: {}".format(
                ", ".join(shadowing)
            )
        )
        return False
    return True


def module_of(path):
    """The module a repository-relative path belongs to, or None."""
    parts = path.split("/")
    if not parts or parts[0] != "src":
        return None
    if len(parts) == 1:
        return None
    top = parts[1]
    return top if top in RANK else None


def resolve(include, from_dir):
    """Find the file an include names, the way the compiler would."""
    candidates = [os.path.normpath(os.path.join(from_dir, include))]
    candidates += [os.path.normpath(os.path.join(d, include)) for d in INCLUDE_DIRS]
    for candidate in candidates:
        if os.path.isfile(os.path.join(REPO, candidate)):
            return candidate
    return None


def walk_sources():
    for root, dirs, files in os.walk(os.path.join(REPO, "src")):
        dirs[:] = [d for d in dirs if d != "sources.cmake"]
        for name in files:
            if name.endswith(SOURCE_SUFFIXES):
                full = os.path.join(root, name)
                yield os.path.relpath(full, REPO).replace(os.sep, "/")


def collect():
    """Every upward edge: (source path, target path, source module, target module).

    Also returns the includes that name no file at all. A bulk move that misses
    a rewrite is exactly how those appear, and a build finds them one compiler
    error at a time, so they are reported together here instead.
    """
    edges = []
    unresolved = []
    for rel in sorted(walk_sources()):
        from_dir = os.path.dirname(rel)
        src_module = module_of(rel)
        with open(os.path.join(REPO, rel), "r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                match = INCLUDE_RE.match(line)
                if not match:
                    continue
                include = match.group(1)
                if include.split("/", 1)[0] in SYSTEM_INCLUDE_ROOTS:
                    continue
                target = resolve(include, from_dir)
                if target is None:
                    unresolved.append((rel, include))
                    continue
                if src_module is None:
                    continue
                dst_module = module_of(target)
                if dst_module is None or dst_module == src_module:
                    continue
                if RANK[dst_module] > RANK[src_module]:
                    edges.append((rel, target, src_module, dst_module))
    return edges, unresolved


def read_baseline():
    recorded = set()
    if not os.path.isfile(BASELINE):
        return recorded
    with open(BASELINE, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                recorded.add(line)
    return recorded


def key(edge):
    return "{}\t{}".format(edge[0], edge[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="list every violation, grouped by pair")
    parser.add_argument("--write-baseline", action="store_true", help="record the current edges")
    args = parser.parse_args()

    if not _reject_shadowing_roots():
        return 1

    edges, unresolved = collect()
    for rel, include in unresolved:
        print("module deps: include names no file: {} includes \"{}\"".format(rel, include))
    if unresolved:
        print("module deps: FAIL, {} include(s) do not resolve".format(len(unresolved)))
        return 1

    if args.write_baseline:
        os.makedirs(os.path.dirname(BASELINE), exist_ok=True)
        with open(BASELINE, "w", encoding="utf-8") as handle:
            handle.write("# Upward includes that already exist, as src<TAB>dst.\n")
            handle.write("# Generated by tools/check_module_deps.py --write-baseline.\n")
            handle.write("# Each line is a debt to pay by moving the code, never by editing this file.\n")
            for source, target, _, _ in sorted(edges):
                handle.write("{}\t{}\n".format(source, target))
        print("module deps: baseline written, {} upward includes".format(len(edges)))
        return 0

    pairs = defaultdict(int)
    for _, _, src_module, dst_module in edges:
        pairs[(src_module, dst_module)] += 1

    if args.list:
        for (src_module, dst_module), count in sorted(pairs.items(), key=lambda kv: -kv[1]):
            print("{:>9} -> {:<9} {:4d}".format(src_module, dst_module, count))
        print("total: {} upward includes in {} module pairs".format(len(edges), len(pairs)))
        return 0

    recorded = read_baseline()
    current = {key(edge) for edge in edges}
    new = sorted(current - recorded)
    fixed = sorted(recorded - current)

    for item in fixed:
        print("module deps: fixed, remove from the baseline: {}".format(item.replace("\t", " -> ")))
    if new:
        for item in new:
            print("module deps: new upward include: {}".format(item.replace("\t", " -> ")))
        print(
            "module deps: FAIL, {} new upward include(s); {} already on the baseline".format(
                len(new), len(recorded & current)
            )
        )
        return 1

    print(
        "module deps: ok, {} upward includes, all on the baseline ({})".format(
            len(current), os.path.relpath(BASELINE, REPO)
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
