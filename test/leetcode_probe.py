#!/usr/bin/env python3
"""Exercise the LeetCode feature against a local GraphQL fixture.

Exit codes: 0 pass, 1 fail, 2 skip (no built binary).
"""
from __future__ import annotations

import http.server
import json
import os
import shutil
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

COLS, ROWS = 120, 32
ROOT = "/tmp/jot_leetcode_probe"
CFG = "/tmp/jot_leetcode_probe_cfg"
SLUG = "two-sum"
TITLE = "Two Sum Local Fixture"
SECOND_SLUG = "add-two-numbers"
SECOND_TITLE = "Add Two Numbers Local Fixture"
REQUESTS: list[dict] = []
RESPONDED: list[bool] = []
COOKIE = "LEETCODE_SESSION=probe-cookie; csrftoken=csrf"
PASTE = b"\x1b[200~" + b"LEETCODE_SESSION=probe-cookie; \r\ncsrftoken=csrf" + b"\x1b[201~"
PROMPT_INTRO = "Open LeetCode in your browser."


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        REQUESTS.append({"path": self.path, "cookie": self.headers.get("Cookie"), "body": raw})
        if self.path.startswith("/interpret_solution/"):
            self.respond({"interpret_id": "probe-run"})
            return
        if self.path.startswith("/submit/"):
            self.respond({"submission_id": "probe-submit"})
            return
        query = json.loads(raw).get("query", "")
        if "questionOfToday" in query:
            payload = {"data": {"today": {"question": {"title_slug": SLUG}}}}
        elif "questionData" in query:
            payload = {"data": {"question": {
                "id": "1", "frontend_id": "1", "title": TITLE, "title_slug": SLUG,
                "difficulty": "Easy", "status": "ac",
                "content": "<p>Return indices of two values.</p>",
                # The live API returns exampleTestcaseList as an array of case
                # strings, not one blank-line separated string; the fixture
                # mirrors that so a parser that only handles strings fails here.
                "testcase_list": ["[2,7,11,15]\\n9", "[3,3]\\n6"],
                "topic_tags": [{"name": "Array"}],
                "code_snippets": [{"lang": "C++", "lang_slug": "cpp", "code": "class Solution { public: int twoSum(vector<int>& nums, int target) {} };"}],
            }}}
        elif "questionTopicTags" in query:
            # The tag group's picker source: the live API returns every topic
            # in one public unpaginated page.
            payload = {"data": {"topicTags": {"edges": [
                {"node": {"name": "Backtracking", "slug": "backtracking"}},
                {"node": {"name": "Array", "slug": "array"}},
            ]}}}
        elif "companyTags" in query:
            payload = {"data": {"companyTags": [
                {"name": "Google", "slug": "google"},
                {"name": "Amazon", "slug": "amazon"},
            ]}}
        elif "myCreatedFavoriteList" in query:
            # Anonymous, like the live API: no session means no created lists,
            # so the probe pins the "no favorites yet" answer.
            payload = {"data": {"myCreatedFavoriteList": {"favorites": []}}}
        elif "problemsetQuestionList" in query:
            # Slow enough that the probe can observe the live status indicator
            # while the request is in flight. Page two is served from the same
            # handler so the picker's load-more row has something to fetch.
            time.sleep(0.9)
            variables = json.loads(raw).get("variables", {})
            if int(variables.get("skip") or 0) == 0:
                questions = [{
                    "frontend_id": "1", "title": TITLE, "title_slug": SLUG,
                    "difficulty": "Easy", "status": "ac", "paid_only": False,
                    "topic_tags": [{"name": "Array", "slug": "array"}],
                }]
            else:
                questions = [{
                    "frontend_id": "2", "title": SECOND_TITLE, "title_slug": SECOND_SLUG,
                    "difficulty": "Medium", "status": None, "paid_only": False,
                    "topic_tags": [{"name": "Linked List", "slug": "linked-list"}],
                }]
            payload = {"data": {"problemsetQuestionList": {"total": 2, "questions": questions}}}
        elif "globalData" in query:
            payload = {"data": {"userStatus": {"id": None, "name": "", "is_signed_in": False, "is_verified": False}}}
        else:
            payload = {"errors": [{"message": "fixture has no such query"}]}
        self.respond(payload)

    def respond(self, payload):
        data = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        RESPONDED.append(True)

    def do_GET(self):
        REQUESTS.append({"path": self.path, "cookie": self.headers.get("Cookie"), "body": b""})
        if self.path.startswith("/submissions/detail/"):
            # Held briefly so the console's in-flight line is observable before
            # the result lands.
            time.sleep(1.0)
            payload = {"status_code": 10, "status_msg": "Accepted", "total_correct": 1,
                       "total_testcases": 1, "runtime": "4", "memory": "10.2 MB",
                       "code_answer": ["[0,1]"], "expected_code_answer": ["[0,1]"]}
        else:
            payload = {}
        data = json.dumps(payload).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        RESPONDED.append(True)


