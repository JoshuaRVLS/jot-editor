#!/usr/bin/env python3
"""UI hierarchy regression: exit 0 pass, 1 fail, 2 missing binary."""
import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pty_screen import run_in_pty


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "build/apps/jot/jot"
    if not os.path.isfile(binary):
        print("UI hierarchy: SKIP - missing binary")
        return 2
    results = []
    with tempfile.TemporaryDirectory(prefix="jot_ui_hierarchy_") as base:
        root = os.path.join(base, "workspace")
        os.makedirs(root)
        source = os.path.join(root, "notes.txt")
        with open(source, "w") as file:
            file.write("READABLE_CODE_AREA_0123456789\n")
        for width in (48, 80):
            cfg = os.path.join(base, "cfg" + str(width))
            os.makedirs(os.path.join(cfg, "configs", "colors"))
            shutil.copyfile(".configs/configs/colors/jot-dark.json",
                            os.path.join(cfg, "configs", "colors", "jot-dark.json"))
            with open(os.path.join(cfg, "configs", "settings.conf"), "w") as file:
                file.write("lsp_enabled=false\nstatus_clock=false\nstatus_coding_time=false\n")
            with open(os.path.join(cfg, "init.lua"), "w") as file:
                file.write('jot.status.register("audit_tool", {side="right", priority=60, text=function() return " TOOLING_READY_123456789 " end})\n'
                           'jot.status.register("audit_social", {side="left", priority=10, text=function() return " SOCIAL_SECONDARY_123456789 " end})\n')
            palette = b"\x1b[112;6u"
            screen = run_in_pty(binary, [root], b"", settle=2, after=.2,
                                cols=width, rows=24, cfg=cfg, cwd=root,
                                phases=[(.2, palette), (.2, b"open " + source.encode()),
                                        (.2, b"\r"), (1, lambda s: "READABLE_CODE" in s.text())],
                                until_timeout=5)
            text = screen.text()
            lines = text.split("\n")
            if width == 48:
                results.append(("48 columns preserve code", "READABLE_CODE_AREA_0123456789" in text))
                results.append(("48 columns hide sidebar", not any("│" in line[:26] for line in lines[2:-1])))
            else:
                results.append(("80 columns retain sidebar", any("│" in line[:26] for line in lines[2:-1])))
                results.append(("tooling outranks social chrome", "TOOLING_READY" in lines[-1]
                                and "SOCIAL_SECONDARY" not in lines[-1]))
                # The explorer owns row 0 now: the strip spans only the pane area
                # between the side columns, so the EXPLORER section header sits
                # on the row the tabs are on instead of being covered by the
                # strip's band (which used to start at column 0), and the
                # pinned root row under it names the workspace.
                results.append(("explorer keeps its header on row 0",
                                "EXPLORER" in lines[0].split("│")[0]))
                results.append(("explorer root sits under the header",
                                "workspace" in lines[1].split("│")[0]))
                # The strip paints only its tabs: the selected one wears the
                # editor's own surface, and past its close cell the row is the
                # editor background, not a full-width TabLine band.
                text0 = "".join(screen.cells[0])
                close_x = text0.rfind("×")
                tab_bg = 1000 + 0x14100B
                band_bg = 1000 + 0x1C1710
                results.append(("selected tab wears the editor surface",
                                close_x >= 0 and screen.bg[0][close_x] == tab_bg))
                results.append(("no TabLine band right of the last tab",
                                close_x >= 0 and band_bg not in screen.bg[0][close_x + 1:]))
                # The explorer's right edge is the separator between the side
                # column and the pane; it is inked with the theme's chrome
                # border colour (SidebarBorder), not left at the background.
                results.append(("separator uses visible ink", any(1000 + 0x3A3020 in row for row in screen.fg)))
            if "--dump" in sys.argv:
                print(text)
    for label, passed in results:
        print("UI hierarchy: %s - %s" % ("PASS" if passed else "FAIL", label))
    return 0 if all(passed for _, passed in results) else 1


if __name__ == "__main__":
    sys.exit(main())
