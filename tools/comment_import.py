#!/usr/bin/env python3
"""Generate src/jot/integrations/comment_style_data.h.

The per-extension comment style table is derived from comment.nvim's
ft.lua (https://github.com/numtostr/comment.nvim, MIT) — the de-facto
authoritative language -> commentstring map — combined with the language
extensions declared in runtime/lua/treesitter/registry.lua, so every
language jot can highlight also gets the right comment markers.

Usage:
  python3 tools/comment_import.py [path/to/Comment/ft.lua]

Reads the local clone of comment.nvim by default
(/tmp/comment.nvim/lua/Comment/ft.lua) and always reads the registry from
the repository root.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "runtime/lua/treesitter/registry.lua"
OUT = ROOT / "src/jot/integrations/comment_style_data.h"
FT_LUA = Path(sys.argv[1] if len(sys.argv) > 1 else "/tmp/comment.nvim/lua/Comment/ft.lua")

# Registry language id -> comment.nvim filetype (ids differ for a few).
ALIASES = {
    "c_sharp": "cs",
    "tsx": "typescriptreact",
    "json": "jsonc",          # .jsonc lives under our json entry
    "latex": "tex",
    "zsh": "sh",
}

# Languages comment.nvim does not cover (or covers wrongly for our ids).
# Each value is (line_form, block_form_or_None); "%s" is the placeholder.
EXTRAS = {
    "ada": ("--%s", None),
    "caddy": ("#%s", None),
    "clojure": (";%s", None),
    "commonlisp": (";%s", "#|%s|#"),
    "crystal": ("#%s", "=begin%s=end"),
    "desktop": ("#%s", None),
    "dockerfile": ("#%s", None),
    "earthfile": ("#%s", None),
    "erlang": ("%%%s", None),
    "foam": ("//%s", None),
    "fortran": ("!%s", None),
    "git_config": ("#%s", None),
    "gitattributes": ("#%s", None),
    "gitcommit": ("#%s", None),
    "inko": ("#%s", None),
    "janet_simple": ("#%s", None),
    "jjdescription": ("#%s", None),
    "kcl": ("#%s", None),
    "kitty": ("#%s", None),
    "liquidsoap": ("#%s", None),
    "llvm": (";%s", None),
    "matlab": ("%%%s", None),
    "nginx": ("#%s", None),
    "nickel": ("#%s", None),
    "ocamllex": ("(*%s*)", None),
    "pascal": ("//%s", "{%s}"),
    "perl": ("#%s", None),
    "powershell": ("#%s", "<#%s#>"),
    "properties": ("#%s", None),
    "pymanifest": ("#%s", None),
    "qmldir": ("#%s", None),
    "racket": (";%s", "#|%s|#"),
    "razor": ("@*%s*@", None),
    "rbs": ("#%s", None),
    "requirements": ("#%s", None),
    "rst": (".. %s", None),
    "snakemake": ("#%s", None),
    "sparql": ("#%s", None),
    "sproto": ("#%s", None),
    "starlark": ("#%s", None),
    "tcl": ("#%s", None),
    "tera": ("{#%s#}", None),
    "tlaplus": ("\\*%s", None),
    "typoscript": ("#%s", None),
    "udev": ("#%s", None),
    "unison": ("--%s", None),
    "usd": ("#%s", None),
    "vento": ("<!--%s-->", None),
    "vimdoc": ('"%s', None),
    "wxml": ("<!--%s-->", None),
    "xcompose": ("#%s", None),
    "xresources": ("!%s", None),
}

# Languages with no comment syntax: toggling is a no-op.
NO_COMMENT = {"csv", "tsv", "query", "regex", "diff", "gosum"}


def parse_ft_lua(text):
    """Return {filetype: (line_form, block_form_or_None)} from ft.lua."""
    m_common = {}
    for name, tmpl in re.findall(r"(\w+)\s*=\s*'([^']*)'", text):
        m_common[name] = tmpl

    def resolve(value):
        m = re.match(r"M\.(\w+)", value)
        if m:
            return m_common[m.group(1)]
        q = re.match(r"'([^']*)'", value)
        if q:
            return q.group(1)
        return None

    out = {}
    # L table entries:  name = { M.x, 'y' },   (metatable __index follows)
    table_body = re.search(r"setmetatable\(\{(.*?)\}, \{", text, re.S).group(1)
    for name, body in re.findall(r"(\w+)\s*=\s*\{(.*?)\},", table_body, re.S):
        forms = [resolve(v) for v in re.findall(r"M\.\w+|\'[^\']*\'", body)]
        forms = [f for f in forms if f is not None]
        if not forms:
            continue
        line = forms[0]
        block = forms[1] if len(forms) > 1 and forms[1] != line else None
        out[name] = (line, block)
    return out


def split_form(tmpl):
    """Split '//%s' / '/*%s*/' / '<!--%s-->' into (before, after)."""
    idx = tmpl.find("%s")
    if idx < 0:
        return (tmpl, "")
    return (tmpl[:idx], tmpl[idx + 2 :])


def parse_registry():
    """Return {language_id: [extensions]} from registry.lua."""
    src = REGISTRY.read_text()
    out = {}
    for name, exts in re.findall(
        r'\{"([a-z0-9_]+)", "https[^"]*", \{(.*?)\}(?:, "[^"]*")?\},', src, re.S
    ):
        # Keep only real file extensions: aliases ("jsx", ...) have no dot.
        out[name] = [e for e in re.findall(r'"([^"]+)"', exts) if e.startswith(".")]
    return out


def main():
    if not FT_LUA.exists():
        sys.exit(f"comment.nvim ft.lua not found at {FT_LUA} (clone it or pass a path)")
    ft = parse_ft_lua(FT_LUA.read_text())
    registry = parse_registry()

    # language id -> (prefix, suffix, block_open, block_close)
    styles = {}
    for lang in registry:
        if lang in NO_COMMENT:
            styles[lang] = ("", "", "", "")
            continue
        entry = None
        if lang in EXTRAS:
            entry = EXTRAS[lang]
        else:
            key = ALIASES.get(lang, lang)
            if key in ft:
                entry = ft[key]
        if not entry:
            continue  # unknown: runtime fallback buckets apply
        line, block = entry
        prefix, suffix = split_form(line)
        if block:
            bopen, bclose = split_form(block)
        elif line == block if False else (suffix != ""):
            # Single-form languages (html, css): the line form is itself a
            # whole wrap, so the block form is the same pair.
            bopen, bclose = prefix, suffix
        else:
            bopen, bclose = "", ""
        styles[lang] = (prefix, suffix, bopen, bclose)

    rows = []
    for lang, (prefix, suffix, bopen, bclose) in sorted(styles.items()):
        for ext in sorted(registry[lang]):
            rows.append((ext, prefix, suffix, bopen, bclose))
    rows.sort(key=lambda r: r[0])

    lines = [
        "// Generated by tools/comment_import.py from comment.nvim's ft.lua",
        "// (https://github.com/numtostr/comment.nvim, MIT) plus the language",
        "// extensions in runtime/lua/treesitter/registry.lua - do not edit by hand.",
        "#pragma once",
        "#include <string_view>",
        "struct CommentStyleSpec",
        "{",
        "  std::string_view ext;",
        "  std::string_view line_prefix;",
        "  std::string_view line_suffix;",
        "  std::string_view block_open;",
        "  std::string_view block_close;",
        "};",
        "inline constexpr CommentStyleSpec kCommentStyles[] = {",
    ]
    def esc(s: str) -> str:
        return s.replace("\\", "\\\\").replace('"', '\\"')

    for ext, prefix, suffix, bopen, bclose in rows:
        lines.append(
            '  {"%s", "%s", "%s", "%s", "%s"},'
            % tuple(esc(s) for s in (ext, prefix, suffix, bopen, bclose))
        )
    lines.append("};")
    lines.append("inline constexpr size_t kCommentStyleCount = %d;" % len(rows))
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({len(rows)} extension entries, {len(styles)} languages)")


if __name__ == "__main__":
    main()