#!/usr/bin/env python3
"""Probe: :compile's prompt runs the shell command that is typed into it.

The compile session is a shell process, so the unit tests drive it through the
job's own file: they hand the command to the run function and read back what the
log holds. What only a real screen shows is the prompt itself -- the line the
reader types a command on, what it re-offers the next time, and that Enter hands
the line to `/bin/sh` rather than to any parsing of the editor's own. A quote or
a pipe that the editor tried to understand would come out differently in the log
than in the terminal.

Commands go through the Ctrl+P palette, the way a user runs them: the editor has
no `:` command line of its own.

Scenes: a command with a quote and a pipe typed into the prompt (the log holds
the line as typed and the shell's output, and the file being read keeps the
screen -- with no tab strip of its own above the log, because a build's output
is shown in its split rather than opened as a tab of it); a build that fails, then `:cnext` landing on the file it named; the
prompt re-offering the last command, cancelled with Esc; a job that asks whether
it has a terminal and reads its stdin (the same prompt runs a program, not only
a build, and stdin answers with EOF instead of waiting for a keyboard); and a
command that prints colour, whose words have to reach the log while the escape
sequences reach the renderer -- asserted in the screen's own escape stream, not
in its text, because that is the only place the colour exists.

Usage: test/compile_prompt_probe.py [path-to-jot] [--dump]
Exit codes: 0 pass, 1 fail, 2 skipped (no binary).
"""
from __future__ import annotations

import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

CTRL_P = b"\x10"  # palette
ENTER = b"\r"
ESC = b"\x1b"

NOTES = "the file the reader was reading\n"
BROKEN = "int main() {\n  return missing;\n}\n"
# What a compiler prints on failure, in the shape the finding parser reads.
BUILD_SH = (
    "#!/bin/sh\n"
    "printf \"broken.cpp:2:10: error: use of undeclared identifier 'missing'\\n\"\n"
    "exit 1\n"
)


