#!/usr/bin/env python3
"""Import the mason-registry package catalog into jot's LSP installer.

Usage:  python3 tools/mason_import.py [path-to-mason-registry]

Reads the YAML package specs from a checkout of https://github.com/mason-org/mason-registry
(packages/<name>/package.yaml) and regenerates two committed derived files:

  - src/lua/lsp/registry.lua       the installer's data-driven package catalog
  - src/core/integrations/lsp_attach_data.h   best-effort file->server attach table

To refresh after upstream moves: pull a fresh mason-registry checkout and
rerun this script.

Canonical ids jot has shipped (python, typescript, cpp, ...) are overlaid on
the matching mason package so install behaviour and aliases stay stable while
the recipe tracks upstream.
"""

import os
import re
import sys

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REGISTRY_OUT = os.path.join(ROOT, "src", "lua", "lsp", "registry.lua")
ATTACH_OUT = os.path.join(ROOT, "src", "core", "integrations", "lsp_attach_data.h")

# Canonical ids -> mason package whose recipe they adopt. The mason row is
# dropped from the listing (no double receipts) and the canonical id inherits
# its source/bin data.
CANONICAL = {
    "python": "python-lsp-server",
    "pyright": "pyright",
    "typescript": "typescript-language-server",
    "cpp": "clangd",
    "rust": "rust-analyzer",
    "go": "gopls",
    "lua": "lua-language-server",
    "bash": "bash-language-server",
    "html": "html-lsp",
    "css": "css-lsp",
    "json": "json-lsp",
    "yaml": "yaml-language-server",
    "vue": "vue-language-server",
    "php": "intelephense",
}

# jot-only servers with no mason equivalent. Their recipes stay hand-written.
JOT_ONLY = {
    "dockerfile": {
        "display": "Dockerfile",
        "detail": "dockerfile-language-server-nodejs (docker-langserver)",
        "languages": ["Dockerfile"],
        "manager": "npm",
        "pkg": "dockerfile-language-server-nodejs",
        "bins": ["docker-langserver"],
        "win_cmd": "npm install -g dockerfile-language-server-nodejs",
        "win_remove_cmd": "npm uninstall -g dockerfile-language-server-nodejs",
    },
    "sql": {
        "display": "SQL",
        "detail": "sql-language-server (sqls)",
        "languages": ["SQL"],
        "manager": "npm",
        "pkg": "sql-language-server",
        "bins": ["sql-language-server"],
        "win_cmd": "npm install -g sql-language-server",
        "win_remove_cmd": "npm uninstall -g sql-language-server",
    },
    "markdown": {
        "display": "Markdown",
        "detail": "markdown-language-features (markdown-language-server)",
        "languages": ["Markdown"],
        "manager": "npm",
        "pkg": "markdown-language-features",
        "bins": ["markdown-language-server"],
        "win_cmd": "npm install -g markdown-language-features",
        "win_remove_cmd": "npm uninstall -g markdown-language-features",
    },
}

# Legacy alias groups kept from the pre-import registry.
ALIASES = {
    "python": ["py", "pylsp"],
    "pyright": [],
    "typescript": ["javascript", "js", "jsx", "ts", "tsx", "mts", "cts", "ts-server"],
    "cpp": ["c", "c++", "clangd"],
    "rust": ["rs", "rust-analyzer"],
    "go": ["golang", "gopls"],
    "lua": ["lua_ls", "luals"],
    "bash": ["sh", "shell", "bashls", "bash-language-server"],
    "html": ["htm", "html-language-server"],
    "css": ["css-language-server"],
    "json": ["json-language-server"],
    "yaml": ["yml", "yaml-language-server"],
    "vue": ["vue-language-server"],
    "php": ["intelephense"],
    "sql": ["sqls"],
    "markdown": ["md"],
    "dockerfile": ["docker"],
}

