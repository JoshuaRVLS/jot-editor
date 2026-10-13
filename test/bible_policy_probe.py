#!/usr/bin/env python3
"""jot.policy: the Bible reader's reference grammar can live in Lua.

`:bible` reads its argument with the native parser in src/extras/bible.cpp and
prints the passage's name with the native labeller. Both are policy, so both
have a provider on the other side of jot.policy now
(runtime/lua/features/bible/reference.lua) with the C++ copy kept as the
fallback. Four sessions against a local fixture API settle it:

  1. no provider: `:bible gospeljn 3:16` is refused -- the native grammar does
     not know that spelling, and nothing opens;
  2. a provider that reads that spelling and prints its own label: the same
     command opens the passage, in a tab named by the provider;   3. a provider that answers with a book the canon does not have: its answer is
     refused and the native parse answers instead, under the native label;
  4. a provider that raises on both calls: the reader still opens the passage;
  5. a plugin reload, which drops every provider on the way in: the bundled one
     claims the name again, so the policy is not stuck in C++ for the rest of
     the session.

Case 1 is the control. Without it, case 2 could not tell a provider that ran
from a native parser that happened to accept the spelling.

Nothing here talks to bible.helloao.org: the fixture server answers the same
paths, so the run is offline and deterministic, and its verse text carries a
tag no editor would invent.

Usage: test/bible_policy_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import http.server
import json
import os
import shutil
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 140, 36
ROOT = "/tmp/jot_bible_policy_probe"
CFG = "/tmp/jot_bible_policy_probe_cfg"

# Ctrl+Shift+P opens the command palette; Enter runs the typed `:command`.
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"
ESC = b"\x1b"

# Unlike anything the editor would invent on its own.
TAG = "jot-bible-policy-3d9"

# The spelling no native spelling list has, and the label no native labeller
# prints: either one on screen can only have come from a provider. Each is the
# line the palette runs, so the command name is part of it.
SPELLING = "bible gospeljn 3:16"
NATIVE = "bible john 3:16"
MARK = "LUA"

NAMES_FILE = "/tmp/jot_bible_policy_names"

NO_PROVIDER = """\
-- Nothing is registered here: this session is what the editor does when no
-- provider claims bible.reference, which is the control for the scenes after it.
"""

# Registers no provider, only a command that reports what owns the policy when
# it runs, so the same question can be asked again after a plugin reload.
ASK_NAMES = """\
jot.command("policyprobe", function()
  jot.file.write("%NAMES%", table.concat(jot.policy.names(), ","))
end)
""".replace("%NAMES%", NAMES_FILE)

# Registers no provider, only a frame hook that reports what owns the policy by
# the time the editor has painted: the shipped module registers as it loads.
WATCH_NAMES = """\
local reported = false
jot.screen.on_paint(function()
  if reported then
    return
  end
  reported = true
  jot.file.write("%NAMES%", table.concat(jot.policy.names(), ","))
end)
""".replace("%NAMES%", NAMES_FILE)

PROVIDER = """\
-- A reference grammar (and a label) only this provider knows.
local names = { JHN = "John", PSA = "Psalms" }

