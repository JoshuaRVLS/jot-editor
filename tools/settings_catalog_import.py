#!/usr/bin/env python3
"""Convert the settings menu's native tables into the Lua catalog module.

The settings menu's labels, types, enum choices and int ranges moved to
runtime/lua/features/settings/catalog.lua, which the menu asks first through
jot.policy. The tables in src/surfaces/settings.cpp stay as the fallback for a
build or a test with no Lua runtime, and this tool is how the Lua copy was
produced from them: run it again after editing the native tables and the two
agree, entry for entry, in name order.

    python3 tools/settings_catalog_import.py

The output is written in place, so the diff says exactly what moved.
"""
from __future__ import annotations

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "src", "surfaces", "settings.cpp")
OUT = os.path.join(ROOT, "runtime", "lua", "features", "settings", "catalog.lua")

HEADER = """\
-- The settings menu's catalog, in Lua.
--
-- Every key the menu knows by name is here: the label its row reads as, and the
-- type the row is edited by. A key with no entry still gets a row, labelled with
-- the key itself, which is what a plugin's own config key gets.
--
-- Converted from the native tables in src/surfaces/settings.cpp, which stay as
-- the fallback for a build or a test with no Lua runtime; ask them to agree with
-- python3 tools/settings_catalog_import.py. Edit this file to relabel the menu,
-- or declare your own entry for a single key with
-- jot.policy.set(\"settings.catalog\", ...) from config.lua or a plugin.
--
-- Entries are in key order:
--
--   label    the row's text
--   type     "bool" toggles, "int" steps by `step` between `min` and `max` and
--            accepts a typed number, "string" opens the text field, "enum"
--            walks `options` with Left and Right
--   options  the values an "enum" row offers, in order
--
-- Example:
--
--   tab_size = { label = "Indent width", type = "int", step = 1, min = 1, max = 16 },

local M = {
"""


def block(source: str, marker: str) -> str:
    """The text between the braces of the first initializer after `marker`."""
    at = source.index(marker)
    at = source.index("{", at)
    depth = 0
    for index in range(at, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[at + 1 : index]
    raise SystemExit("unterminated initializer for " + marker)


def flat(text: str) -> str:
    return re.sub(r"\s+", " ", text)


def lua_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def lua_key(key: str) -> str:
    """A table field: bare when the key is a Lua name, bracketed otherwise.

    Setting keys carry dots ("toast.max_width"), and `toast.max_width = ...`
    inside a table constructor is not Lua: only a positional array, `name =`, or
    `[expr] =` may appear there.
    """
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
        return key
    return "[" + lua_string(key) + "]"


def main() -> int:
    source = open(SOURCE, encoding="utf-8").read()
    entries: dict[str, dict] = {}

    known = flat(block(source, "kKnownSettings[] ="))
    for key, label, type_name in re.findall(
        r'\{\s*"([^"]+)",\s*"((?:[^"\\]|\\.)*)",\s*SettingsEntry::Type::(Bool|Int|String)\s*\}',
        known,
    ):
        entries[key] = {"label": label, "type": type_name.lower()}

    arrays: dict[str, list[str]] = {}
    for match in re.finditer(r"const char \*const (\w+)\[\] = \{(.*?)\};", source, re.S):
        arrays[match.group(1)] = re.findall(r'"((?:[^"\\]|\\.)*)"', flat(match.group(2)))

    for key, array, count in re.findall(
        r'\{\s*"([^"]+)",\s*(\w+),\s*(\d+)\s*\}', flat(block(source, "kEnumChoices[] ="))
    ):
        entry = entries.setdefault(key, {})
        entry["type"] = "enum"
        entry["options"] = arrays[array][: int(count)]

    for key, step, low, high in re.findall(
        r'\{\s*"([^"]+)",\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\}', flat(block(source, "kIntRanges[] ="))
    ):
        entry = entries.setdefault(key, {})
        entry["step"] = int(step)
        entry["min"] = int(low)
        entry["max"] = int(high)

    lines: list[str] = []
    for key in sorted(entries):
        entry = entries[key]
        parts = []
        if "label" in entry:
            parts.append("label = " + lua_string(entry["label"]))
        parts.append("type = " + lua_string(entry["type"]))
        if "options" in entry:
            parts.append("options = {" + ", ".join(lua_string(o) for o in entry["options"]) + "}")
        if "step" in entry:
            parts.append("step = %d, min = %d, max = %d" % (entry["step"], entry["min"], entry["max"]))
        body = ", ".join(parts)
        if len(body) <= 96:
            lines.append("  %s = { %s }," % (lua_key(key), body))
        else:
            lines.append("  %s = {" % lua_key(key))
            lines.extend("    " + part + "," for part in parts)
            lines.append("  },")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as handle:
        handle.write(HEADER)
        handle.write("\n".join(lines))
        handle.write("\n}\n\nreturn M\n")
    print("wrote %s: %d entries" % (os.path.relpath(OUT, ROOT), len(entries)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
