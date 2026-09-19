#!/usr/bin/env python3
"""Probe: a vendored LSP payload installs with no network, and serves.

A release ships clangd under share/jot/payload/clangd (see
runtime/lua/lsp/managers/payload.lua). This stages exactly that layout beside a
copy of the built binary -- bin/jot + share/jot/payload/clangd/<version>/... --
points the lsp data dir at a scratch XDG_DATA_HOME, and drives the real binary:

  * `jot.lsp.install("cpp")` (the same call :lspinstall makes) links the shipped
    binary into <data>/jot/lsp/bin and writes the package receipt;
  * the link target is inside the staged payload tree, which is how this probe
    knows the bundled copy was used rather than a download;
  * a second run opens a C++ file with clangd removed from $PATH, so the only
    clangd left to serve it is the one linked out of the payload.

jot is modeless, so there is no `:` line to type at; the first run seeds the
install through init.lua, which is also how a user could ask for it.

The payload binary is a symlink to the system clangd when one is installed (the
shape a real package ships); with none, a stub stands in and only the install
assertions run.

Usage: test/bundled_lsp_payload_probe.py [path-to-jot-binary]
Exit codes: 0 pass, 1 fail, 2 binary missing.
"""
from __future__ import annotations

import glob
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty  # noqa: E402

STAGE = "/tmp/jot_payload_probe_stage"
DATA = "/tmp/jot_payload_probe_data"
WORK = "/tmp/jot_payload_probe_work"
CFG_INSTALL = "/tmp/jot_payload_probe_cfg_install"
CFG_SERVE = "/tmp/jot_payload_probe_cfg_serve"
EMPTY_PATH = "/tmp/jot_payload_probe_empty_path"
VERSION_DIR = "clangd_22.1.8"


def stage_layout(binary):
    for path in (STAGE, DATA, WORK, CFG_INSTALL, CFG_SERVE, EMPTY_PATH):
        shutil.rmtree(path, ignore_errors=True)
    os.makedirs(os.path.join(STAGE, "bin"))
    os.makedirs(os.path.join(STAGE, "share", "jot", "payload", "clangd", VERSION_DIR, "bin"))
    os.makedirs(WORK)
    os.makedirs(EMPTY_PATH)
    staged_binary = os.path.join(STAGE, "bin", "jot")
    shutil.copy2(binary, staged_binary)
    with open(os.path.join(WORK, "probe.cpp"), "w") as fh:
        fh.write("int main() { int unused = 1; return 0; }\n")
    # The install trigger: the same entry point :lspinstall cpp takes.
    os.makedirs(CFG_INSTALL)
    with open(os.path.join(CFG_INSTALL, "init.lua"), "w") as fh:
        fh.write('jot.autocmd("EditorEnter", function() jot.lsp.install("cpp") end)\n')

    payload_bin = os.path.join(STAGE, "share", "jot", "payload", "clangd",
                               VERSION_DIR, "bin", "clangd")
    system_clangd = shutil.which("clangd")
    if system_clangd:
        os.symlink(os.path.realpath(system_clangd), payload_bin)
    else:
        with open(payload_bin, "w") as fh:
            fh.write("#!/bin/sh\nexit 1\n")
        os.chmod(payload_bin, 0o755)
    return staged_binary, bool(system_clangd)


def main() -> int:
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.exists(binary):
        print(f"bundled lsp payload probe: SKIP - no binary at {binary}")
        return 2

    staged_binary, have_clangd = stage_layout(os.path.abspath(binary))
    os.environ["XDG_DATA_HOME"] = DATA
    os.environ.pop("JOT_LSP_PAYLOAD_DIR", None)

    failures = 0

    def check(name, ok, detail):
        nonlocal failures
        if not ok:
            failures += 1
        print(f"bundled lsp payload probe: {name} {'ok' if ok else 'FAIL'} - {detail}")

    # Run 1: install the bundled copy. No JOT_LSP_PAYLOAD_DIR: the binary must
    # find the payload through its own location (bin/jot -> ../share/jot).
    screen = run_in_pty(staged_binary, [os.path.join(WORK, "probe.cpp")], b"",
                        settle=3.0, after=4.0, cfg=CFG_INSTALL, cwd=WORK)
    text = screen.text()

    link = os.path.join(DATA, "jot", "lsp", "bin", "clangd")
    receipt = os.path.join(DATA, "jot", "lsp", "cpp", "receipt")
    payload_root = os.path.join(STAGE, "share", "jot", "payload", "clangd")
    check("managed bin linked", os.path.islink(link), link)
    # One hop: the managed link points at the payload's own binary. realpath()
    # would follow that link's final target (the system clangd) and hide it.
    target = os.readlink(link) if os.path.islink(link) else ""
    check("link points into the payload", target.startswith(payload_root), target or "(missing)")
    check("receipt written", os.path.isfile(receipt), receipt)

    # The background job's log carries the installer's own lifecycle markers, so
    # a script that exited before the receipt was written fails here too.
    logs = glob.glob(os.path.join(CFG_INSTALL, "logs", "install_lsp-cpp_*.log"))
    log_text = ""
    for path in logs:
        with open(path, "r", errors="replace") as fh:
            log_text += fh.read()
    check("install job reported success", "[jot:lsp] success cpp" in log_text,
          log_text.strip() or repr(text[-300:]))

    # Run 2: with clangd removed from $PATH the only thing that can serve this
    # buffer is the payload-linked binary, which is what the status segment
    # ("cpp @ ...") then proves.
    if have_clangd:
        old_path = os.environ.get("PATH", "")
        os.environ["PATH"] = EMPTY_PATH
        try:
            screen = run_in_pty(staged_binary, [os.path.join(WORK, "probe.cpp")], b"",
                                settle=4.0, after=4.0, cfg=CFG_SERVE, cwd=WORK)
        finally:
            os.environ["PATH"] = old_path
        text = screen.text()
        attached = "cpp @" in text or "clangd" in text
        check("payload clangd serves the buffer without PATH", attached,
              "status shows the server" if attached else repr(text[-300:]))
    else:
        print("bundled lsp payload probe: no system clangd - LSP attach check skipped")

    print("bundled lsp payload probe: " + ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