jot.policy.set("bible.reference", {
  parse = function(text)
    if string.lower(text or ""):match("^gospeljn%s+3:16$") then
      return { id = "JHN", chapter = 3, has_verse = true, verse_first = 16, verse_last = 16 }
    end
    return nil
  end,
  label = function(ref)
    local out = "%MARK% " .. (names[ref.id] or ref.id) .. " " .. tostring(ref.chapter)
    if ref.has_verse then
      out = out .. ":" .. tostring(ref.verse_first)
    end
    return out
  end,
})
""".replace("%MARK%", MARK)

# Answers every `john` reference, but with a book the native canon cannot
# address. Its label prints the book id it was handed, which is how the run
# reads back which parse the answer came from.
PROVIDER_WRONG_BOOK = """\
jot.policy.set("bible.reference", {
  parse = function(text)
    if string.lower(text or ""):match("^john") then
      return { id = "ZZZ", chapter = 3, has_verse = true, verse_first = 16, verse_last = 16 }
    end
    return nil
  end,
  label = function(ref)
    return "%MARK% " .. tostring(ref.id)
  end,
})
""".replace("%MARK%", MARK)

PROVIDER_BROKEN = """\
jot.policy.set("bible.reference", {
  parse = function(text)
    error("the provider's parse is broken")
  end,
  label = function(ref)
    error("the provider's label is broken")
  end,
})
"""


def chapter_fixture(book_id: str, chapter: int) -> dict:
    name = "John" if book_id == "JHN" else book_id
    content = [{"type": "heading", "text": "A Probe Heading"}]
    for verse in range(1, 18):
        content.append({"type": "verse", "number": verse,
                        "text": "%s %d:%d (%s)." % (name, chapter, verse, TAG)})
    return {
        "translation": {"id": "BSB", "englishName": "Berean Standard Bible"},
        "book": {"id": book_id, "name": name, "commonName": name},
        "chapter": {"number": chapter, "content": content},
    }


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):  # keep the probe's own output readable
        pass

    def do_GET(self):
        parts = [part for part in self.path.split("/") if part]
        # /api/BSB/JHN/3.simple.json
        if len(parts) == 4 and parts[0] == "api" and parts[3].endswith(".simple.json"):
            body = json.dumps(chapter_fixture(parts[2], int(parts[3].split(".")[0]))).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(404)
        self.end_headers()


def set_provider(source: str) -> None:
    with open(os.path.join(CFG, "init.lua"), "w") as fh:
        fh.write(source)


def command_phase(command: str, expect: str):
    """Type `:command` into the palette and stop once `expect` is on screen.

    The fetch is a worker-thread request, so a fixed tail races it; waiting on
    the answer keeps the verdict about the feature rather than the machine.
    """
    return [(0.8, PALETTE), (0.6, command.encode()), (0.6, ENTER),
            (8.0, lambda screen: expect in screen.text()), (0.4, b"")]


def run(binary: str, phases: list):
    """Opens the workspace (off the home screen), then runs `phases`."""
    return run_in_pty(binary, [ROOT], b"", settle=9.0, after=1.0, cols=COLS, rows=ROWS,
                      cfg=CFG, cwd=ROOT, phases=[(0.8, PALETTE), (0.6, b"e notes.txt"),
                                                 (0.5, ENTER), (0.5, ESC)] + phases)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("bible policy probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(CFG, ignore_errors=True)
    shutil.rmtree(ROOT, ignore_errors=True)
    os.makedirs(ROOT)
    os.makedirs(os.path.join(CFG, "configs"), exist_ok=True)
    with open(os.path.join(ROOT, "notes.txt"), "w") as fh:
        fh.write("See John 3:16 for the verse.\n")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        fh.write("lsp_enabled=false\n"
                 "bible_api_base=http://127.0.0.1:%d/api\n" % port)

    failures: list[str] = []

    def scene(title: str, provider: str, phases: list):
        print("scene: %s" % title)
        set_provider(provider)
        screen = run(binary, phases)
        if dump:
            print(screen.text())
            print("-" * 70)
        return screen

    # A toast is what the refusals below are: the run has to catch it while it
    # is up rather than look for it on the final screen.
    def refusal_phase():
        seen = {"value": False}

        def check(screen):
            if "cannot read" in screen.text():
                seen["value"] = True
                return True
            return False

        return (0.1, check), seen

    # 0. The shipped module claims the name: the port is live in an ordinary
    # session, not only when a config registers something itself.
    if os.path.exists(NAMES_FILE):
        os.remove(NAMES_FILE)
    scene("the shipped module claims bible.reference", WATCH_NAMES,
          [(2.0, b"")])
    names = open(NAMES_FILE).read() if os.path.exists(NAMES_FILE) else ""
    if "bible.reference" not in names:
        failures.append("the shipped reference module did not register "
                        "(jot.policy.names() is %r)" % names)

    # 1. No provider: the native grammar refuses the spelling outright.
    poll, seen = refusal_phase()
    screen = scene("no provider: the native grammar refuses the spelling",
                   NO_PROVIDER,
                   [(0.8, PALETTE), (0.6, SPELLING.encode()), (0.6, ENTER), poll, (0.4, b"")])
    if not seen["value"]:
        failures.append("with no provider, `:%s` was not refused" % SPELLING)
    if "[Bible]" in screen.text():
        failures.append("with no provider, the refused spelling still opened a passage")

    # 2. A provider reads the spelling and prints its own label: the passage
    # opens in a tab named by it, holding the fixture's own verse text.
    view = scene("provider: the same spelling opens, under the provider's label",
                 PROVIDER, command_phase(SPELLING, TAG)).text()
    if "%s John 3:16" % MARK not in view:
        failures.append("the tab does not carry the provider's label")
    if TAG not in view:
        failures.append("the served verse text never reached the passage")
    if "Berean Standard Bible (BSB)" not in view:
        failures.append("the passage header is not on screen")

    # 3. A provider naming a book the canon does not have is refused, and the
    # native parse answers. Its label still prints (a label is its own call),
    # and the id it is handed says which parse produced the reference: the
    # native JHN, never the ZZZ the provider asked for.
    view = scene("provider names an unknown book: the native parse answers",
                 PROVIDER_WRONG_BOOK, command_phase(NATIVE, TAG)).text()
    if "%s JHN" % MARK not in view:
        failures.append("the label was not handed the natively parsed book")
    if "ZZZ" in view:
        failures.append("a book the canon does not have reached the passage")
    if TAG not in view:
        failures.append("the passage the native parse asked for is not on screen")

    # 4. A provider that raises on both calls costs a fallback, not the reader.
    view = scene("provider raises: the reader still opens the passage",
                 PROVIDER_BROKEN, command_phase(NATIVE, TAG)).text()
    if "[Bible] John 3:16" not in view:
        failures.append("a provider that raises took the reader down with it")
    if TAG not in view:
        failures.append("a provider that raises left the passage empty")

    # 5. A reload drops every provider as it re-runs the config. The bundled
    # module registers after the config does, so the name is claimed again and
    # the policy is live -- without that, a reload would silently leave every
    # reference going through C++ until the editor was restarted. The command
    # below is asked the same question before and after, and the file it writes
    # is read back after the second answer.
    if os.path.exists(NAMES_FILE):
        os.remove(NAMES_FILE)
    ask = [(0.8, PALETTE), (0.6, b"policyprobe"), (0.6, ENTER), (1.5, b"")]
    scene("a plugin reload claims the name again", ASK_NAMES,
          ask + [(0.8, PALETTE), (0.6, b"reloadplugins"), (0.6, ENTER), (3.0, b"")] + ask)
    if not os.path.exists(NAMES_FILE):
        failures.append("the probe command never reported after the reload")
    else:
        names = open(NAMES_FILE).read()
        if "bible.reference" not in names:
            failures.append(":reloadplugins left the provider unregistered "
                            "(jot.policy.names() is %r)" % names)

    server.shutdown()

    if failures:
        for failure in failures:
            print("bible policy probe: FAIL - %s" % failure)
        return 1
    print("bible policy probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