# Well-known launch args for servers jot can auto-attach to. Native servers
# (pyright/typescript/.../sql/php) plus catalog servers whose binary needs a
# subcommand or --stdio flag to act as a stdio server. Others launch bare
# (best effort; most LSP binaries default to stdio).
KNOWN_ARGS = {
    "pyright": "--stdio",
    "typescript": "--stdio",
    "html": "--stdio",
    "css": "--stdio",
    "json": "--stdio",
    "yaml": "--stdio",
    "dockerfile": "--stdio",
    "vue": "--stdio",
    "markdown": "--stdio",
    "sql": "up --method stream",
    "php": "--stdio",
    # Catalog servers with a non-default stdio contract.
    "terraform-ls": "serve",
    "taplo": "lsp stdio",
    "solargraph": "stdio",
    "svelte-language-server": "--stdio",
    "vim-language-server": "--stdio",
    "visualforce-language-server": "--stdio",
    "graphql-language-service-cli": "server --method=stream",
    "astro-language-server": "--stdio",
}

# Curated default LSP per language name. When a language is claimed by several
# LSP packages this entry decides which one auto-attaches (mason/lspconfig do
# the same job for nvim; this is jot's equivalent).
LANG_DEFAULT = {
    "Bash": "bash-language-server", "Sh": "bash-language-server", "Zsh": "bash-language-server",
    "C": "clangd", "C++": "clangd",
    "Rust": "rust-analyzer", "Go": "gopls", "Lua": "lua-language-server",
    "Python": "basedpyright",
    "TypeScript": "typescript-language-server", "JavaScript": "typescript-language-server",
    "HTML": "html-lsp", "CSS": "css-lsp", "SCSS": "css-lsp", "Less": "css-lsp",
    "JSON": "json-lsp", "JSONC": "json-lsp",
    "YAML": "yaml-language-server",
    "Vue": "vue-language-server",
    "SQL": "sqls", "PHP": "intelephense", "Markdown": "marksman",
    "Dockerfile": "dockerfile-language-server-nodejs",
    "Terraform": "terraform-ls", "HCL": "terraform-ls",
    "Solidity": "solc", "GraphQL": "graphql-language-service-cli",
    "Kotlin": "kotlin-language-server", "Java": "jdtls",
    "Ruby": "solargraph", "Elixir": "elixir-ls", "Erlang": "erlang_ls",
    "Haskell": "haskell-language-server", "OCaml": "ocaml-lsp",
    "F#": "fsautocomplete", "C#": "omnisharp",
    "Clojure": "clojure-lsp", "Scheme": "scheme-langserver",
    "Racket": "racket-langserver", "Common Lisp": "cl-lsp",
    "Scala": "metals", "Swift": "sourcekit-lsp", "Dart": "dartls",
    "R": "r-languageserver", "Julia": "julia-lsp",
    "Perl": "perlnavigator", "Raku": "raku-navigator",
    "Zig": "zls", "Nim": "nimlsp", "V": "v-analyzer",
    "Crystal": "crystalline", "Elm": "elm-language-server",
    "Nix": "nil", "Gleam": "gleam",
    "Purescript": "purescript-language-server",
    "Fennel": "fennel-ls", "Teal": "teal-language-server",
    "Tcl": "tcl-language-server", "CMake": "cmake-language-server",
    "TOML": "taplo", "INI": "taplo", "XML": "lemminx",
    "Svelte": "svelte-language-server", "Astro": "astro-language-server",
    "Twig": "twiggy-language-server",
    "Jinja": "jinja-lsp", "Caddyfile": "caddyfile-language-server",
    "Docker": "dockerfile-language-server-nodejs",
    "Protobuf": "bufls", "Thrift": "thriftls",
    "Prisma": "prisma-language-server",
    "LaTeX": "texlab", "BibTeX": "texlab", "TeX": "texlab",
    "AsciiDoc": "asciidoc-lsp", "reStructuredText": "esbonio",
    "Text": "textlint",
    "SystemVerilog": "svls", "Verilog": "svls", "VHDL": "vhdl-ls",
    "Assembly": "asm-lsp", "NASM": "asm-lsp", "GAS": "asm-lsp",
    "Ada": "ada_language_server", "SPARK": "ada_language_server",
    "Fortran": "fortls", "Pascal": "pascal-language-server",
    "Apex": "apex-language-server", "Visualforce": "visualforce-language-server",
    "AutoHotkey": "autohotkey_lsp", "VimScript": "vim-language-server",
    "Vim": "vim-language-server", "Neovim": "vim-language-server",
    "PowerShell": "powershell-editor-services", "Ansible": "ansible-language-server",
    "AWK": "awk-language-server", "Nginx": "nginx-language-server",
    "Apache": "apache-language-server", "Haproxy": "haproxy-language-server",
    "Systemd": "systemd-language-server",
    "Meson": "mesonlsp", "Bazel": "bzl", "Starlark": "starpls",
    "Pkl": "pkl-lsp", "CUE": "cuelsp", "Jsonnet": "jsonnet-language-server",
    "Nickel": "nickel-lang-lsp", "Dhall": "dhall-lsp-server",
    "Nushell": "nu-language-server", "Fish": "fish-lsp",
    "Groovy": "groovy-language-server", "Gradle": "gradle-language-server",
    "Haxe": "haxe-language-server", "D": "serve-d",
    "Vala": "vala-language-server", "Ceylon": "ceylon",
    "Idris": "idris2-lsp", "Coq": "coq-lsp", "Lean": "lean",
    "Roc": "roc_ls", "Grain": "grain-lsp", "Motoko": "motoko-lsp",
    "Move": "move-analyzer", "Cadence": "cadence-language-server",
    "Cairo": "cairo-language-server", "Sway": "sway-lsp",
    "Smt": "z3",
    "Motoko": "motoko-lsp",
    "ActionScript": "actionscript-language-server",
    "Starlark": "starpls",
    "Python": "basedpyright",
}

