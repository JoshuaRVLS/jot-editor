#!/usr/bin/env python3
"""Convert the per-language comment styles into the Lua module.

The comment markers a file type gets moved to
runtime/lua/features/comments/styles.lua, which `Ctrl+/` asks first through
jot.policy. The table in src/syntax/comment_style_data.h stays as the fallback
for a build or a test with no Lua runtime, and this tool is how the Lua copy was
produced from it: run it again after editing the native table and the two agree,
file type for file type, in name order.

    python3 tools/comment_style_import.py

Entries that name no marker at all are left out: the native table answers the
same empty style for them, so the file stays to the types that have something to
say. A field a type does not use is left out too, and the native value answers
for it.
"""
from __future__ import annotations

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "syntax", "comment_style_data.h")
OUT = os.path.join(ROOT, "runtime", "lua", "features", "comments", "styles.lua")

HEADER = """\
-- The comment markers each file type gets, in Lua.
--
-- `Ctrl+/` reads the entry for the file's extension here first; the table in
-- src/syntax/comment_style_data.h answers for a build or a test with no Lua
-- runtime, and for a field this file leaves out. A file type with no entry gets
-- the C-family form (//, and /* */ for a whole selection).
--
-- Converted from the native table; ask the two to agree with
-- python3 tools/comment_style_import.py. To comment a file type the editor has
-- never heard of, declare it yourself:
--
--   -- config.lua or a plugin, or a whole catalog of your own
--   jot.policy.set("text.comments", {
--     [".probe"] = { line = "#!", block_open = "", block_close = "" },
--   })
--
-- A field left out keeps the native answer, so naming just `line` is enough for
-- most types. Keys are extensions with their dot, in name order.
--
--   line         the per-line prefix ("//", "#", "--")
--   suffix       a per-line closer for types whose line comment wraps (html)
--   block_open   the opener when the type has a real block form ("/*")
--   block_close  its closer ("*/")

local M = {
"""

FIELDS = [("line", "line_prefix"), ("suffix", "line_suffix"),
          ("block_open", "block_open"), ("block_close", "block_close")]


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def lua_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_key(key: str) -> str:
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
        return key
    return "[" + lua_string(key) + "]"


def main() -> int:
    source = open(SOURCE, encoding="utf-8").read()
    at = source.index("kCommentStyles[] =")
    start = source.index("{", at)
    depth = 0
    body = None
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                body = source[start + 1 : index]
                break
    if body is None:
        raise SystemExit("unterminated kCommentStyles initializer")

    rows = re.findall(
        r'\{\s*"([^"]*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",'
        r'\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\}',
        flat(body),
    )
    styles = {row[0]: dict(zip([name for name, _ in FIELDS], row[1:])) for row in rows}

    lines = []
    empty = 0
    for ext in sorted(styles):
        fields = [(name, styles[ext][name]) for name, _ in FIELDS if styles[ext][name]]
        if not fields:
            empty += 1
            continue
        body = ", ".join("%s = %s" % (name, lua_string(value)) for name, value in fields)
        if len(body) <= 96:
            lines.append("  %s = { %s }," % (lua_key(ext), body))
        else:
            lines.append("  %s = {" % lua_key(ext))
            lines.extend("    %s = %s," % (name, lua_string(value)) for name, value in fields)
            lines.append("  },")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as handle:
        handle.write(HEADER)
        handle.write("\n".join(lines))
        handle.write("\n}\n\nreturn M\n")
    print("wrote %s: %d file types (%d with no marker at all, left out)"
          % (os.path.relpath(OUT, ROOT), len(styles) - empty, empty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
