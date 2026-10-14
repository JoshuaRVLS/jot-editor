#!/usr/bin/env python3
"""Probe: a language whose parser is installed actually becomes active.

The bundled highlight query is compiled as one unit, so a single node name the
grammar does not define fails the whole thing and the editor drops that language
to the regex fallback -- quietly. :tsstatus still lists the parser under
"Installed" ("parser loaded"), so the language looks healthy while nothing in
the buffer is highlighted. Three bundled queries shipped that way:

  * `c` named `(preproc_endif)`, which tree-sitter-c does not define (`#endif`
    belongs to its enclosing preproc_if), killing every .c and .h buffer;
  * `markdown` named inline-grammar and field names (`emphasis`,
    `heading_content`, `link_text`, `strong_emphasis`) against the block
    grammar, killing .md;
  * `djot` named `(class)` where the grammar has `class_name`, killing .dj.

This drives the real binary with one file per language, opens :tsstatus, and
reads the Active section: the language's row has to say it is active in the open
buffer. A language whose parser is not on the machine is skipped; if none is
runnable the probe skips (exit 2).

The same failure has a second tell that :tsstatus covers up, and the probe reads
that too: the boot log. A bundled query that does not compile is reported there
line by line ("Tree-sitter bundled query skipped (bash: query compilation failed
...)"), which is what the editor shows as a message on the way in. No such line
is allowed at all: the languages with an installed parser are exactly the ones
whose bundled queries are compiled on the way up, so a skip means a language
whose curated query is dead weight -- `bash` named the literal token `"return"`
and `python` the literals `"True"`, `"False"` and `"None"`, none of which the
current grammars define (python spells them as the named nodes true/false/none).

Usage: test/treesitter_query_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no parser).
"""
from __future__ import annotations

import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
ROOT = "/tmp/jot_treesitter_query_probe"
CFG = "/tmp/jot_treesitter_query_probe_cfg"

# (language id as :tsstatus lists it, file name, one-file source)
SCENES = [
    ("c", "main.c", "#include <stdio.h>\n\nint main(void) {\n  int apples = 1;\n  return apples;\n}\n"),
    ("markdown", "notes.md", "# Title\n\nSome *text* and a [link](https://example.com).\n"),
    ("djot", "notes.dj", "# Title\n\nSome _text_ and a {.highlight} span.\n"),
]


def parser_dirs() -> list[str]:
    home = os.environ.get("HOME", "")
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    cache = os.environ.get("XDG_CACHE_HOME") or os.path.join(home, ".cache")
    return [
        os.path.join(data, "jot", "treesitter", "parsers"),
        os.path.join(cache, "jot", "treesitter", "parsers"),
        "/usr/lib",
        "/usr/local/lib",
    ]


def parser_installed(language: str) -> bool:
    names = [f"libtree-sitter-{language}.so", f"libtree-sitter-{language}.dylib",
             f"tree-sitter-{language}.dll"]
    return any(os.path.exists(os.path.join(d, name)) for d in parser_dirs() for name in names)


def write_workspace(name: str, source: str) -> str:
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT, exist_ok=True)
    path = os.path.join(ROOT, name)
    with open(path, "w") as fh:
        fh.write(source)
    return path


def skipped_queries(log_path: str) -> list[str]:
    """The bundled-query skips the boot log reports, one line each."""
    if not os.path.exists(log_path):
        return []
    with open(log_path, errors="replace") as fh:
        return [line.strip() for line in fh if "bundled query skipped" in line]


def active_rows(view: str) -> list[str]:
    """The language column of every row that says `active in open buffer`."""
    rows = []
    for line in view.splitlines():
        if "active in open buffer" in line:
            match = re.search(r"│\s*([a-z0-9_+.-]+)\s+active in open buffer", line)
            if match:
                rows.append(match.group(1))
    return rows


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print(f"treesitter query probe: SKIP - no binary at {binary}")
        return 2

    runnable = [scene for scene in SCENES if parser_installed(scene[0])]
    if not runnable:
        print("treesitter query probe: SKIP - no parser installed for " +
              ", ".join(scene[0] for scene in SCENES))
        return 2

    failures = []
    skipped: set[str] = set()
    for language, name, source in runnable:
        shutil.rmtree(CFG, ignore_errors=True)
        path = write_workspace(name, source)
        screen = run_in_pty(binary, [path], PALETTE + b"tsstatus\r",
                            settle=4.0, after=3.0, cols=120, rows=40, cfg=CFG, cwd=ROOT)
        view = screen.text()
        if dump:
            print(view)
            print("-" * 70)
        # Read on every scene and reported once: the compile happens once, at
        # boot, for every language with an installed parser.
        skipped.update(skipped_queries(os.path.join(CFG, "logs", "jot_stderr.log")))
        rows = active_rows(view)
        print(f"{language}: active in open buffer: {rows}")
        if language not in rows:
            detail = ""
            for line in view.splitlines():
                if re.search(r"│\s*" + re.escape(language) + r"\s+", line):
                    detail = line.strip()
                    break
            failures.append(f"{language}: the {name} buffer is not using tree-sitter; "
                            f"its row reads {detail!r}")

    for reason in sorted(skipped):
        failures.append(f"a bundled query did not compile, so its language is on "
                        f"another source: {reason}")

    if failures:
        for failure in failures:
            print(f"treesitter query probe: FAIL - {failure}")
        return 1
    print("treesitter query probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