# language name -> (extensions, lsp language id). Used to turn a language
# claim into file extensions for the attach table.
LANG_EXT = {
    "Bash": (["sh", "bash"], "shellscript"), "Sh": (["sh"], "shellscript"), "Zsh": (["zsh"], "shellscript"),
    "C": (["c", "h"], "c"), "C++": (["cpp", "cc", "cxx", "hpp", "hh", "hxx"], "cpp"),
    "Objective-C": (["m"], "objective-c"), "Objective-C++": (["mm"], "objective-cpp"),
    "Rust": (["rs"], "rust"), "Go": (["go"], "go"), "Lua": (["lua"], "lua"),
    "Python": (["py", "pyw", "pyi"], "python"),
    "TypeScript": (["ts", "mts", "cts"], "typescript"),
    "JavaScript": (["js", "mjs", "cjs"], "javascript"),
    "HTML": (["html", "htm"], "html"), "CSS": (["css"], "css"),
    "SCSS": (["scss"], "scss"), "Less": (["less"], "less"),
    "JSON": (["json"], "json"), "JSONC": (["jsonc"], "jsonc"),
    "YAML": (["yaml", "yml"], "yaml"), "Vue": (["vue"], "vue"),
    "SQL": (["sql"], "sql"), "PHP": (["php"], "php"),
    "Markdown": (["md", "markdown"], "markdown"),
    "Dockerfile": (["dockerfile"], "dockerfile"),
    "Terraform": (["tf", "tfvars"], "terraform"), "HCL": (["hcl"], "terraform"),
    "Solidity": (["sol"], "solidity"),
    "GraphQL": (["graphql", "gql"], "graphql"),
    "Kotlin": (["kt", "kts"], "kotlin"),
    "Java": (["java"], "java"), "Ruby": (["rb"], "ruby"),
    "Elixir": (["ex", "exs"], "elixir"), "Erlang": (["erl", "hrl"], "erlang"),
    "Haskell": (["hs"], "haskell"), "OCaml": (["ml", "mli"], "ocaml"),
    "F#": (["fs", "fsi", "fsx"], "fsharp"),
    "C#": (["cs", "csx"], "csharp"),
    "Clojure": (["clj", "cljs", "cljc", "edn"], "clojure"),
    "Scheme": (["scm", "ss"], "scheme"), "Racket": (["rkt"], "racket"),
    "Common Lisp": (["lisp", "lsp"], "commonlisp"),
    "Scala": (["scala", "sc"], "scala"), "Swift": (["swift"], "swift"),
    "Dart": (["dart"], "dart"), "R": (["r", "rmd"], "r"), "Julia": (["jl"], "julia"),
    "Perl": (["pl", "pm"], "perl"), "Raku": (["raku", "p6"], "raku"),
    "Zig": (["zig"], "zig"), "Nim": (["nim"], "nim"), "V": (["v"], "v"),
    "Crystal": (["cr"], "crystal"), "Elm": (["elm"], "elm"),
    "Nix": (["nix"], "nix"), "Gleam": (["gleam"], "gleam"),
    "Purescript": (["purs"], "purescript"), "Fennel": (["fnl"], "fennel"),
    "Teal": (["tl"], "teal"), "Tcl": (["tcl"], "tcl"),
    "CMake": (["cmake"], "cmake"),
    "TOML": (["toml"], "toml"), "INI": (["ini"], "ini"),
    "XML": (["xml", "xsl", "xsd"], "xml"), "XSLT": (["xslt"], "xslt"),
    "Svelte": (["svelte"], "svelte"), "Astro": (["astro"], "astro"),
    "Twig": (["twig"], "twig"),
    "Jinja": (["jinja", "j2"], "jinja"),
    "Caddyfile": (["caddyfile"], "caddyfile"),
    "Docker": (["dockerfile"], "dockerfile"),
    "Protobuf": (["proto"], "protobuf"), "Thrift": (["thrift"], "thrift"),
    "Prisma": (["prisma"], "prisma"),
    "LaTeX": (["tex"], "latex"), "BibTeX": (["bib"], "bibtex"), "TeX": (["tex"], "tex"),
    "AsciiDoc": (["adoc", "asciidoc"], "asciidoc"),
    "reStructuredText": (["rst"], "restructuredtext"),
    "Text": (["txt"], "plaintext"),
    "SystemVerilog": (["sv", "svh"], "systemverilog"),
    "Verilog": (["v"], "verilog"), "VHDL": (["vhd", "vhdl"], "vhdl"),
    "Assembly": (["asm", "s"], "asm"), "NASM": (["asm"], "asm"), "GAS": (["s"], "asm"),
    "Ada": (["adb", "ads"], "ada"), "SPARK": (["adb", "ads"], "ada"),
    "Fortran": (["f", "f90", "f95"], "fortran"),
    "Pascal": (["pas"], "pascal"), "ABAP": (["abap"], "abap"),
    "Apex": (["cls"], "apex"), "Visualforce": (["page"], "visualforce"),
    "AutoHotkey": (["ahk"], "ahk"),
    "VimScript": (["vim"], "vim"), "Vim": (["vim"], "vim"), "Neovim": (["vim"], "vim"),
    "PowerShell": (["ps1", "psm1"], "powershell"),
    "Ansible": (["yml", "yaml"], "ansible"),
    "AWK": (["awk"], "awk"), "Nginx": (["conf"], "nginx"),
    "Apache": (["htaccess", "conf"], "apacheconf"),
    "Haproxy": (["cfg"], "haproxy"),
    "Systemd": (["service"], "systemd"),
    "Meson": (["build"], "meson"),
    "Bazel": (["bzl"], "bazel"), "Starlark": (["bzl", "star"], "bazel"),
    "Pkl": (["pkl"], "pkl"), "CUE": (["cue"], "cue"),
    "Jsonnet": (["jsonnet", "libsonnet"], "jsonnet"),
    "Nickel": (["ncl"], "nickel"), "Dhall": (["dhall"], "dhall"),
    "Nushell": (["nu"], "nu"), "Fish": (["fish"], "fish"),
    "Groovy": (["groovy"], "groovy"), "Gradle": (["gradle"], "gradle"),
    "Haxe": (["hx"], "haxe"), "D": (["d"], "d"), "Vala": (["vala"], "vala"),
    "Ceylon": (["ceylon"], "ceylon"), "Idris": (["idr"], "idris"),
    "Coq": (["v"], "coq"), "Lean": (["lean"], "lean"),
    "Roc": (["roc"], "roc"), "Grain": (["gr"], "grain"),
    "Motoko": (["mo"], "motoko"), "Move": (["move"], "move"),
    "Cadence": (["cdc"], "cadence"), "Cairo": (["cairo"], "cairo"),
    "Sway": (["sw"], "sway"),
    "Smt": (["smt2"], "smt"),
    "ActionScript": (["as"], "actionscript"),
    "Racket": (["rkt"], "racket"),
}


