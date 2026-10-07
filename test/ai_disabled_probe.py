#!/usr/bin/env python3
"""Probe: the bundled AI assistant stays unregistered while it is disabled.

The AI runtime is loaded from src/editor/lua/api_bindings.cpp, and every surface it
owns - the command palette's rows, the :CodeCompanion* commands and the
Alt+Shift+A keymap family - is registered by features/ai/init.lua. Skipping the
load has to leave all of them absent, because a surface that still answers would
make "disabled" only half true.

Scenes, all against the real binary:
  * the palette lists no CodeCompanion* command for its query;
  * running :CodeCompanionChat never opens the chat buffer;
  * Alt+Shift+A C never opens it either (the family is gone).

Usage: test/ai_disabled_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 140, 30
PALETTE = b"\x1b[112;6u"  # Ctrl+Shift+P
ENTER = b"\r"
# The chat buffer's own heading, painted by features/ai/chat.lua. Nothing else
# in the editor draws it, so it is the one reliable "the assistant is up" mark.
CHAT_TITLE = "# Chat"


def run(binary: str, name: str, phases, keys: bytes = b"", dump: bool = False):
    tmp = "/tmp/jot_ai_disabled_probe_" + name
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    path = os.path.join(tmp, "a.cpp")
    with open(path, "w") as fh:
        fh.write("int a = 1;\n")
    screen = run_in_pty(binary, [path], keys, settle=2.5, after=0.6, cols=COLS,
                        rows=ROWS, cfg=tmp + "_cfg", cwd=tmp, phases=phases)
    if dump:
        print("=== %s ===" % name)
        print(screen.text())
        print("-" * 70)
    return screen.text()


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("ai disabled probe: SKIP - no binary at %s" % binary)
        return 2

    failures = []

    # Scene 1: the query matches no command. The palette lowercases its result
    # rows while the typed query keeps its capitals, so a lowercase needle
    # cannot be satisfied by the query line itself.
    text = run(binary, "palette", [(0.6, PALETTE), (0.8, b"CodeCompanion"), (0.8, b"")], dump=dump)
    if "codecompanionchat" in text or "codecompanionsend" in text:
        failures.append("palette: a CodeCompanion command is still listed")
    print("palette rows:     %s" % ("ok" if not failures else "FAILED"))

    # Scene 2: running the chat command opens nothing.
    before = len(failures)
    text = run(binary, "command", [(0.6, PALETTE), (0.8, b"CodeCompanionChat"), (0.6, ENTER),
                                   (0.8, b"")], dump=dump)
    if CHAT_TITLE in text:
        failures.append("command: :CodeCompanionChat opened the chat buffer")
    print("chat command:     %s" % ("ok" if len(failures) == before else "FAILED"))

    # Scene 3: the keymap family is gone too.
    before = len(failures)
    text = run(binary, "keymap", [], keys=b"\x1bAC", dump=dump)
    if CHAT_TITLE in text:
        failures.append("keymap: Alt+Shift+A C opened the chat buffer")
    print("keymap family:    %s" % ("ok" if len(failures) == before else "FAILED"))

    if failures:
        print("ai disabled probe: FAIL")
        for failure in failures:
            print("  - " + failure)
        return 1
    print("ai disabled probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
