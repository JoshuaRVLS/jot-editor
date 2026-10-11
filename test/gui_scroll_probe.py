#!/usr/bin/env python3
"""Probe: wheel scrolling in the GUI glides; it does not kick or teleport.

The GUI slide is pixel animation (gui/gui_anim.cpp), so a terminal screen
cannot show it. This drives the real binary on a private Xvfb display, sends
real wheel notches with XTEST, and reads the window back between frames to
watch the content move.

The landmarks are the diagnostic bands (the full-row background a warning
wears): a scripted `clangd` warns on lines spaced 3, 8, 4, 7, 5 and 6 rows
apart, repeating. Evenly spaced bands are no use here -- the pane repeats
every two notches, so a jump aliases onto a small move. The uneven gaps make
the visible arrangement identify exactly where the content is, and one whole
gap cycle (33 rows, 759 px) is wider than anything a single frame can cover,
which is what lets a teleport show up as a teleport.

Scenes:
  * calibration: three paced notches each move the content the same distance,
    a whole number of rows (the wheel step is proportional),
  * burst: eight notches in quick succession glide in one continuous motion:
    the content never moves backwards (the mid-glide snap this replaced threw
    the rest of the accumulated scroll backwards a whole pane at once), never
    overshoots where it lands, and lands exactly on 8 notches,
  * reversal: notches one way interrupted by notches back land where they
    started, turning around at most once.

Usage: test/gui_scroll_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary or no python-xlib).
"""
from __future__ import annotations

import os
import shutil
import stat
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from gui_screen import GuiSession, align_marks, banded_rows, landmark_tops, modal_row_color
except ImportError:
    GuiSession = None

if GuiSession is not None:
    try:
        import Xlib.display  # noqa: F401
    except ImportError:
        GuiSession = None

# The probe theme: plain black background so a band row is unmistakable, and
# the warning band on palette 58 (the same shape test/diagnostic_line_bg_probe
# pins in the terminal).
THEME = """{
  "extends": "jot",
  "Normal": {"fg": 7, "bg": 0},
  "DiagnosticWarn": {"bg": 58}
}
"""

# Warning line spacing, in rows. All six gaps differ, so any two visible bands
# tell the arrangement apart within a gap cycle; a cycle spans 33 rows, which
# is wider than the 22-row pane a wheel reaches through.
BAND_GAPS = [3, 8, 4, 7, 5, 6]
BAND_LINES = []
_line, _i = 3, 0
while _line < 195:
    BAND_LINES.append(_line)
    _line += BAND_GAPS[_i % len(BAND_GAPS)]
    _i += 1

FAKE_CLANGD = r'''#!/usr/bin/env python3
import json, os, sys

if "--version" in sys.argv:
    print("fake clangd 1.0.0")
    sys.exit(0)


def send(obj):
    body = json.dumps(obj).encode()
    sys.stdout.buffer.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
    sys.stdout.buffer.flush()


def publish(uri):
    diagnostics = []
    for line in BAND_LINES_PLACEHOLDER:
        diagnostics.append({
            "range": {"start": {"line": line, "character": 0},
                      "end": {"line": line, "character": 1}},
            "severity": 2,
            "message": "PROBE-WARN-%d" % line,
        })
    send({"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
          "params": {"uri": uri, "diagnostics": diagnostics}})


EMPTY = {
    "textDocument/completion": {"isIncomplete": False, "items": []},
    "textDocument/documentSymbol": [],
    "textDocument/inlayHint": [],
    "textDocument/codeAction": [],
    "textDocument/references": [],
    "textDocument/formatting": [],
}


def read_more():
    chunk = os.read(0, 65536)
    if not chunk:
        sys.exit(0)
    return chunk


pending = b""
while True:
    while b"\r\n\r\n" not in pending:
        pending += read_more()
    header, pending = pending.split(b"\r\n\r\n", 1)
    length = 0
    for line in header.split(b"\r\n"):
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    while len(pending) < length:
        pending += read_more()
    body, pending = pending[:length], pending[length:]
    msg = json.loads(body)
    method = msg.get("method")

    if method == "initialize":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": {"capabilities": {
            "textDocumentSync": {"openClose": True, "change": 1},
        }}})
    elif method == "shutdown":
        send({"jsonrpc": "2.0", "id": msg["id"], "result": None})
    elif method == "exit":
        sys.exit(0)
    elif method == "textDocument/didOpen":
        publish(msg["params"]["textDocument"]["uri"])
    elif "id" in msg:
        send({"jsonrpc": "2.0", "id": msg["id"],
              "result": EMPTY.get(method)})
'''.replace("BAND_LINES_PLACEHOLDER", repr(BAND_LINES))