def lua_str(value):
    out = str(value).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + out + '"'


def lua_str_list(items):
    return "{" + ", ".join(lua_str(str(i)) for i in items) + "}"


VER_FILL = re.compile(r"\{\{\s*version\s*(?:\|\s*strip_prefix\s*\"([^\"]*)\"\s*)?\}\}")


def parse_pkg_id(ident):
    """pkg:npm/foo@1.2.3?extra=all -> (manager, arg, version, extras)."""
    assert ident.startswith("pkg:")
    mgr, _, rest = ident[len("pkg:"):].partition("/")
    if "@" not in rest:
        return mgr, rest, "", {}
    arg, _, ver = rest.rpartition("@")
    version, _, extra_q = ver.partition("?")
    extras = {}
    if extra_q:
        for pair in extra_q.split("&"):
            k, _, v = pair.partition("=")
            extras[k] = v
    # golang packages may pin a subcommand: @v1.2.3#cmd/gopls
    if "#" in arg:
        base, _, sub = arg.partition("#")
        arg = base + "/" + sub
    return mgr, arg, version, extras


def fill_version(template, version):
    """Substitute {{version [| strip_prefix "x"]}} tokens with the pinned version."""
    def repl(m):
        v = version
        prefix = m.group(1)
        if prefix and v.startswith(prefix):
            v = v[len(prefix):]
        return v
    return VER_FILL.sub(repl, str(template))


