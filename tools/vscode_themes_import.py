#!/usr/bin/env python3
"""Import VSCode color themes as jot color schemes.

Usage:  python3 tools/vscode_themes_import.py [dir-of-vscode-themes] [output-dir]

Reads VSCode theme JSONs (colors + tokenColors, hex values) from the given
directory (default: ~/VSCode-Ultimate-Themes-Pack/multi-bg-extension/themes)
and writes one self-contained jot theme per input file into
.configs/configs/colors/<name>.json.

The output directory defaults to jot's bundled set. jot ships only its own two
schemes (jot-dark, jot-light) and never overwrites them, so pass your own
config directory -- `~/.config/jot/configs/colors` -- to keep imports personal
instead of adding them to the tree.

jot themes are flat maps of highlight group -> {fg, bg} where fg/bg are ANSI
256 palette indices (-1 = unset). VSCode themes are converted in two steps:

  - tokenColors scopes (TextMate) are matched against each jot syntax group
    with longest-scope-prefix priority; later rules win on ties, mirroring
    VSCode's own semantics.
  - editor UI colors (editor.background, statusBar.background, ...) feed the
    chrome groups (status line, tabs, sidebar, popups, diagnostics). Groups
    the source theme does not define are derived from the editor foreground /
    background by blending, light-aware, so every theme stays self-contained.

The source pack occasionally ships malformed JSON (JSONC comments, trailing
commas, a stray second document). Those are repaired heuristically; files
that still fail to parse are skipped with a warning.
"""

import glob
import json
import os
import re
import sys

THEME_DIR_DEFAULT = os.path.expanduser(
    "~/VSCode-Ultimate-Themes-Pack/multi-bg-extension/themes")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, ".configs", "configs", "colors")

# ---------------------------------------------------------------------------
# tolerant JSON loading (VSCode packs ship JSONC-ish files)
# ---------------------------------------------------------------------------

def _strip_jsonc(text):
    """Remove // comments and trailing commas, keeping string literals intact
    (the $schema URL legitimately contains "//")."""
    out = []
    i, n = 0, len(text)
    in_str = False
    esc = False
    while i < n:
        ch = text[i]
        if in_str:
            out.append(ch)
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            i += 1
            continue
        if ch == '"':
            in_str = True
            out.append(ch)
        elif ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        else:
            out.append(ch)
        i += 1
    text = "".join(out)
    # trailing commas before } or ]
    text = re.sub(r",(\s*[}\x5d])", r"\1", text)
    return text


def _first_object(text):
    """Return the first balanced {...} object in text (nebula-storm ships a
    second, stray document after the theme object)."""
    depth = 0
    start = None
    in_str = False
    esc = False
    for i, ch in enumerate(text):
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            continue
        if ch == '"':
            in_str = True
        elif ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                return text[start:i + 1]
    return text


def load_theme(path):
    raw = open(path, encoding="utf-8", errors="replace").read()
    text = _first_object(_strip_jsonc(raw))
    return json.loads(text)


# ---------------------------------------------------------------------------
# color conversion
# ---------------------------------------------------------------------------

def hex_to_rgb(value):
    if not isinstance(value, str):
        return None
    v = value.strip()
    if v.startswith("#"):
        v = v[1:]
    if len(v) == 3:
        v = "".join(c * 2 for c in v)
    if len(v) >= 8:
        v = v[:6]  # drop alpha channel (#rrggbbaa)
    if len(v) != 6:
        return None
    try:
        return tuple(int(v[i:i + 2], 16) for i in (0, 2, 4))
    except ValueError:
        return None


