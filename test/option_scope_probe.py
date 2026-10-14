#!/usr/bin/env python3
"""Probe: options with scopes, read off a real screen and a real file.

Most settings are one value for the session. Five of them are also a property of
one buffer or one window, and this probe drives the real binary to see that the
local value is not just a number in a table:

  * the store's answers, written on the screen by the session that asked them
    (init.lua): the value in force of a buffer option, the typed read, the
    session value the same key still reports, the refusal of a scope a key does
    not take, the schema from jot.config.info and the scoped writes;
  * a buffer's own tab_size reindents that buffer: Ctrl+A, Tab, Ctrl+S and the
    file on disk holds four spaces where the session's two would have gone. The
    leading spaces are read off disk rather than off the screen, which is what a
    screen reconstruction drops (the same reason cpp_indent_probe reads a save);
  * a pane's own winbar takes the breadcrumb row out of that pane: the file's
    first line is painted one row higher and the crumb separator is gone.

Usage: test/option_scope_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 110, 32
CTRL_A = b"\x01"
TAB = b"\t"
CTRL_SAVE = b"\x13"
CRUMB_SEPARATOR = "\u203a"  # the winbar's separator between crumbs

FAILURES: list[str] = []


def check(ok: bool, message: str) -> bool:
    if not ok:
        FAILURES.append(message)
    return ok


def dump(screen, label: str) -> None:
    print("=== %s ===" % label)
    print(screen.text())
    print("-" * 78)


def fresh(path: str) -> str:
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path)
    return path


def write_lua(cfg: str, body: str) -> None:
    os.makedirs(cfg, exist_ok=True)
    with open(os.path.join(cfg, "init.lua"), "w", encoding="utf-8") as fh:
        fh.write(body)


# --------------------------------------------------------------------------
# 1. the store's answers, written on the screen by the session that asked
# --------------------------------------------------------------------------
API_LUA = r"""
-- The questions a plugin asks about scopes, asked once the file is in front and
-- written to the screen every frame so the probe reads the last frame's answers.
local ready = false
local rows = {}

local function val(v)
  if v == nil then
    return "nil"
  end
  if type(v) == "number" then
    return string.format("%d", v)
  end
  return tostring(v)
end

local function ask()
  local cur = jot.buffer.current()
  local set_ok, set_err = jot.buffer.set_option("tab_size", 4)
  local local_v = jot.buffer.get_option("tab_size")
  -- The bare index a namespace call takes, which is the buffer in front here.
  local indexed = jot.buffer.get_option("tab_size", cur)
  local store = jot.config.get("tab_size")
  local eff = jot.config.get("tab_size", {scope = "effective"})
  local buf_only = jot.config.get("tab_size", {scope = "buffer"})
  local types = type(jot.buffer.get_option("tab_size")) .. ","
                .. type(jot.buffer.get_option("auto_indent"))

  local win_set = jot.window.set_option("relative_line_numbers", true, 1)
  local win_read_back = jot.window.get_option("relative_line_numbers", 1)
  local win_v = jot.window.get_option("relative_line_numbers")
  local win_store = jot.config.get("relative_line_numbers")
  local win_type = type(jot.window.get_option("relative_line_numbers"))

  local refuse_ok, refuse_err = jot.buffer.set_option("winbar", "off")
  local nil_v, nil_ok, nil_err = jot.window.get_option("tab_size")

  local info = jot.config.info("tab_size")
  local list = jot.config.list()
  local infos = 0
  for _ in pairs(jot.config.info()) do
    infos = infos + 1
  end
  local sorted = true
  for i = 2, #list do
    if list[i] < list[i - 1] then
      sorted = false
    end
  end
  local has_tab = false
  for _, key in ipairs(list) do
    if key == "tab_size" then
      has_tab = true
    end
  end

  local unset_ok, unset_err = jot.buffer.unset_option("tab_size")
  local after = jot.buffer.get_option("tab_size")
  -- The plain forms take no opts table and are what they always were: a set
  -- with no return, and a store read that ignores a local value.
  local plain = jot.config.set("word_wrap", true)
  local wrap = jot.config.get("word_wrap")
  local global_ok, global_err = jot.config.set("tab_size", 8, {scope = "global"})
  local store8 = jot.config.get("tab_size")
  local eff8 = jot.config.get("tab_size", {scope = "effective"})

  rows = {
    "SET=" .. val(set_ok) .. "+" .. val(set_err) .. " IDX=" .. val(indexed)
      .. " LOCAL=" .. val(local_v) .. " STORE=" .. val(store) .. " EFF=" .. val(eff)
      .. " BUFONLY=" .. val(buf_only),
    "TYPES=" .. types .. " WIN=" .. val(win_set) .. " WINIDX=" .. val(win_read_back)
      .. " WINV=" .. val(win_v) .. " WINTYPE=" .. win_type .. " WINSTORE=" .. val(win_store),
    "REFUSE=" .. val(refuse_ok) .. " " .. val(refuse_err),
    "NIL=" .. val(nil_v) .. "," .. val(nil_ok) .. " " .. val(nil_err),
    "INFO=" .. info.name .. "," .. info.label .. "," .. info.type .. ","
      .. info.scope .. "," .. val(info.live) .. "," .. val(info.default) .. ","
      .. info.value .. "," .. val(info.known),
    "RANGE=" .. val(info.min) .. "," .. val(info.max) .. "," .. val(info.step),
    "LIST=" .. #list .. " SORTED=" .. val(sorted) .. " HAS=" .. val(has_tab)
      .. " INFOS=" .. infos,
    "UNSET=" .. val(unset_ok) .. "+" .. val(unset_err) .. " AFTER=" .. val(after),
    "PLAIN=" .. val(plain) .. " WRAP=" .. val(wrap) .. " GLOBAL=" .. val(global_ok)
      .. "+" .. val(global_err) .. " STORE8=" .. val(store8) .. " EFF8=" .. val(eff8),
  }