def command_phase(command: str):
    return [(0.5, b"\x1b[112;6u"), (0.6, command.encode()), (0.4, b"\r")]


def on_screen(fragment: str):
    return lambda screen: fragment in screen.text()


def list_phase(command: str = "LeetList"):
    """Open the plain problem list through the group picker.

    `:LeetList` without filters opens five groups now, so a phase that wants
    the unfiltered list picks its first row (All problems) and waits for the
    fetch that row starts.
    """
    return command_phase(command) + [(4.0, on_screen("All problems")), (0.3, b"\r")]


def list_filters(request: dict):
    """The QuestionListFilterInput a /graphql/ request sent, or None."""
    if not request["path"].endswith("/graphql/"):
        return None
    try:
        return (json.loads(request["body"]).get("variables") or {}).get("filters")
    except (ValueError, TypeError):
        return None


def cell_of(screen, needle: str) -> tuple[int, int]:
    """0-based (x, y) of `needle`'s first cell, or (-1, -1)."""
    for y, line in enumerate(screen.text().split("\n")):
        x = line.find(needle)
        if x >= 0:
            return x, y
    return -1, -1


def row_colors(screen, needle: str) -> tuple[int, int]:
    """(fg, bg) of the first cell of the row carrying `needle`."""
    x, y = cell_of(screen, needle)
    if y < 0:
        return -1, -1
    return screen.fg[y][x], screen.bg[y][x]


def motion(x: int, y: int) -> bytes:
    """SGR any-motion report over a cell."""
    return b"\x1b[<35;%d;%dM" % (x + 1, y + 1)