def rgb_to_ansi256(r, g, b):
    """Nearest ANSI 256 index for an (r, g, b) tuple (standard xterm cube)."""
    def dist(c):
        return (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2

    basic = [
        (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0),
        (0, 0, 128), (128, 0, 128), (0, 128, 128), (192, 192, 192),
        (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0),
        (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
    ]
    best = min(range(16), key=lambda i: dist(basic[i]))
    best_d = dist(basic[best])

    cube_best, cube_d = 16, None
    for r6 in range(6):
        for g6 in range(6):
            for b6 in range(6):
                c = (55 + r6 * 40 if r6 else 0,
                     55 + g6 * 40 if g6 else 0,
                     55 + b6 * 40 if b6 else 0)
                d = dist(c)
                if cube_d is None or d < cube_d:
                    cube_d, cube_best = d, 16 + 36 * r6 + 6 * g6 + b6

    gray_best, gray_d = 232, None
    for i in range(24):
        v = 8 + i * 10
        d = dist((v, v, v))
        if gray_d is None or d < gray_d:
            gray_d, gray_best = d, 232 + i

    if best_d <= (cube_d or 1e9) and best_d <= (gray_d or 1e9):
        return best
    if (cube_d or 1e9) <= (gray_d or 1e9):
        return cube_best
    return gray_best


def ansi(value, default=-1):
    if value is None:
        return default
    return max(0, min(255, value))


# ---------------------------------------------------------------------------
# group mapping tables
# ---------------------------------------------------------------------------

# Each syntax group lists candidate TextMate scopes, most specific first.
# The first scope with a resolved color wins; otherwise the group falls back
# to a derivation of the editor foreground.
SYNTAX_SCOPES = {
    "Comment": ["comment"],
    "Keyword": ["keyword.control", "keyword"],
    "keyword.control": ["keyword.control", "keyword"],
    "keyword.storage": ["storage", "keyword.storage"],
    "keyword.directive": ["keyword.directive", "meta.preprocessor"],
    "String": ["string.quoted", "string"],
    "string.escape": ["constant.character.escape", "string.escape"],
    "Number": ["constant.numeric", "constant.other.number"],
    "Function": ["entity.name.function", "support.function"],
    "function.method": ["entity.name.function.method", "meta.method-call.entity.name.function"],
    "function.constructor": ["entity.name.class", "function.constructor", "entity.name.type.class"],
    "Type": ["entity.name.type", "storage.type", "support.type"],
    "type.builtin": ["support.type", "entity.name.type.builtin"],
    "variable": ["variable.other", "variable"],
    "parameter": ["variable.parameter", "variable.other.parameter"],
    "property": ["variable.object.property", "support.variable.property"],
    "constant": ["constant.other", "constant"],
    "constant_macro": ["constant.other.capitalized", "constant.language", "support.constant"],
    "builtin": ["support.function", "support.class", "support.type", "support.variable"],
    "operator": ["keyword.operator"],
    "tag": ["entity.name.tag"],
    "attribute": ["entity.other.attribute-name"],
    "namespace": ["entity.name.namespace", "support.namespace"],
    "module": ["entity.name.module", "support.module"],
    "punctuation": ["punctuation"],
    "punctuation.bracket": ["punctuation.section", "punctuation.definition.bracket"],
    "punctuation.delimiter": ["punctuation.separator", "punctuation.definition.separator"],
}# Chrome groups: (name, vscode colors key for fg, vscode colors key for bg,
#                 fallback fg, fallback bg). Fallbacks are functions of the
#                 resolved base palette (bg, fg) plus the light flag.
def _blend(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def _mix(bg, fg, light, t):
    """t in [0,1]; towards fg when not light (dark themes), towards bg on
    light themes so derived colors keep contrast."""
    return _blend(bg, fg, t) if not light else _blend(fg, bg, t)


CHROME = [
    # group, fg key, bg key, fg fallback fn(bg,fg,light), bg fallback fn(bg,fg,light)
    ("Normal", "editor.foreground", "editor.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("NormalFloat", "editorWidget.foreground", "editorWidget.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("LineNr", "editorLineNumber.foreground", None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.45), lambda bg, fg, l: bg),
    ("CursorLineNr", "editorLineNumber.activeForeground", None,
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.08)),
    ("Cursor", "editorCursor.foreground", None,
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("Visual", None, "editor.selectionBackground",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.25)),
    ("CursorLine", None, "editor.lineHighlightBackground",
     None, lambda bg, fg, l: _mix(bg, fg, l, 0.07)),
    ("Search", None, "editor.findMatchBackground",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.35)),
    ("CurSearch", None, "editor.findMatchHighlightBackground",
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.5)),
    ("StatusLine", "statusBar.foreground", "statusBar.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineMsg", None, None,
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineLogo", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.4), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineFile", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.4), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineInfo", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.4), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineWarn", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.4), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineError", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.4), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("StatusLineMuted", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.45), lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("FloatBorder", "editorWidget.border", None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.45), lambda bg, fg, l: bg),
    ("WinSeparator", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.45), lambda bg, fg, l: bg),
    ("WinActiveBorder", "focusBorder", None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.55), lambda bg, fg, l: bg),
    ("TabLine", "tab.inactiveForeground", "tab.inactiveBackground",
     lambda bg, fg, l: _mix(bg, fg, l, 0.5), lambda bg, fg, l: _mix(bg, fg, l, 0.06)),
    ("TabLineSel", "tab.activeForeground", "tab.activeBackground",
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.4)),
    ("TabLineFill", None, "tab.inactiveBackground",
     lambda bg, fg, l: _mix(bg, fg, l, 0.5), lambda bg, fg, l: _mix(bg, fg, l, 0.06)),
    ("TabClose", "tab.inactiveForeground", "tab.inactiveBackground",
     lambda bg, fg, l: _mix(bg, fg, l, 0.35), lambda bg, fg, l: _mix(bg, fg, l, 0.06)),
    ("Sidebar", "sideBar.foreground", "sideBar.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("SidebarDir", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.6), lambda bg, fg, l: bg),
    ("SidebarSel", None, "list.activeSelectionBackground",
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.35)),
    ("SidebarSelNC", None, "list.inactiveSelectionBackground",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.12)),
    ("SidebarBorder", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.45), lambda bg, fg, l: bg),
    ("DiagnosticError", "editorError.foreground", None,
     lambda bg, fg, l: (255, 85, 85), lambda bg, fg, l: bg),
    ("DiagnosticWarn", "editorWarning.foreground", None,
     lambda bg, fg, l: (230, 190, 90), lambda bg, fg, l: bg),
    ("DiagnosticInfo", "editorInfo.foreground", None,
     lambda bg, fg, l: (95, 175, 225), lambda bg, fg, l: bg),
    ("DiagnosticHint", "editorHint.foreground", None,
     lambda bg, fg, l: (125, 205, 155), lambda bg, fg, l: bg),
    ("Pmenu", "editorSuggestWidget.foreground", "editorSuggestWidget.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.06)),
    ("PmenuSel", "editorSuggestWidget.selectedForeground",
     "editorSuggestWidget.selectedBackground",
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.35)),
    ("TelescopeNormal", None, "editorSuggestWidget.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.06)),
    ("TelescopeSelection", None, "editorSuggestWidget.selectedBackground",
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.35)),
    ("TelescopePreviewNormal", None, "editor.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("Terminal", "terminal.foreground", "terminal.background",
     lambda bg, fg, l: fg, lambda bg, fg, l: bg),
    ("TerminalTab", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.5), lambda bg, fg, l: bg),
    ("TerminalTabActive", None, "terminal.inactiveSelectionBackground",
     lambda bg, fg, l: fg, lambda bg, fg, l: _mix(bg, fg, l, 0.1)),
    ("TerminalTabFocused", None, None,
     lambda bg, fg, l: bg, lambda bg, fg, l: _mix(bg, fg, l, 0.35)),
    ("TerminalTabClose", None, None,
     lambda bg, fg, l: (255, 85, 85), lambda bg, fg, l: bg),
    ("TerminalTabPlus", None, None,
     lambda bg, fg, l: (125, 205, 155), lambda bg, fg, l: bg),
    ("TerminalTabSeparator", None, None,
     lambda bg, fg, l: _mix(bg, fg, l, 0.5), lambda bg, fg, l: bg),
]