JINJA = re.compile(r"\{\{[^}]*\}\}")


def pick_asset(src_asset, target):
    """Normalize mason's github asset shapes to a per-target dict.

    Two shapes exist: a list [{target, file, bin}, ...] (new) and a map
    {file, bin} applying to every platform (older/leaf packages).
    """
    asset_list = src_asset if isinstance(src_asset, list) else None
    if asset_list is None and isinstance(src_asset, dict):
        return src_asset
    for entry in asset_list or []:
        if not isinstance(entry, dict):
            continue
        t = entry.get("target", [])
        if isinstance(t, str):
            t = [t]
        if target in t:
            return entry
    return None


def template_regex(file_spec):
    """Turn an asset filename template into an anchored regex over names."""
    pat = file_spec
    pat = pat.replace("{{version}}", ".*").replace("{{ version }}", ".*")
    pat = JINJA.sub(".*", pat)
    return re.escape(pat).replace(r"\.\*", ".*")


def classify_bin(spec):
    """Map a mason bin spec to (kind, hint). kind is empty for plain files."""
    if not isinstance(spec, str):
        return "", ""
    spec = spec.strip()
    m = re.match(r"^(node|python|python3|php|ruby|dotnet|java-jar|pyvenv):(.*)$", spec, re.S)
    if not m:
        return "", spec
    kind = "jar" if m.group(1) == "java-jar" else m.group(1)
    return kind, m.group(2).strip().strip('"').strip()