end

jot.screen.on_paint(function(w, h)
  if w < 80 or h < 20 then
    return
  end
  if not ready then
    local meta = jot.buffer.meta()
    if meta and meta.name == "scope.txt" then
      ready = true
      ask()
    end
  end
  if not ready then
    return
  end
  for i, line in ipairs(rows) do
    jot.screen.write(2, 2 + i, line)
  end
end)
"""

# The answers, row by row as API_LUA writes them at y = 3, 4, ... The session
# config is a fresh one, so tab_size is the built-in 2, and the local value is
# the 4 the session just set on the buffer in front. A refusal is the third
# return, and `SET=true+nil` is the second one: the binding returns two values
# and the session reads both, so a binding that pushed only one would show a
# number or a key name where the nil is.
API_EXPECTED = [
    (3, "SET=true+nil IDX=4 LOCAL=4 STORE=2 EFF=4 BUFONLY=4"),
    (4, "TYPES=number,boolean WIN=true WINIDX=true WINV=true WINTYPE=boolean WINSTORE=false"),
    (5, 'REFUSE=false "winbar" has no buffer value'),
    (6, 'NIL=nil,false "tab_size" has no window value'),
    (7, "INFO=tab_size,Tab size,int,buffer,true,2,2,true"),
    (8, "RANGE=1,16,1"),
]

# --------------------------------------------------------------------------
# 2. a buffer's own width reindents that buffer
# --------------------------------------------------------------------------
INDENT_LUA = r"""
-- Once the file is in front, this buffer gets an indent width of its own: the
-- session's stays 2, so what the file holds after Ctrl+A Tab Ctrl+S is the
-- local value's doing.
local done = false
jot.screen.on_paint(function()
  if done then
    return
  end
  local meta = jot.buffer.meta()
  if not meta or meta.name ~= "a.cpp" then
    return
  end
  done = true
  jot.buffer.set_option("tab_size", 4)
end)
"""

INDENT_BODY = "int a = 1;\nint b = 2;\n"
INDENT_KEYS = CTRL_A + TAB + CTRL_SAVE

# --------------------------------------------------------------------------
# 3. a pane's own winbar takes its row away
# --------------------------------------------------------------------------
WINBAR_LUA = r"""
local done = false
jot.screen.on_paint(function()
  if done then
    return
  end
  local meta = jot.buffer.meta()
  if not meta or meta.name ~= "main.py" then
    return
  end
  done = true
  jot.window.set_option("winbar", "off")
end)
"""


def run_api(binary: str, dump_screen: bool) -> None:
    cfg = fresh("/tmp/jot_option_scope_probe_api_cfg")
    work = fresh("/tmp/jot_option_scope_probe_api_work")
    write_lua(cfg, API_LUA)
    with open(os.path.join(work, "scope.txt"), "w") as fh:
        fh.write("scope probe\n")
    screen = run_in_pty(binary, [os.path.join(work, "scope.txt")], b"", settle=3.0,
                        after=2.0, cfg=cfg, cwd=work, cols=COLS, rows=ROWS)
    if dump_screen:
        dump(screen, "scope api")
    rows = ["".join(cells).rstrip() for cells in screen.cells]

    def reading(index: int, first_key: str) -> str:
        """A reading row from its first key on: the pane's own gutter (a line
        number or the tilde past the end of the file) stays to its left, and
        what the session wrote is what the row says from there."""
        text = rows[index]
        at = text.find(first_key + "=")
        if at < 0:
            check(False, "row %d has no %s: %r" % (index, first_key, text))
            return ""
        return text[at:].rstrip()

    for index, expected in API_EXPECTED:
        got = reading(index, expected.split("=", 1)[0])
        check(got == expected, "row %d reads %r, wanted %r" % (index, got, expected))

    # The key list and the schema have to describe the same store, and the list
    # is the one jot.config.keys() hands out, sorted.
    list_row = reading(9, "LIST")
    fields = dict(part.split("=", 1) for part in list_row.split(" ") if "=" in part)
    if "LIST" in fields and "SORTED" in fields and "HAS" in fields and "INFOS" in fields:
        check(fields["SORTED"] == "true", "jot.config.list() is not sorted: %r" % list_row)
        check(fields["HAS"] == "true", "jot.config.list() has no tab_size: %r" % list_row)
        check(int(fields["LIST"]) > 100, "jot.config.list() is short: %r" % list_row)
        check(fields["LIST"] == fields["INFOS"],
              "the list names %s keys, the schema describes %s"
              % (fields["LIST"], fields["INFOS"]))
    check(reading(10, "UNSET") == "UNSET=true+nil AFTER=2",
          "row 10 reads %r, wanted the local value to fall back to the session's"
          % reading(10, "UNSET"))
    check(reading(11, "PLAIN")
          == "PLAIN=nil WRAP=true GLOBAL=true+nil STORE8=8 EFF8=8",
          "row 11 reads %r, wanted the plain write and the scoped one to land"
          % reading(11, "PLAIN"))


def run_indent(binary: str, local: bool, dump_screen: bool) -> str:
    label = "local" if local else "session"
    cfg = fresh("/tmp/jot_option_scope_probe_indent_%s_cfg" % label)
    work = fresh("/tmp/jot_option_scope_probe_indent_%s_work" % label)
    write_lua(cfg, INDENT_LUA if local else "")
    path = os.path.join(work, "a.cpp")
    with open(path, "w") as fh:
        fh.write(INDENT_BODY)
    screen = run_in_pty(binary, [path], INDENT_KEYS, settle=3.0, after=1.0, cfg=cfg,
                        cwd=work, cols=COLS, rows=20)
    if dump_screen:
        dump(screen, "indent %s" % label)
    with open(path) as fh:
        return fh.read()


def first_marker_row(screen, marker: str) -> int:
    for index, cells in enumerate(screen.cells):
        if marker in "".join(cells):
            return index
    return -1


def run_winbar(binary: str, local: bool, dump_screen: bool):
    label = "local" if local else "session"
    cfg = fresh("/tmp/jot_option_scope_probe_winbar_%s_cfg" % label)
    work = fresh("/tmp/jot_option_scope_probe_winbar_%s_work" % label)
    os.makedirs(os.path.join(work, "demo"))
    write_lua(cfg, WINBAR_LUA if local else "")
    with open(os.path.join(work, "demo", "main.py"), "w") as fh:
        fh.write("SCOPE_MARKER = 1\nprint(SCOPE_MARKER)\n")
    screen = run_in_pty(binary, ["demo/main.py"], b"", settle=3.0, after=2.0, cfg=cfg,
                        cwd=work, cols=COLS, rows=20)
    if dump_screen:
        dump(screen, "winbar %s" % label)
    return screen


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump_screen = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("option scope probe: SKIP - no binary at %s" % binary)
        return 2

    run_api(binary, dump_screen)

    # The session width would indent by two; the buffer's own by four.
    session_text = run_indent(binary, False, dump_screen)
    local_text = run_indent(binary, True, dump_screen)
    print("%-34s %s" % ("session width indents by two",
                        "ok" if session_text == "  int a = 1;\n  int b = 2;\n" else "FAILED"))
    print("%-34s %s" % ("buffer width indents by four",
                        "ok" if local_text == "    int a = 1;\n    int b = 2;\n" else "FAILED"))
    check(session_text == "  int a = 1;\n  int b = 2;\n",
          "the session width indented to %r" % session_text)
    check(local_text == "    int a = 1;\n    int b = 2;\n",
          "the buffer's own width indented to %r" % local_text)

    # The breadcrumb row is a row the pane spends; the pane's own winbar decides.
    on_screen = run_winbar(binary, False, dump_screen)
    off_screen = run_winbar(binary, True, dump_screen)
    on_row = first_marker_row(on_screen, "SCOPE_MARKER")
    off_row = first_marker_row(off_screen, "SCOPE_MARKER")
    on_crumb = any(CRUMB_SEPARATOR in "".join(cells) for cells in on_screen.cells)
    off_crumb = any(CRUMB_SEPARATOR in "".join(cells) for cells in off_screen.cells)
    shifted = on_row >= 0 and off_row >= 0 and off_row == on_row - 1
    print("%-34s %s" % ("a pane's winbar keeps its row",
                        "ok" if on_crumb else "FAILED"))
    print("%-34s %s" % ("a pane's own winbar drops it",
                        "ok" if shifted and not off_crumb else "FAILED"))
    check(on_row >= 0 and off_row >= 0, "the marker line is not on screen")
    check(on_crumb, "the session's winbar painted no crumb row")
    check(shifted,
          "the file's first line sits on row %d with the row, %d without" % (on_row, off_row))
    check(not off_crumb, "the pane's own winbar was off and a crumb row was painted anyway")

    if FAILURES:
        print("option scope probe: FAIL")
        for line in FAILURES:
            print("  - %s" % line)
        return 1
    print("option scope probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