# Fixed semantic hues for the git gutter (theme-independent).
GIT_GROUPS = {
    "git_modified": ((215, 165, 80), None),
    "git_added": ((110, 190, 130), None),
    "git_untracked": ((110, 190, 130), None),
    "git_deleted": ((220, 90, 90), None),
    "git_renamed": ((90, 160, 215), None),
    "git_conflict": ((200, 130, 210), None),
}

# Emit order mirrors the bundled reference theme.
GROUP_ORDER = [
    "Normal", "NormalFloat", "LineNr", "Comment", "Keyword", "String",
    "Number", "Function", "Type", "keyword.control", "keyword.storage",
    "keyword.directive", "string.escape", "variable", "parameter",
    "property", "constant", "constant_macro", "builtin", "operator",
    "function.method", "function.constructor", "type.builtin", "tag",
    "attribute", "namespace", "module", "punctuation",
    "punctuation.bracket", "punctuation.delimiter", "Cursor", "Visual",
    "CursorLine", "CursorLineNr", "Search", "CurSearch", "StatusLine",
    "StatusLineMsg", "StatusLineLogo", "StatusLineFile", "StatusLineInfo",
    "StatusLineWarn", "StatusLineError", "StatusLineMuted", "FloatBorder",
    "WinSeparator", "WinActiveBorder", "TabLine", "TabLineSel",
    "TabLineFill", "TabClose", "Sidebar", "SidebarDir", "SidebarSel",
    "SidebarSelNC", "SidebarBorder", "DiagnosticError", "DiagnosticWarn",
    "DiagnosticInfo", "DiagnosticHint", "Pmenu", "PmenuSel",
    "TelescopeNormal", "TelescopeSelection", "TelescopePreviewNormal",
    "Terminal", "TerminalTab", "TerminalTabActive", "TerminalTabFocused",
    "TerminalTabClose", "TerminalTabPlus", "TerminalTabSeparator",
] + list(GIT_GROUPS.keys())