def build_entry(name, data):
    """One package.yaml -> registry row dict (or None for canonical dupes)."""
    src = data.get("source") or {}
    if "id" not in src:
        return None
    mgr, arg, version, extras = parse_pkg_id(src["id"])
    detail = (data.get("description") or name).strip().replace("\n", " ")
    if len(detail) > 120:
        detail = detail[:117] + "..."
    row = {
        "id": name,
        "display": name,
        "detail": detail,
        "manager": mgr,
        "categories": [str(c) for c in data.get("categories", [])],
        "languages": [str(l) for l in data.get("languages", [])],
    }
    if mgr == "github":
        row["repo"] = arg
        row["asset"] = {}
        for plat, target in (("linux", "linux_x64_gnu"),
                             ("mac", "darwin_arm64"),
                             ("win", "win_x64")):
            entry = pick_asset(src.get("asset", []), target)
            if not entry:
                # Try broader linux target (musl or arm64) for coverage.
                if plat == "linux":
                    for alt in ("linux_x64_musl", "linux_arm64_gnu", "linux_arm_gnu"):
                        entry = pick_asset(src.get("asset", []), alt)
                        if entry:
                            break
                else:
                    alt = "darwin_x64" if plat == "mac" else "win_arm64"
                    entry = pick_asset(src.get("asset", []), alt)
            if not entry:
                continue
            file_spec = str(entry.get("file", "")).split(":", 1)[0]
            low = file_spec.lower()
            archive = "none"
            if low.endswith(".zip"):
                archive = "zip"
            elif low.endswith((".tar.gz", ".tgz")):
                archive = "tar.gz"
            elif low.endswith(".gz"):
                archive = "gz"
            elif low.endswith(".tar"):
                archive = "tar"
            row["asset"][plat] = {
                # Filename template -> anchored regex over release asset names
                # (version only known after the release is queried).
                "match": template_regex(file_spec),
                "archive": archive,
                "file": file_spec,
            }
        if not row["asset"]:
            return None
    elif mgr == "generic":
        raw_dl = src.get("download")
        if isinstance(raw_dl, dict):
            # Untargeted single download (applies to every platform).
            raw_dl = [raw_dl]
        chosen = {}
        for entry in raw_dl or []:
            if not isinstance(entry, dict):
                continue
            t = entry.get("target", [])
            if isinstance(t, str):
                t = [t]
            for tg in t:
                chosen.setdefault(tg, entry)
        row["dl"] = {}
        for plat, prefs in (("linux", ["linux_x64", "linux_x64_gnu", "linux_arm64", "linux"]),
                            ("mac", ["darwin_x64", "darwin_arm64", "darwin"]),
                            ("win", ["win_x64", "win"])):
            for p in prefs:
                if p in chosen:
                    files = chosen[p].get("files", {}) or {}
                    row["dl"][plat] = {
                        "files": {fill_version(k, version): fill_version(v, version)
                                  for k, v in files.items()},
                        "bin": str(chosen[p].get("bin", "") or "").strip(),
                    }
                    break
        if not row["dl"]:
            return None
    elif mgr == "openvsx":
        ns, _, ext = arg.partition("/")
        dl = src.get("download", {}) or {}
        row["openvsx"] = {
            "ns": ns, "ext": ext, "version": version,
            "file": str(dl.get("file", "")) if isinstance(dl, dict) else "",
        }
    else:
        row["pkg"] = arg
        if extras:
            row["extras"] = extras
    if version:
        row["version"] = version

    # Binaries: public name -> how to run (kind "" = plain file).
    bin_map = data.get("bin") or {}
    bins = []
    runs = {}
    for pub, spec in bin_map.items():
        pub = str(pub)
        kind, hint = classify_bin(spec)
        bins.append(pub)
        if kind:
            runs[pub] = {"kind": kind, "hint": hint}
    row["bins"] = bins
    if runs:
        row["runs"] = runs
    if mgr == "npm" and isinstance(data.get("extra_packages"), list):
        row["extra_pkgs"] = [str(x) for x in data["extra_packages"]]
    return row