def press(x: int, y: int) -> bytes:
    """SGR press+release on a cell."""
    return b"\x1b[<0;%d;%dM\x1b[<0;%d;%dm" % (x + 1, y + 1, x + 1, y + 1)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build-tests/apps/jot/jot"
    if not os.path.exists(binary):
        print("leetcode probe: SKIP - no binary at %s" % binary)
        return 2

    shutil.rmtree(ROOT, ignore_errors=True)
    shutil.rmtree(CFG, ignore_errors=True)
    os.makedirs(ROOT, exist_ok=True)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    os.makedirs(os.path.join(CFG, "configs"), exist_ok=True)
    # The solution dir is pinned under the probe root as well: a leftover
    # template in the real data dir would let a broken read-and-write path
    # pass by accident (the file the editor opens would already exist).
    solutions = os.path.join(CFG, "solutions")
    os.makedirs(solutions, exist_ok=True)
    with open(os.path.join(CFG, "configs", "settings.conf"), "w") as fh:
        fh.write("lsp_enabled=false\nleetcode_api_base=http://127.0.0.1:%d\n"
                 "leetcode_solution_dir=%s\n" % (port, solutions))
    os.makedirs(ROOT, exist_ok=True)
    env = {"JOT_CONFIG_HOME": CFG, "JOT_CACHE_HOME": CFG}
    # The Linux credential store refuses to open until a session bus and
    # secret-tool exist, which is why the sign-in prompt used to be skipped on
    # the platform where it shipped broken. Stub both so the prompt itself is
    # exercised everywhere; the stub records what was stored on the way out.
    stub_bin = os.path.join(CFG, "stub_bin")
    stub_store = os.path.join(CFG, "stub_session")
    os.makedirs(stub_bin, exist_ok=True)
    stub = os.path.join(stub_bin, "secret-tool")
    with open(stub, "w") as fh:
        fh.write("#!/bin/sh\n"
                 "case \"$1\" in\n"
                 "  lookup) if [ -f \"$JOT_LEETCODE_STUB_STORE\" ]; then cat \"$JOT_LEETCODE_STUB_STORE\"; exit 0; fi; exit 1 ;;\n"
                 "  store) cat > \"$JOT_LEETCODE_STUB_STORE\" ;;\n"
                 "  clear) rm -f \"$JOT_LEETCODE_STUB_STORE\" ;;\n"
                 "esac\nexit 0\n")
    os.chmod(stub, 0o755)
    cookie_env = {"JOT_LEETCODE_STUB_STORE": stub_store}
    if os.name != "nt" and sys.platform != "darwin":
        cookie_env["PATH"] = stub_bin + os.pathsep + os.environ.get("PATH", "")
        cookie_env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/tmp/jot-leetcode-probe-bus"

    loading_seen = {"value": False}
    feedback_seen = {"value": False}
    groups_seen = {"value": False}
    favorites_seen = {"value": False}

    def mark_favorites(screen):
        # The empty-favorites answer is a message-line notice, which fades; the
        # run has to remember seeing it, not the final screen.
        if "No favorite lists found" in screen.text():
            favorites_seen["value"] = True
            return True
        return False

    def mark_groups(screen):
        # :LeetList without filters is the five groups, not every problem.
        if all(label in screen.text()
               for label in ("All problems", "Difficulty", "Tag", "By company", "Favorites")):
            groups_seen["value"] = True
            return True
        return False

    def mark_loading(screen):
        if "leet: loading list" in screen.text():
            loading_seen["value"] = True
            return True
        return False

    def mark_feedback(screen):
        if "Loaded 1 of 2 problem" in screen.text():
            feedback_seen["value"] = True
            return True
        return False

    # :LeetPanel brings the right dock's LeetCode panel up on its own, before a
    # question or a run has. Nothing is open yet, so the panel's own empty state
    # is the proof: the dock says no problem is open and points at the commands
    # that choose one.
    panel = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                       cfg=CFG, cwd=ROOT, env=env,
                       phases=command_phase("LeetPanel") + [
                           (3.0, on_screen("No problem open")),
                           (0.4, lambda s: True),
                       ], until_timeout=15.0)
    if "No problem open" not in panel.text():
        print("leetcode probe: FAIL - :LeetPanel did not open the dock panel")
        print(panel.text())
        server.shutdown()
        return 1

    screen = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT, env=env,
                        phases=command_phase("LeetList") + [(4.0, mark_groups),
                                                            (0.3, b"\r"),
                                                            (0.0, mark_loading),
                                                            (0.0, mark_feedback),
                                                            (4.0, on_screen(TITLE))])
    if not groups_seen["value"]:
        print("leetcode probe: FAIL - :LeetList did not offer the five problem groups")
        print(screen.text())
        server.shutdown()
        return 1
    if not RESPONDED:
        print("leetcode probe: FAIL - HTTP request did not complete")
        server.shutdown()
        return 1
    if not loading_seen["value"]:
        print("leetcode probe: FAIL - no loading indicator while the list request was in flight")
        print(screen.text())
        server.shutdown()
        return 1
    if TITLE not in screen.text():
        print("leetcode probe: FAIL - local problem fixture was not listed")
        print(screen.text())
        print("requests:", REQUESTS)
        print("responses:", RESPONDED)
        server.shutdown()
        return 1
    if not feedback_seen["value"]:
        print("leetcode probe: FAIL - list completion had no feedback message")
        print(screen.text())
        server.shutdown()
        return 1
    if not REQUESTS or REQUESTS[-1]["cookie"]:
        print("leetcode probe: FAIL - public list request missing or carried a cookie")
        server.shutdown()
        return 1
    # The list is the checklist: a solved problem leads with a check.
    if "\u2713" not in screen.text():
        print("leetcode probe: FAIL - a solved problem is not checked off in the list")
        print(screen.text())
        server.shutdown()
        return 1

    # The list picker's trailing row must fetch the next page: one page is
    # capped at 100 questions server-side, so without it the user could never
    # see past the first page.
    load_start = len(REQUESTS)
    more = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                      cfg=CFG, cwd=ROOT, env=env,
                      phases=list_phase() + [
                          (6.0, on_screen("Load more problems")),
                          (0.3, b"load"),
                          # Wait for the query to filter the list down to the
                          # load-more row before accepting it: pressing Enter
                          # while the unfiltered list is still up opens the
                          # question instead, which made this phase flaky.
                          (6.0, lambda s: "Load more problems" in s.text()
                                          and TITLE not in s.text()),
                          (0.5, b"\r"),
                          (6.0, on_screen(SECOND_TITLE)),
                      ], until_timeout=20.0)
    if SECOND_TITLE not in more.text():
        print("leetcode probe: FAIL - load-more did not fetch the next page")
        print(more.text())
        server.shutdown()
        return 1
    if not any(b'"skip":100' in request["body"] or b'"skip": 100' in request["body"]
               for request in REQUESTS[load_start:]):
        print("leetcode probe: FAIL - load-more did not request the second page")
        print("requests:", REQUESTS[load_start:])
        server.shutdown()
        return 1

    # The groups are real filters: difficulty, tag and company each open a
    # second picker and the chosen value becomes the list's filter. The tag and
    # company sources are public queries, like the live API's.
    levels_start = len(REQUESTS)
    levels = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT, env=env,
                        phases=command_phase("LeetList") + [
                            # The groups are navigated by arrows: row 2 is
                            # Difficulty, and its row 3 is Medium.
                            (4.0, on_screen("All problems")),
                            (0.5, b"\x1b[B"),
                            (0.5, b"\r"),
                            (4.0, on_screen("All difficulties")),
                            (0.5, b"\x1b[B\x1b[B"),
                            (0.5, b"\r"),
                            (8.0, on_screen(TITLE)),
                        ], until_timeout=20.0)
    if TITLE not in levels.text():
        print("leetcode probe: FAIL - the difficulty group did not open the problem list")
        print(levels.text())
        server.shutdown()
        return 1
    if not any((list_filters(request) or {}).get("difficulty") == "MEDIUM"
               for request in REQUESTS[levels_start:]):
        print("leetcode probe: FAIL - the difficulty group did not filter the list")
        print("requests:", REQUESTS[levels_start:])
        server.shutdown()
        return 1

    tag_start = len(REQUESTS)
    tagged = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT, env=env,
                        phases=command_phase("LeetList") + [
                            # Row 3 is Tag; its picker is sorted, so row 2 is
                            # Backtracking.
                            (4.0, on_screen("All problems")),
                            (0.5, b"\x1b[B\x1b[B"),
                            (0.5, b"\r"),
                            (4.0, on_screen("Backtracking")),
                            (0.5, b"\x1b[B"),
                            (0.5, b"\r"),
                            (8.0, on_screen(TITLE)),
                        ], until_timeout=20.0)
    if TITLE not in tagged.text():
        print("leetcode probe: FAIL - the tag group did not open the problem list")
        print(tagged.text())
        server.shutdown()
        return 1
    recent = REQUESTS[tag_start:]
    if not any(b"questionTopicTags" in request["body"] for request in recent):
        print("leetcode probe: FAIL - the tag group did not fetch the tag list")
        print("requests:", recent)
        server.shutdown()
        return 1
    if not any((list_filters(request) or {}).get("tags") == ["backtracking"] for request in recent):
        print("leetcode probe: FAIL - the tag group did not filter the list by tag")
        print("requests:", recent)
        server.shutdown()
        return 1
    if any(request["cookie"] for request in recent):
        print("leetcode probe: FAIL - a public tag query carried credentials")
        print("requests:", recent)
        server.shutdown()
        return 1

    company_start = len(REQUESTS)
    company = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                         cfg=CFG, cwd=ROOT, env=env,
                         phases=command_phase("LeetList") + [
                             # Row 4 is By company; its picker is sorted, so row
                             # 1 is already Amazon.
                             (4.0, on_screen("All problems")),
                             (0.5, b"\x1b[B\x1b[B\x1b[B"),
                             (0.5, b"\r"),
                             (4.0, on_screen("Google")),
                             (0.5, b"\r"),
                             (8.0, on_screen(TITLE)),
                         ], until_timeout=20.0)
    if TITLE not in company.text():
        print("leetcode probe: FAIL - the company group did not open the problem list")
        print(company.text())
        server.shutdown()
        return 1
    recent = REQUESTS[company_start:]
    if not any(b"companyTags" in request["body"] for request in recent):
        print("leetcode probe: FAIL - the company group did not fetch the company list")
        print("requests:", recent)
        server.shutdown()
        return 1
    if not any((list_filters(request) or {}).get("companies") == ["amazon"] for request in recent):
        print("leetcode probe: FAIL - the company group did not filter the list by company")
        print("requests:", recent)
        server.shutdown()
        return 1
    if any(request["cookie"] for request in recent):
        print("leetcode probe: FAIL - a public company query carried credentials")
        print("requests:", recent)
        server.shutdown()
        return 1

    # Favorites with no stored session stay anonymous (the live API answers an
    # empty list) and the answer points at the sign-in command instead of
    # failing; no credential may reach the local fixture either way. The stub
    # credential environment pins "no session" here: the real OS store could
    # hold one, and then the request would be refused for the non-official
    # domain before it ever reached the fixture.
    favorite_start = len(REQUESTS)
    favorites = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                           cfg=CFG, cwd=ROOT, env=cookie_env,
                           phases=command_phase("LeetList") + [
                               # Row 5 is Favorites.
                               (4.0, on_screen("All problems")),
                               (0.5, b"\x1b[B\x1b[B\x1b[B\x1b[B"),
                               (0.5, b"\r"),
                               # Poll at once: the empty-favorites notice is a
                               # toast that fades on its own.
                               (0.0, mark_favorites),
                           ], until_timeout=20.0)
    if not favorites_seen["value"]:
        print("leetcode probe: FAIL - the favorite group did not report an empty signed-out list")
        print(favorites.text())
        server.shutdown()
        return 1
    recent = REQUESTS[favorite_start:]
    if not any(b"myCreatedFavoriteList" in request["body"] for request in recent):
        print("leetcode probe: FAIL - the favorite group did not ask for the favorite lists")
        print("requests:", recent)
        server.shutdown()
        return 1
    if any(request["cookie"] for request in recent):
        print("leetcode probe: FAIL - a signed-out favorite query carried credentials")
        print("requests:", recent)
        server.shutdown()
        return 1

    # Choosing a problem from the list: the language picker opens the template
    # for real (the missing-file read used to leave the buffer empty, so the
    # user saw "loading" and then nothing), and the dock's LeetCode panel comes
    # up with the question, its clickable Run test / Submit rows and the judge
    # console. The fixture is not an official LeetCode domain, so the
    # authenticated judge request is refused before it leaves the process --
    # the console reports that refusal and the assertion pins that no request
    # reached the local endpoint with credentials.
    judge_start = len(REQUESTS)
    judge = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                       cfg=CFG, cwd=ROOT, env=env,
                       phases=list_phase() + [
                           (6.0, on_screen(TITLE)),
                           (0.5, b"\r"),
                           (6.0, on_screen("C++")),
                           (0.5, b"\r"),
                           (5.0, on_screen("class Solution")),
                           (3.0, on_screen("Run test")),
                           (0.4, b"\x1b[112;6u"), (0.6, b"LeetRun"), (0.4, b"\r"),
                           (8.0, on_screen("Judge request failed")),
                       ], until_timeout=20.0)
    if "class Solution" not in judge.text():
        print("leetcode probe: FAIL - selecting a language did not open the solution template")
        print(judge.text())
        server.shutdown()
        return 1
    if "Run test" not in judge.text() or "Submit" not in judge.text():
        print("leetcode probe: FAIL - the dock panel did not offer the run/submit rows")
        print(judge.text())
        server.shutdown()
        return 1
    # The error row is clipped to the dock's width, so only its leading cells
    # are asserted; the full sentence is pinned by the Lua unit tests.
    if "Judge request failed" not in judge.text() or "restricted to" not in judge.text():
        print("leetcode probe: FAIL - the dock panel console did not report the run outcome")
        print(judge.text())
        server.shutdown()
        return 1
    recent = REQUESTS[judge_start:]
    if len(recent) < 2 or any(request["cookie"] for request in recent):
        print("leetcode probe: FAIL - daily/question requests were missing or carried credentials")
        print("requests:", recent)
        server.shutdown()
        return 1
    if any(request["path"].startswith("/interpret_solution/") for request in recent):
        print("leetcode probe: FAIL - a judge request reached the non-official endpoint")
        print("requests:", recent)
        server.shutdown()
        return 1

    # The run/submit rows are buttons in the dock: hovering one lights its
    # background and clicking it runs the action through the panel callback.
    # The click path is what the user asked for (instead of typing :LeetRun),
    # so it is pinned against the real binary, not just the Lua unit tests.
    # The panel's rows depend on what the frame spends above the dock, so the
    # row's cells are measured off the grid first and the click reuses them.
    click_start = len(REQUESTS)
    opened = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                        cfg=CFG, cwd=ROOT, env=env,
                        phases=list_phase() + [
                            (6.0, on_screen(TITLE)),
                            (0.5, b"\r"),
                            (6.0, on_screen("C++")),
                            (0.5, b"\r"),
                            (5.0, on_screen("Run test")),
                        ], until_timeout=20.0)
    run_x, run_y = cell_of(opened, "Run test")
    if run_x < 0:
        print("leetcode probe: FAIL - the Run test row is not on screen to click")
        print(opened.text())
        server.shutdown()
        return 1
    # The question fetch carries its status, so the dock can say the problem
    # is already solved instead of leaving the user to remember.
    if "Completed" not in opened.text():
        print("leetcode probe: FAIL - the dock did not mark the solved problem")
        print(opened.text())
        server.shutdown()
        return 1
    hovered = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                         cfg=CFG, cwd=ROOT, env=env,
                         phases=list_phase() + [
                             (6.0, on_screen(TITLE)),
                             (0.5, b"\r"),
                             (6.0, on_screen("C++")),
                             (0.5, b"\r"),
                             (5.0, on_screen("Run test")),
                             (0.4, motion(run_x, run_y)),
                             (1.0, lambda s: True),
                         ], until_timeout=20.0)
    # `opened` is the same screen without the pointer, so it is the baseline
    # the hover highlight is told apart from (one less editor run).
    if row_colors(hovered, "Run test")[1] == row_colors(opened, "Run test")[1]:
        print("leetcode probe: FAIL - hovering the Run test row did not highlight it")
        print(hovered.text())
        server.shutdown()
        return 1

    clicked = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                         cfg=CFG, cwd=ROOT, env=env,
                         phases=list_phase() + [
                             (6.0, on_screen(TITLE)),
                             (0.5, b"\r"),
                             (6.0, on_screen("C++")),
                             (0.5, b"\r"),
                             (5.0, on_screen("Run test")),
                             (0.4, press(run_x, run_y)),
                             (8.0, on_screen("Judge request failed")),
                         ], until_timeout=20.0)
    if "Judge request failed" not in clicked.text():
        print("leetcode probe: FAIL - clicking the Run test row did not run the judge")
        print(clicked.text())
        server.shutdown()
        return 1
    if any(request["cookie"] for request in REQUESTS[click_start:]):
        print("leetcode probe: FAIL - the clicked run sent credentials to the local fixture")
        print("requests:", REQUESTS[click_start:])
        server.shutdown()
        return 1

    # The panel's keys belong to the panel. Clicking back into the buffer keeps
    # the caret there, so q is a letter and Esc is an editor key; with the dock
    # merely visible they used to close it mid-word and swallow the keystroke.
    # The click lands in the code pane (past the explorer's column and the
    # line-number gutter, left of the dock) so the caret moves there; a click
    # in the explorer column would keep the focus in the tree.
    editing = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                         cfg=CFG, cwd=ROOT, env=env,
                         phases=list_phase() + [
                             (6.0, on_screen(TITLE)),
                             (0.5, b"\r"),
                             (6.0, on_screen("C++")),
                             (0.5, b"\r"),
                             (5.0, on_screen("Run test")),
                             (0.4, press(40, 3)),
                             (0.5, b"qz"),
                             (0.5, b"\x1b"),
                             (1.5, lambda s: "qz" in s.text()),
                         ], until_timeout=20.0)
    if "qz" not in editing.text():
        print("leetcode probe: FAIL - q was swallowed instead of typed into the buffer")
        print(editing.text())
        server.shutdown()
        return 1
    if "Run test" not in editing.text():
        print("leetcode probe: FAIL - Esc closed the dock while the editor had focus")
        print(editing.text())
        server.shutdown()
        return 1

    cookie_start = len(REQUESTS)
    if os.name != "nt" and sys.platform != "darwin":
        masked = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                            cfg=CFG, cwd=ROOT, env=cookie_env,
                            phases=command_phase("LeetCookie") + [
                                (1.0, on_screen(PROMPT_INTRO)),
                                (0.4, PASTE),
                                (1.0, on_screen("Session  **")),
                            ])
        if PROMPT_INTRO not in masked.text() or "Session  **" not in masked.text():
            print("leetcode probe: FAIL - credential prompt or masked paste did not appear")
            print(masked.text())
            server.shutdown()
            return 1
        if "probe-cookie" in masked.text():
            print("leetcode probe: FAIL - pasted cookie was shown in clear")
            print(masked.text())
            server.shutdown()
            return 1
        signin_feedback = {"value": False}

        def mark_signin(screen):
            # The local fixture is not an official LeetCode HTTPS domain, so
            # the auth request is refused before it goes out; the refusal is
            # itself the visible feedback this asserts. Real custom api_base
            # setups must not receive the stored cookie.
            if "restricted to official LeetCode domains" in screen.text():
                signin_feedback["value"] = True
                return True
            return False

        screen = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                            cfg=CFG, cwd=ROOT, env=cookie_env,
                            phases=command_phase("LeetCookie") + [
                                (1.0, on_screen(PROMPT_INTRO)),
                                (0.4, PASTE),
                                (1.0, on_screen("Session  **")),
                                (0.4, b"\r"),
                                (1.0, lambda s: PROMPT_INTRO not in s.text()),
                                (0.0, mark_signin),
                            ])
        stored = ""
        if os.path.exists(stub_store):
            stored = open(stub_store).read().strip()
        if stored != COOKIE:
            print("leetcode probe: FAIL - pasted cookie was not stored (got %r)" % stored)
            print(screen.text())
            server.shutdown()
            return 1
        if "probe-cookie" in screen.text():
            print("leetcode probe: FAIL - cookie stayed visible or leaked after save")
            print(screen.text())
            server.shutdown()
            return 1
        if not signin_feedback["value"]:
            print("leetcode probe: FAIL - storing the cookie produced no sign-in feedback")
            print(screen.text())
            server.shutdown()
            return 1

        screen = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                            cfg=CFG, cwd=ROOT, env=cookie_env,
                            phases=command_phase("LeetSignOut") + [(1.0, on_screen("Session removed"))])
        if "Session removed from the OS credential store" not in screen.text():
            print("leetcode probe: FAIL - sign-out produced no feedback")
            print(screen.text())
            server.shutdown()
            return 1
        if os.path.exists(stub_store):
            print("leetcode probe: FAIL - sign-out left the stored session behind")
            server.shutdown()
            return 1
    else:
        screen = run_in_pty(binary, [ROOT], b"", settle=3.5, after=0.4, cols=COLS, rows=ROWS,
                            cfg=CFG, cwd=ROOT, env=env,
                            phases=command_phase("LeetCookie") + [(1.0, on_screen(PROMPT_INTRO))])
        if PROMPT_INTRO not in screen.text():
            print("leetcode probe: FAIL - credential prompt did not open")
            print(screen.text())
            server.shutdown()
            return 1

    if any(request["cookie"] for request in REQUESTS[:cookie_start]):
        print("leetcode probe: FAIL - a public request carried credentials")
        server.shutdown()
        return 1
    server.shutdown()
    print("leetcode probe: PASS - problem groups, list, cookie-free public requests and masked sign-in")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