def theme_is_light(theme):
    return str(theme.get("type", "dark")).startswith("light")


def resolve_token_colors(theme):
    """scope -> rgb for every token rule; later rules win on equal specificity,
    matching VSCode's cascade."""
    resolved = {}
    for rule in theme.get("tokenColors", []):
        settings = rule.get("settings") or {}
        color = settings.get("foreground")
        rgb = hex_to_rgb(color) if color else None
        if rgb is None:
            continue
        scopes = rule.get("scope")
        if isinstance(scopes, str):
            scopes = [scopes]
        for scope in scopes or []:
            if not isinstance(scope, str):
                continue
            spec = scope.count(".") + 1
            prev_spec, prev_order = resolved.get(scope, (None, -1, -1))[1:]
            if spec > prev_spec:
                resolved[scope] = (rgb, spec, 0)
    return {k: v[0] for k, v in resolved.items()}


def scope_color(scopes, token_map):
    """First candidate scope with an exact map hit, else its longest prefix."""
    for cand in scopes:
        parts = cand.split(".")
        for i in range(len(parts), 0, -1):
            key = ".".join(parts[:i])
            if key in token_map:
                return token_map[key]
    return None


def _luminance(rgb):
    def chan(c):
        c = c / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (chan(x) for x in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def _contrast(a, b):
    la, lb = _luminance(a), _luminance(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def fix_fg_contrast(rgb, bg, anchor, min_keep=2.0, min_target=3.2):
    """Some pack themes (aurora-borealis, akari-dawn, ...) paint generic
    scopes with a color that equals the editor background, which makes whole
    classes of tokens invisible in jot (its tree-sitter uses broad scopes like
    plain 'variable'). If a resolved foreground is essentially unreadable on
    its background, blend it toward the theme's own readable foreground until
    it clears a contrast floor, preserving the author's colors everywhere
    else."""
    if rgb is None or bg is None or _contrast(rgb, bg) >= min_keep:
        return rgb
    if _contrast(anchor, bg) < min_target:
        lum = _luminance(bg)
        anchor = (15, 15, 15) if lum > 0.5 else (240, 240, 240)
    cur = list(rgb)
    for _ in range(14):
        cur = [int(a + (b - a) * 0.5) for a, b in zip(cur, anchor)]
        if _contrast(tuple(cur), bg) >= min_target:
            break
    return tuple(cur)


def convert(theme, src_name):
    light = theme_is_light(theme)
    colors = theme.get("colors") or {}
    bg = hex_to_rgb(colors.get("editor.background"))
    if bg is None:
        bg = (240, 240, 240) if light else (24, 24, 27)
    fg = hex_to_rgb(colors.get("editor.foreground"))
    if fg is None:
        fg = (40, 40, 40) if light else (220, 220, 220)

    token_map = resolve_token_colors(theme)

    def color_for(key):
        if key:
            return hex_to_rgb(colors.get(key))
        return None

    def fg_ansi(rgb):
        return rgb_to_ansi256(*rgb) if rgb else -1

    def bg_ansi(rgb):
        return rgb_to_ansi256(*rgb) if rgb else -1

    # Collect raw (fg_rgb, bg_rgb) pairs first; a contrast pass then repairs
    # any foreground the source theme made unreadable against its background
    # (generic scopes painted with the background color are common in the pack
    # and leave plain tree-sitter tokens invisible).
    raw = {}

    # syntax groups
    for group, cands in SYNTAX_SCOPES.items():
        rgb = scope_color(cands, token_map)
        if rgb is None:
            rgb = fg  # unthemed scope: default text, never washed towards bg
        raw[group] = (rgb, bg)

    # chrome groups
    for group, fg_key, bg_key, fg_fb, bg_fb in CHROME:
        rgb_fg = color_for(fg_key)
        rgb_bg = color_for(bg_key)
        if rgb_fg is None and fg_fb is not None:
            rgb_fg = fg_fb(bg, fg, light)
        if rgb_bg is None and bg_fb is not None:
            rgb_bg = bg_fb(bg, fg, light)
        raw[group] = (rgb_fg, rgb_bg)

    # git gutter
    for group, (rgb_fg, rgb_bg) in GIT_GROUPS.items():
        raw[group] = (rgb_fg, rgb_bg)

    # The block cursor inverts the default pair (dark block, light glyph on
    # dark themes; light block, dark glyph on light themes) like the bundled
    # schemes, so it never disappears into the editor background.
    raw["Cursor"] = (bg, fg)

    out = {}
    for group, (rgb_fg, rgb_bg) in raw.items():
        # contrast is judged against the group's own background; groups that
        # carry none fall back to the editor background.
        ref_bg = rgb_bg if rgb_bg is not None else bg
        rgb_fg = fix_fg_contrast(rgb_fg, ref_bg, fg)
        out[group] = {"fg": fg_ansi(rgb_fg), "bg": bg_ansi(rgb_bg)}
    return out


def output_name(path):
    name = os.path.splitext(os.path.basename(path))[0]
    name = re.sub(r"-theme$", "", name)
    name = re.sub(r"^vscode-", "", name)
    return name or "unnamed"


# jot's own themes: hand-maintained, and never replaced by an import whose
# name happens to collide. Everything else in the output directory is fair game.
BUNDLED_THEMES = {
    "jot-dark", "jot-light",
}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else THEME_DIR_DEFAULT
    out_dir = sys.argv[2] if len(sys.argv) > 2 else OUT_DIR
    if not os.path.isdir(src):
        print("theme directory not found:", src)
        return 1
    os.makedirs(out_dir, exist_ok=True)

    existing = set(BUNDLED_THEMES)
    written, skipped = 0, []
    for path in sorted(glob.glob(os.path.join(src, "*.json"))):
        name = output_name(path)
        if name in existing:
            skipped.append((name, "collides with bundled theme"))
            continue
        try:
            theme = load_theme(path)
            groups = convert(theme, name)
        except Exception as exc:  # noqa: BLE001 - report and keep going
            skipped.append((name, str(exc)))
            continue
        body = ",\n".join(
            "  \"%s\": {\"fg\": %d, \"bg\": %d}" % (g, groups[g]["fg"], groups[g]["bg"])
            for g in GROUP_ORDER)
        with open(os.path.join(out_dir, name + ".json"), "w") as f:
            f.write("{\n%s\n}\n" % body)
        written += 1
        existing.add(name)

    print("wrote %d themes to %s" % (written, out_dir))
    if skipped:
        print("skipped %d:" % len(skipped))
        for name, why in skipped:
            print("  %-45s %s" % (name, why))
    return 0


if __name__ == "__main__":
    sys.exit(main())