def main():
    registry_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "..", "mason-registry")
    pkg_root = os.path.join(registry_dir, "packages")
    if not os.path.isdir(pkg_root):
        sys.exit(f"mason-registry packages dir not found at {pkg_root}")

    # Parse every package once.
    parsed = {}
    for dirname in sorted(os.listdir(pkg_root)):
        path = os.path.join(pkg_root, dirname, "package.yaml")
        if not os.path.isfile(path):
            continue
        with open(path, encoding="utf-8") as f:
            data = yaml.safe_load(f)
        if not isinstance(data, dict) or not data or data.get("ci_skip"):
            continue
        name = data.get("name") or dirname
        parsed[name] = data

    # Build rows. Canonical target rows are folded into the canonical id.
    canonical_target = {v: k for k, v in CANONICAL.items()}
    rows = []
    for name in sorted(parsed):
        entry = build_entry(name, parsed[name])
        if entry is None:
            continue
        if name in canonical_target:
            continue  # owned by a canonical id below
        rows.append(entry)

    for cid, target in CANONICAL.items():
        data = parsed.get(target)
        if not data:
            print(f"  !! canonical {cid}: mason package {target} missing; skipping")
            continue
        entry = build_entry(target, data)
        entry["id"] = cid
        entry["aliases"] = ALIASES.get(cid, []) + [target]
        # Public bin names stay those of the underlying package.
        rows.append(entry)

    for cid, spec in JOT_ONLY.items():
        entry = {"id": cid, "display": spec["display"], "detail": spec["detail"],
                 "manager": spec["manager"], "categories": ["LSP"],
                 "languages": spec["languages"]}
        for field in ("pkg", "bins", "win_cmd", "win_remove_cmd"):
            if spec.get(field) is not None:
                entry[field] = spec[field]
        entry["aliases"] = ALIASES.get(cid, [])
        rows.append(entry)

    rows.sort(key=lambda r: r["id"])

    lines = [
        "-- LSP / language-tooling package catalog, generated from the mason registry.",
        "-- Source: https://github.com/mason-org/mason-registry",
        "-- Regenerate: python3 tools/mason_import.py <mason-registry-checkout>",
        "",
        "local M = {}",
        "",
        "M.entries = {",
    ]
    for row in rows:
        lines.append("  {")
        lines.append(f"    id = {lua_str(row['id'])}, display = {lua_str(row['display'])},"
                     f" detail = {lua_str(row['detail'])}, manager = {lua_str(row['manager'])},"
                     f" pkg = {lua_str(row.get('pkg', ''))},")
        if row.get("categories"):
            lines.append(f"    categories = {lua_str_list(row['categories'])},")
        if row.get("languages"):
            lines.append(f"    languages = {lua_str_list(row['languages'])},")
        if row.get("aliases"):
            lines.append(f"    aliases = {lua_str_list(row['aliases'])},")
        lines.append(f"    bin = {lua_str_list(row.get('bins', []))},")
        mgr = row["manager"]
        if mgr == "github":
            lines.append(f"    repo = {lua_str(row['repo'])}, asset = {{")
            for plat in ("linux", "mac", "win"):
                a = row.get("asset", {}).get(plat)
                if a:
                    lines.append(f"      {plat} = {{ match = {lua_str(a['match'])},"
                                 f" archive = {lua_str(a['archive'])} }},")
            lines.append("    },")
        elif mgr == "generic":
            lines.append("    dl = {")
            for plat in ("linux", "mac", "win"):
                d = row.get("dl", {}).get(plat)
                if d:
                    files = ", ".join(f"[{lua_str(k)}] = {lua_str(v)}"
                                      for k, v in d["files"].items())
                    lines.append(f"      {plat} = {{ files = {{{files}}},"
                                 f" bin = {lua_str(d['bin'])} }},")
            lines.append("    },")
        elif mgr == "openvsx":
            o = row.get("openvsx", {})
            lines.append(f"    openvsx = {{ ns = {lua_str(o.get('ns', ''))},"
                         f" ext = {lua_str(o.get('ext', ''))}, version = {lua_str(o.get('version', ''))},"
                         f" file = {lua_str(o.get('file', ''))} }},")
        extras = row.get("extras") or {}
        if extras:
            parts = ", ".join(f"[{lua_str(k)}] = {lua_str(v)}" for k, v in extras.items())
            lines.append(f"    extras = {{{parts}}},")
        if row.get("extra_pkgs"):
            lines.append(f"    extra_pkgs = {lua_str_list(row['extra_pkgs'])},")
        if row.get("version"):
            lines.append(f"    version = {lua_str(row['version'])}, -- snapshot pin")
        if row.get("runs"):
            lines.append("    runs = {")
            for pub, spec in row["runs"].items():
                lines.append(f"      [{lua_str(pub)}] = {{ kind = {lua_str(spec['kind'])},"
                             f" hint = {lua_str(spec.get('hint', ''))} }},")
            lines.append("    },")
        if row.get("win_cmd"):
            lines.append(f"    win_cmd = {lua_str(row['win_cmd'])},"
                         f" win_remove_cmd = {lua_str(row.get('win_remove_cmd', ''))},")
        lines.append("  },")
    lines.append("}")
    lines += [
        "",
        "-- Resolves a user-supplied id or alias to an entry.",
        "function M.resolve(name)",
        "  if not name then",
        "    return nil",
        "  end",
        "  local n = tostring(name):lower():gsub('%s', '')",
        "  for _, e in ipairs(M.entries) do",
        "    if e.id == n then",
        "      return e",
        "    end",
        "    for _, a in ipairs(e.aliases or {}) do",
        "      if a == n then",
        "        return e",
        "      end",
        "    end",
        "  end",
        "  return nil",
        "end",
        "",
        "return M",
    ]
    with open(REGISTRY_OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {REGISTRY_OUT}: {len(rows)} entries")

    # ---- attach table -----------------------------------------------------
    # Default LSP per extension: canonical ids keep priority; then a language's
    # curated default (only when that server really claims the language).
    lsp_by_name = {}  # package name -> canonical id / itself, LSP only
    for r in rows:
        if "LSP" in (r.get("categories") or []):
            lsp_by_name[r["id"]] = r["id"]

    claim = {}
    for name, data in parsed.items():
        if "LSP" not in (data.get("categories") or []):
            continue
        cid = canonical_target.get(name, name)
        if cid not in lsp_by_name:
            continue
        for lang in data.get("languages", []):
            claim.setdefault(str(lang), []).append(cid)

    ext_server = {}
    ext_langid = {}
    taken = set()
    # 1) canonical first (order in EXT_CANONICAL_EXT governs override)
    for ext, server in EXT_CANONICAL_EXT.items():
        ext_server[ext.lower()] = server
        taken.add(ext.lower())
    # 2) curated per-language defaults
    for lang, chosen in sorted(LANG_DEFAULT.items()):
        cands = claim.get(lang)
        if not cands or chosen not in cands:
            continue
        ext_info = LANG_EXT.get(lang)
        if not ext_info:
            continue
        for ext in ext_info[0]:
            key = ext.lower()
            if not key or key in taken or "." in key:
                continue
            ext_server[key] = chosen
            ext_langid[key] = ext_info[1]
            taken.add(key)

    header = [
        "// Generated by tools/mason_import.py — do not edit by hand.",
        "#pragma once",
        "// Best-effort file->LSP server auto-attach table (extension, server id).",
        "// The full catalog lives in src/lua/lsp/registry.lua.",
        "struct LspAttachEntry { const char *ext; const char *server; };",
        "static const LspAttachEntry kLspAttachTable[] = {",
    ]
    for ext, server in sorted(ext_server.items()):
        header.append(f'  {{"{ext}", "{server}"}},')
    header.append("};")
    # server -> primary managed bin name (launch path) for auto attach.
    bin_override = {"pyright": "pyright-langserver"}
    servers = set(ext_server.values()) | set(KNOWN_ARGS.keys())
    bin_map = {}
    for r in rows:
        if r["id"] in servers and r.get("bins"):
            bin_map[r["id"]] = bin_override.get(r["id"], r["bins"][0])
    for server in KNOWN_ARGS:
        bin_map.setdefault(server, bin_override.get(server, server))
    header.append("struct LspBinEntry { const char *server; const char *bin; };")
    header.append("static const LspBinEntry kLspBinTable[] = {")
    for server, bin_name in sorted(bin_map.items()):
        header.append(f'  {{"{server}", "{bin_name}"}},')
    header.append("};")
    header.append("// Server ids whose binary is a known stdio server (others launch bare).")
    header.append("struct LspKnownArgs { const char *server; const char *args; };")
    header.append("static const LspKnownArgs kLspKnownArgs[] = {")
    for server, args in sorted(KNOWN_ARGS.items()):
        header.append(f'  {{"{server}", "{args}"}},')
    header.append("};")
    header.append("// File extension -> LSP language id for generated attaches.")
    header.append("struct LspLangIdEntry { const char *ext; const char *langid; };")
    header.append("static const LspLangIdEntry kLspLangIdTable[] = {")
    for ext, langid in sorted(ext_langid.items()):
        header.append(f'  {{"{ext}", "{langid}"}},')
    header.append("};")
    with open(ATTACH_OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(header) + "\n")
    print(f"wrote {ATTACH_OUT}: {len(ext_server)} extension mappings, "
          f"{len(bin_map)} launch bins, {len(ext_langid)} language ids")


# Extensions that stay mapped to jot's native canonical ids regardless of the
# mason catalog (do not let another server steal .py etc).
EXT_CANONICAL_EXT = {
    "py": "python", "pyw": "python", "pyi": "python",
    "ts": "typescript", "tsx": "typescript", "mts": "typescript", "cts": "typescript",
    "js": "typescript", "jsx": "typescript", "mjs": "typescript", "cjs": "typescript",
    "c": "cpp", "h": "cpp", "cpp": "cpp", "hpp": "cpp", "cc": "cpp", "cxx": "cpp",
    "rs": "rust", "go": "go", "lua": "lua",
    "sh": "bash", "bash": "bash", "zsh": "bash",
    "html": "html", "htm": "html",
    "json": "json", "jsonc": "json",
    "css": "css", "scss": "css", "less": "css",
    "yaml": "yaml", "yml": "yaml",
    "vue": "vue", "sql": "sql", "php": "php",
    "md": "markdown", "markdown": "markdown",
    "dockerfile": "dockerfile",
}


if __name__ == "__main__":
    main()
