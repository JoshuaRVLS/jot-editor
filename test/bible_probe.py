#!/usr/bin/env python3
"""Probe: the Bible reader end to end, against a local fixture API.

The unit tests (test/test_bible.cpp, test/test_bible_client.cpp) pin the
reference grammar, the renderers and the command path with a pre-seeded cache.
What only a real session proves is the loop: `:bible` reaches the HelloAO API
shape over a real socket, the answer lands in the `[Bible]` tab, `next` walks a
chapter on, `random` draws a verse, and a passage seen once is served from
JOT_CACHE_HOME without a second request.

Nothing here talks to bible.helloao.org: the fixture server answers the same
paths, so the run is offline and deterministic. The verse text carries a tag
that no editor would invent, so seeing it on screen can only mean the body came
back from this probe's server.

Usage: test/bible_probe.py [path-to-jot] [--dump]
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
ROOT = "/tmp/jot_bible_probe"
CFG = "/tmp/jot_bible_probe_cfg"

# Ctrl+Shift+P opens the command palette; Enter runs the typed `:command`.
PALETTE = b"\x1b[112;6u"
ENTER = b"\r"
ESC = b"\x1b"

# Unlike anything the editor would invent on its own.
TAG = "jot-bible-fixture-7c1"

BOOK_NAMES = {
    "GEN": "Genesis",
    "PSA": "Psalms",
    "JHN": "John",
    "ROM": "Romans",
}

REQUESTS: list[str] = []


def chapter_fixture(book_id: str, chapter: int) -> dict:
    """A chapter with verses 1..17, so any verse the day/random pick lands on
    exists. Every verse carries the tag, so the run can prove the served text
    reached the buffer whatever verse was asked for."""
    name = BOOK_NAMES.get(book_id, book_id)
    content = [{"type": "heading", "text": "A Probe Heading"}]
    for verse in range(1, 18):
        content.append({"type": "verse", "number": verse,
                        "text": "%s %d:%d (%s)." % (name, chapter, verse, TAG)})
        if verse == 8:
            content.append({"type": "line_break"})
    return {
        "translation": {"id": "BSB", "englishName": "Berean Standard Bible"},
        "book": {"id": book_id, "name": name, "commonName": name},
        "chapter": {"number": chapter, "content": content},
    }


def commentary_fixture(book_id: str, chapter: int) -> dict:
    name = BOOK_NAMES.get(book_id, book_id)
    return {
        "commentary": {"id": "adam-clarke", "englishName": "Adam Clarke Bible Commentary"},
        "book": {"id": book_id, "commonName": name},
        "chapter": {
            "number": chapter,
            "content": [
                {"type": "verse", "number": 16,
                 "content": ["The probe note on verse sixteen (%s)." % TAG]},
            ],
        },
    }


def xref_fixture(book_id: str, chapter: int) -> dict:
    return {
        "dataset": {"id": "open-cross-ref"},
        "book": {"id": book_id},
        "chapter": {
            "number": chapter,
            "content": [
                {"verse": 16, "references": [
                    {"book": "ROM", "chapter": 5, "verse": 8, "score": 9},
                ]},
            ],
        },
    }


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):  # keep the probe's own output readable
        pass

    def _reply(self, payload: dict) -> None:
        body = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path
        REQUESTS.append(path)
        parts = [part for part in path.split("/") if part]
        # /api/BSB/JHN/3.simple.json
        if len(parts) == 4 and parts[0] == "api" and parts[3].endswith(".simple.json"):
            book_id = parts[2]
            chapter = int(parts[3].split(".")[0])
            self._reply(chapter_fixture(book_id, chapter))
            return
        # /api/c/adam-clarke/JHN/3.json
        if len(parts) == 5 and parts[0] == "api" and parts[1] == "c":
            book_id = parts[3]
            chapter = int(parts[4].split(".")[0])
            self._reply(commentary_fixture(book_id, chapter))
            return
        # /api/d/open-cross-ref/JHN/3.json
        if len(parts) == 5 and parts[0] == "api" and parts[1] == "d":
            book_id = parts[3]
            chapter = int(parts[4].split(".")[0])
            self._reply(xref_fixture(book_id, chapter))
            return
        if path == "/api/available_translations.json":
            self._reply({"translations": [
                {"id": "BSB", "englishName": "Berean Standard Bible", "language": "eng"},
                {"id": "ENGWEBP", "englishName": "World English Bible", "language": "eng"},
            ]})
            return
        self.send_response(404)
        self.end_headers()


def count_requests(suffix: str) -> int:
    return len([path for path in REQUESTS if path.endswith(suffix)])


def write_workspace(root: str) -> None:
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    with open(os.path.join(root, "notes.txt"), "w") as fh:
        fh.write("See John 3:16 for the verse.\n")


def command_phase(command: str, expect: str, wait: float = 8.0):
    """Type `:command` into the palette and wait for `expect` on screen.

    The fetch is a worker-thread request, so a fixed tail races it; waiting on
    the answer keeps the scene's verdict about the feature rather than about
    the machine's load. A command the palette cannot match never satisfies the
    predicate, and the scene still reads the screen after `wait` -- so the
    check below reports what is actually wrong.
    """
    return [(0.8, PALETTE), (0.6, command.encode()), (0.6, ENTER),
            (wait, lambda screen: expect in screen.text()), (0.4, b"")]


def run(binary: str, roots: list[str], cfg: str, phases: list):
    """Opens the workspace (off the home screen), then runs `phases`."""
    return run_in_pty(binary, roots, b"", settle=9.0, after=1.0, cols=COLS, rows=ROWS,
                      cfg=cfg, cwd=roots[0],
                      env={"JOT_CONFIG_HOME": cfg, "JOT_CACHE_HOME": cfg},
                      phases=[(0.8, PALETTE), (0.6, b"e notes.txt"), (0.5, ENTER),
                              (0.5, ESC)] + phases)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("bible probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(os.path.join(CFG, "configs"), exist_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        fh.write("lsp_enabled=false\nbible_context_verses=1\n"
                 "bible_api_base=http://127.0.0.1:%d/api\n" % port)
    write_workspace(ROOT)

    failures: list[str] = []

    def scene(title: str, phases: list):
        print("scene: %s" % title)
        screen = run(binary, [ROOT], CFG, phases)
        if dump:
            print(screen.text())
            print("-" * 70)
        return screen

    # 1. A reference opens the passage, marked, in the `[Bible]` tab.
    screen = scene(":bible john 3:16 (server answer -> [Bible] tab)",
                   command_phase("bible john 3:16", TAG))
    view = screen.text()
    if "John 3 - Berean Standard Bible (BSB)" not in view:
        failures.append("the passage header is not on screen")
    if ">16" not in view:
        failures.append("the requested verse is not marked")
    if TAG not in view:
        failures.append("the served verse text never reached the buffer")
    if "[Bible] John 3" not in view:
        failures.append("the tab does not name the passage it is showing")

    # The colours, read off the cells themselves: the header and the requested
    # verse's marker are painted in theme colours, the verse text is not.
    rows = view.split("\n")
    header_row = next((y for y, line in enumerate(rows) if "John 3 - Berean" in line), None)
    verse_row = next((y for y, line in enumerate(rows) if ">16" in line), None)
    if header_row is None or verse_row is None:
        failures.append("the passage rows are not on screen for the colour check")
    else:
        hx = rows[header_row].index("John 3 - Berean")
        vx = rows[verse_row].index(">16")
        header_fg = screen.fg[header_row][hx]
        marker_fg = screen.fg[verse_row][vx]
        plain_fg = screen.fg[verse_row][vx + 8]
        if header_fg == plain_fg:
            failures.append("the passage header is not coloured")
        if marker_fg == plain_fg:
            failures.append("the requested verse marker is not coloured")

    if count_requests("/BSB/JHN/3.simple.json") != 1:
        failures.append("the chapter was requested %d times, expected 1"
                        % count_requests("/BSB/JHN/3.simple.json"))

    # 2. The passage is a view, not a document: typing into it changes nothing
    # and says why. The notice is a toast, which fades in a couple of seconds,
    # so the run remembers seeing it rather than looking for it on the final
    # screen. Polling starts right after the keystroke for the same reason: a
    # callable phase drains its delay before it ever looks, and the default
    # wait would outlive the toast it is waiting for.
    readonly_seen = {"value": False}

    def mark_readonly(candidate):
        if "read-only" in candidate.text():
            readonly_seen["value"] = True
            return True
        return False

    view = scene(":bible john 3:16 then typing into the passage",
                 command_phase("bible john 3:16", TAG)
                 + [(0.6, b"xyz"), (0.1, mark_readonly), (0.4, b"")]).text()
    if not readonly_seen["value"]:
        failures.append("typing into the passage was not refused as read-only")
    if ">16  John 3:16" not in view:
        failures.append("typing into the passage changed it")

    # 3. `:bible next` walks to the next chapter in the same tab.
    view = scene(":bible john 3 then :bible next",
                 command_phase("bible john 3", TAG)
                 + command_phase("bible next", "John 4 - Berean")).text()
    if "John 4 - Berean Standard Bible (BSB)" not in view:
        failures.append(":bible next did not walk to the next chapter")

    # 4. The cache: the same passage again is not requested a second time.
    before = count_requests("/BSB/JHN/3.simple.json")
    view = scene(":bible john 3:16 a second time (must come from the cache)",
                 command_phase("bible john 3:16", TAG)).text()
    after = count_requests("/BSB/JHN/3.simple.json")
    if after != before:
        failures.append("a cached passage was fetched again (%d -> %d)" % (before, after))
    if TAG not in view:
        failures.append("the cached passage did not render")

    # 5. Cross references from the dataset.
    view = scene(":bible xref john 3 (open-cross-ref)",
                 command_phase("bible xref john 3", "Romans 5:8")).text()
    if "Cross references - John 3 (open-cross-ref)" not in view:
        failures.append("the cross-reference header is not on screen")
    if "Romans 5:8" not in view:
        failures.append("the cross-reference link is not on screen")

    # 6. A commentary chapter.
    view = scene(":bible commentary john 3:16 (adam-clarke)",
                 command_phase("bible commentary john 3:16", TAG)).text()
    if "Adam Clarke Bible Commentary" not in view:
        failures.append("the commentary header is not on screen")
    if TAG not in view:
        failures.append("the commentary prose never reached the buffer")

    # 7. The translation catalog.
    view = scene(":bible translations",
                 command_phase("bible translations", "World English Bible")).text()
    if "Available translations (2)" not in view:
        failures.append("the translation list is not on screen")
    if "World English Bible" not in view:
        failures.append("a translation name is missing from the list")

    # 8. A whole chapter by abbreviation.
    view = scene(":bible ps23 (a whole chapter)",
                 command_phase("bible ps23", "Psalms 23")).text()
    if "Psalms 23 - Berean Standard Bible (BSB)" not in view:
        failures.append("the Psalm did not open by abbreviation")

    # 9. `:bible random` draws a verse and shows it with its context.
    view = scene(":bible random", command_phase("bible random", TAG)).text()
    if "Berean Standard Bible (BSB)" not in view:
        failures.append(":bible random did not land a passage")
    if TAG not in view:
        failures.append(":bible random did not render the served verse")

    # 10. The home screen's verse of the day: a bare launch lands on the home
    # screen, the verse is fetched from the fixture (cached for the day) and
    # centered on the bottom strip, and the footer names the build date and the
    # author. It must not open the `[Bible]` tab.
    print("scene: the home screen's verse of the day")
    home = run_in_pty(binary, [], b"", settle=9.0, after=1.0, cols=COLS, rows=ROWS,
                      cfg=CFG, cwd=ROOT,
                      env={"JOT_CONFIG_HOME": CFG, "JOT_CACHE_HOME": CFG},
                      phases=[(6.0, lambda screen: TAG in screen.text())])
    home_view = home.text()
    if dump:
        print(home_view)
        print("-" * 70)
    home_rows = home_view.splitlines()
    tag_row = next((i for i, line in enumerate(home_rows) if TAG in line), None)
    if tag_row is None:
        failures.append("the home screen's verse of the day never arrived")
    else:
        if tag_row < ROWS * 6 // 10:
            failures.append("the verse of the day is not on the bottom strip")
        line = home_rows[tag_row]
        left = len(line) - len(line.lstrip())
        right = len(line.rstrip())
        # Centered within a few cells of the window's middle.
        if abs(left + right - COLS) > 12:
            failures.append("the verse of the day is not centered")
    if "Last updated" not in home_view:
        failures.append("the home screen does not show when it was last updated")
    if "Developed by Joshua Ravael" not in home_view:
        failures.append("the home screen does not carry the credit line")
    if "[Bible]" in home_view:
        failures.append("the home screen's verse of the day stole the pane")

    server.shutdown()

    if failures:
        for failure in failures:
            print("bible probe: FAIL - %s" % failure)
        return 1
    print("bible probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