def workspace(tmp: str) -> str:
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(os.path.join(tmp, "configs", "colors"))
    os.makedirs(os.path.join(tmp, "bin"))
    os.makedirs(os.path.join(tmp, "data"))
    with open(os.path.join(tmp, "configs", "settings.conf"), "w") as fh:
        fh.write("# jot configuration file\n\ncolor_scheme=guiscrollprobe\n")
    with open(os.path.join(tmp, "configs", "colors", "guiscrollprobe.json"), "w") as fh:
        fh.write(THEME)
    shim = os.path.join(tmp, "bin", "clangd")
    with open(shim, "w") as fh:
        fh.write(FAKE_CLANGD)
    os.chmod(shim, os.stat(shim).st_mode | stat.S_IEXEC)
    path = os.path.join(tmp, "probe.cpp")
    with open(path, "w") as fh:
        for i in range(200):
            fh.write("int value_%d = %d;\n" % (i, i))
    return path


def wait_for_marks(session):
    """(band colour, banded rows of a capture), or None when none appear.

    Only interior rows count: the window's first and last cell rows are the
    tab strip and the status line, uniform colours of their own that would
    otherwise pass as a band before the server has published anything.
    """
    size = session.cell_size()
    cell_h = size[1] if size else 24.0
    deadline = time.time() + 25.0
    while time.time() < deadline:
        rows = session.capture()
        interior = rows[int(cell_h) + 2: len(rows) - int(cell_h) - 2]
        if not interior:
            time.sleep(0.2)
            continue
        modal = modal_row_color(interior)
        counts = {}
        for row in interior:
            center = row[len(row) // 2]
            if center != modal:
                counts[center] = counts.get(center, 0) + 1
        for color in [c for c, n in counts.items() if n >= 3]:
            if len(banded_rows(rows, color)) >= 3:
                return color
        time.sleep(0.2)
    return None


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    dump = "--dump" in sys.argv
    if not os.path.exists(binary):
        print("gui scroll probe: SKIP - no binary at %s" % binary)
        return 2
    if GuiSession is None:
        print("gui scroll probe: SKIP - python-xlib is not installed")
        return 2

    tmp = "/tmp/jot_gui_scroll_probe"
    path = workspace(tmp)
    env = {
        "PATH": os.path.join(tmp, "bin") + os.pathsep + os.environ.get("PATH", ""),
        # A managed clangd install must not shadow the shim.
        "XDG_DATA_HOME": os.path.join(tmp, "data"),
    }
    session = GuiSession(binary, [path], cfg=tmp, env_extra=env)
    failures = []
    try:
        band_rgb = wait_for_marks(session)
        if band_rgb is None:
            print("gui scroll probe: FAIL - no diagnostic bands appeared (LSP did not attach)")
            return 1
        cell_w, cell_h = session.cell_size()
        template = [line * cell_h for line in BAND_LINES]
        band_value = session.packed_color(band_rgb)
        pane_rows = int((session.window_size()[1] - 2 * cell_h) // cell_h)
        print("bands %s, cell %.0fx%.0f, pane %d rows (template %d bands)"
              % (band_rgb, cell_w, cell_h, pane_rows, len(BAND_LINES)))

        offset = None

        def read(label):
            """Re-read where the content sits, in pixels from the pane's own
            top edge: an absolute position, not a per-frame delta, so nothing
            can hide between samples."""
            nonlocal offset
            tops = landmark_tops(session.capture_bands(band_value), cell_h)
            landed, explained = align_marks(tops, template, near=offset)
            if landed is None:
                failures.append("%s: %d band landmarks fit the template"
                                % (label or "reading", explained))
                return None
            if dump and label:
                print("   %-10s offset %5d (%d landmarks)" % (label, landed, explained))
            offset = landed
            return landed

        # Let the pane's own first-layout slide finish before measuring.
        time.sleep(0.5)
        if read("") is None:
            print("gui scroll probe: FAIL - no band landmark fits the known layout")
            return 1

        def steps_of(label, samples, allow_back):
            """Consecutive (dt, step) pairs, failing on stalls and teleports."""
            pairs = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(samples, samples[1:])]
            if dump:
                print("   %s offsets: %s"
                      % (label, ",".join(str(o) for _, o in samples)))
            if pairs and max(dt for dt, _ in pairs) > 0.03:
                failures.append("%s: sampling stalled %.0f ms"
                                % (label, max(dt for dt, _ in pairs) * 1000))
            for dt, step in pairs:
                if step < -4 and not allow_back:
                    failures.append("%s: content moved %d px backwards against the scroll"
                                    % (label, -step))
                    break
                # A frame cannot carry the content further than the model's own
                # peak speed over that span (a critically damped spring tops
                # out near omega * distance / e, a few px per ms for a pane of
                # scroll): this is where the snap shows up as a teleport.
                runaway = abs(step) if allow_back else step
                if runaway > 6.0 * dt * 1000.0 + 20.0:
                    failures.append("%s: content jumped %d px in %.0f ms (teleport)"
                                    % (label, abs(step), dt * 1000))
                    break
            return pairs

        def glide(plan, tail=1.0):
            """Sample the content while the wheel plan plays out.

            `plan` is (seconds from now, clicks, down). The notches are sent
            from inside the sampling loop so no window goes unwatched: a
            teleport is a step no continuous motion could have taken, and a
            window with no samples in it is a window where one could pass.
            """
            queue = [(time.time() + delay, clicks, down) for delay, clicks, down in plan]
            samples = [(time.time(), offset)]
            end = (queue[-1][0] if queue else time.time()) + tail
            while time.time() < end:
                if queue and queue[0][0] <= time.time():
                    _, clicks, down = queue.pop(0)
                    session.wheel(clicks=clicks, down=down)
                landed = read("")
                if landed is None:
                    break
                samples.append((time.time(), landed))
                time.sleep(0.003)
            return samples

        # Scene 1: three paced notches, each landing the same, whole number of
        # rows away -- and the first of them glides, since the pane reports its
        # body every frame and so has a viewport to slide from from the start.
        steps = []
        for i in range(3):
            before = offset
            samples = glide([(0.0, 1, True)], tail=0.6)
            landed = samples[-1][1]
            if i == 0 or dump:
                steps_of("notch %d" % i, samples, allow_back=False)
            steps.append(landed - before)
        print("per-notch shift px: %s" % steps)
        if len(steps) < 3:
            failures.append("only %d of 3 paced notches were readable" % len(steps))
        else:
            for step in steps[1:]:
                if abs(step - steps[0]) > 4:
                    failures.append("notches moved different distances: %s" % steps)
            rows = abs(steps[0]) / cell_h
            if abs(rows - round(rows)) > 0.15:
                failures.append("a notch moved %.2f rows, not whole rows" % rows)
        notch = abs(steps[0]) if steps else 0.0

        # Scene 2: a burst glides as one motion and lands on the exact notch
        # count. The snap this replaced threw the rendered content back to the
        # logical position the moment the accumulated scroll outgrew a pane,
        # which is a step against the scroll the backward check below sees.
        settled = offset
        samples = glide([(i * 0.03, 1, True) for i in range(8)])
        end = samples[-1][1]
        print("burst: %d samples, settled %d px (want %d)"
              % (len(samples), end - settled, 8 * int(notch)))
        steps_of("burst", samples, allow_back=False)
        peak = max(o for _, o in samples)
        if peak > end + 6:
            failures.append("burst overshot to %d before settling on %d" % (peak, end))
        if notch and abs((end - settled) - 8 * notch) > 8:
            failures.append("burst settled %d px off %d"
                            % (end - settled, 8 * int(notch)))

        # Scene 3: three notches one way, then three back, mid-glide: land
        # where the scene started, turning around once.
        settled = offset
        samples = glide([(i * 0.03, 1, True) for i in range(3)]
                        + [(0.12 + i * 0.03, 1, False) for i in range(3)])
        end = samples[-1][1]
        print("reversal: %d samples, net %d px (want 0)" % (len(samples), end - settled))
        pairs = steps_of("reversal", samples, allow_back=True)
        if abs(end - settled) > 8:
            failures.append("reversal settled %d px from where it started" % (end - settled))
        if notch:
            peak, low = max(o for _, o in samples), min(o for _, o in samples)
            if peak > settled + 3 * notch + 6:
                failures.append("reversal went %d px past the notches it was given"
                                % (peak - settled - 3 * notch))
            if low < settled - 6:
                failures.append("reversal undershot to %d before returning to %d"
                                % (low, settled))
        signs = [1 if step > 4 else (-1 if step < -4 else 0) for _, step in pairs]
        turns = sum(1 for i in range(1, len(signs)) if signs[i] and signs[i - 1]
                    and signs[i] != signs[i - 1])
        if turns > 2:
            failures.append("reversal turned around %d times" % turns)
    finally:
        session.stop()

    if failures:
        for failure in failures:
            print("gui scroll probe: FAIL - %s" % failure)
        return 1
    print("gui scroll probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