def command(name: str) -> bytes:
    return CTRL_P + name.encode() + ENTER


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dump = "--dump" in sys.argv
    binary = args[0] if args else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"compile prompt probe: SKIP - no binary at {binary}")
        return 2

    work = "/tmp/jot_compile_prompt_probe"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    with open(os.path.join(work, "notes.txt"), "w") as fh:
        fh.write(NOTES)
    with open(os.path.join(work, "broken.cpp"), "w") as fh:
        fh.write(BROKEN)
    with open(os.path.join(work, "build.sh"), "w") as fh:
        fh.write(BUILD_SH)
    cfg = "/tmp/jot_compile_prompt_probe_cfg"
    shutil.rmtree(cfg, ignore_errors=True)

    failures = 0

    def check(name: str, ok: bool, detail: str = ""):
        nonlocal failures
        print(f"compile prompt probe: {'ok' if ok else 'FAIL'} - {name}"
              f"{(': ' + detail) if (detail and not ok) else ''}")
        if not ok:
            failures += 1

    def session(keys: bytes, phases=None, after: float = 3.0):
        return run_in_pty(binary, [os.path.join(work, "notes.txt")], keys,
                          settle=3.0, after=after, cols=110, rows=32,
                          cfg=cfg, cwd=work, phases=phases)

    # 1. A command with a quote and a pipe, typed on the prompt: the log holds the
    #    line as it was typed and the shell's output, which means the editor
    #    handed it over rather than reading it. The pane the reader was in keeps
    #    its file while the split below it fills with the log.
    text = session(command("compile")
                   + b"printf 'probe says hello\\n' | tr a-z A-Z" + ENTER).text()
    check("the log says the build started", "Compilation started at" in text)
    check("the log holds the command as typed",
          "$ printf 'probe says hello" in text and "tr a-z A-Z" in text,
          "the line in the log is not the line that was typed")
    check("the shell ran it", "PROBE SAYS HELLO" in text,
          "the pipeline's output is not in the log")
    check("the log closes the run", "Compilation finished at" in text)
    check("the file being read keeps the screen",
          "the file the reader was reading" in text)
    # ... and the log is not one of the opened tabs: its split draws no strip
    # row (the rule above its first line is the pane divider), and the strip the
    # file's pane draws lists the file alone. A build's output is shown in a
    # pane, not opened in one.
    lines = text.splitlines()
    log_row = next((i for i, line in enumerate(lines) if "Compilation started at" in line), -1)
    above = lines[log_row - 1] if log_row > 0 else ""
    check("the log draws no tab strip above it",
          log_row > 0 and "Compile" not in above and "notes.txt" not in above,
          f"the row above the log is {above!r}")
    check("the file's strip lists the file, not the log",
          any("notes.txt" in line and "the file the reader" not in line for line in lines)
          and not any(
              "Compile" in line for line in lines
              if "notes.txt" in line and "the file the reader" not in line),
          "the log has a tab of its own beside the file's")

    # 2. A build that fails: the log carries the compiler's line and the exit, and
    #    :cnext opens the file it named with the caret on the finding.
    text = session(command("compile") + b"sh ./build.sh" + ENTER,
                   phases=[(1.5, command("cnext"))]).text()
    check("a failing build says so",
          "Compilation exited abnormally with code 1" in text)
    check("the finding is in the log", "use of undeclared identifier" in text)
    # The jump reports itself on the toast row ("broken.cpp:2"). The log holds
    # the compiler's line, which also starts with that text, so the report has to
    # be the row that carries nothing else: a finding row would bring the
    # message after it along.
    check("cnext names the file and line",
          any("broken.cpp:2" in line and "error" not in line and "warning" not in line
              for line in text.splitlines()),
          "no 'broken.cpp:2' report after :cnext")
    check("cnext lands in the source", "return missing;" in text,
          "the file the build named is not on screen")

    # 3. The prompt re-offers the last command, and Esc leaves the buffer alone.
    seen = {"prefill": False}

    def prompt_prefill(screen) -> bool:
        if "Compile command: true" in screen.text():
            seen["prefill"] = True
            return True
        return False

    text = session(command("compile") + b"true" + ENTER,
                   phases=[(1.5, command("compile")), (0.4, prompt_prefill), (0.3, ESC)],
                   after=2.0).text()
    check("the build ran", "Compilation finished at" in text)
    check("the prompt re-offers the last command", seen["prefill"],
          "the prompt came up empty instead of with 'true'")
    check("Esc takes the prompt away", "Compile command:" not in text,
          "the prompt is still on screen")

    # 4. The job's terminal: the same prompt runs a program, and stdin answers
    #    it. Both are what the pty is for -- `test -t 1` fails under a pipe, and
    #    a `read` with nothing to read would sit there until the probe gave up.
    text = session(command("compile")
                   + b"if test -t 1; then echo IS-A-TTY; fi; "
                     b"if read line; then echo read:$line; else echo STDIN-EOF; fi"
                   + ENTER).text()
    check("the job sees a terminal", "IS-A-TTY" in text,
          "the command's stdout is not a tty")
    check("stdin answers with end-of-input", "STDIN-EOF" in text,
          "the job is waiting on a stdin nobody can type into")
    check("the job still ends", "Compilation finished at" in text)

    # 5. Colour: red (SGR 31), a palette entry (38;5;196) and bold green (1;32),
    #    plus an exact 24-bit colour the terminal resolves itself. The words are
    #    on screen as text; the colour is in the escape stream, as the palette
    #    entry (or the exact value) the command named -- not as the sequence
    #    printed into the buffer.
    screen = session(command("compile")
                     + b"printf '\\033[31mred\\033[0m \\033[38;5;196mexact\\033[0m "
                       b"\\033[1;32mgreen\\033[0m \\033[38;2;255;64;0mtrue\\033[0m\\n'"
                     + ENTER)
    text = screen.text()
    check("the coloured words are in the log", "red exact green true" in text,
          "the command's own words are not on screen")
    # The row the command printed, not the row the log echoed the command on:
    # the echo carries the sequences as the reader typed them, so only the
    # output row can say whether they leaked into the text or were read.
    log_row = next((line for line in text.splitlines() if "red exact green true" in line), "")
    check("the sequences are not printed as text",
          log_row != "" and "[31m" not in log_row and "38;5;196m" not in log_row
          and "[0m" not in log_row,
          f"the output row carries the sequences: {log_row!r}")
    check("ANSI red is painted as palette entry 1", b"\x1b[38;5;1m" in screen.raw,
          "no 38;5;1 in the editor's escape stream")
    check("the palette entry is painted as itself", b"\x1b[38;5;196m" in screen.raw,
          "no 38;5;196 in the editor's escape stream")
    check("bold green is painted bright", b"\x1b[38;5;10m" in screen.raw,
          "no 38;5;10 in the editor's escape stream")
    check("an exact colour is painted exactly", b"\x1b[38;2;255;64;0m" in screen.raw,
          "no 38;2;255;64;0 in the editor's escape stream")

    if dump:
        print(text)

    if failures:
        print(f"compile prompt probe: FAIL ({failures})")
        return 1
    print("compile prompt probe: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
