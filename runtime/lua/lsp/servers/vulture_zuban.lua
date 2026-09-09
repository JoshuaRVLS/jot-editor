-- LSP / language-tooling package catalog shard: vulture .. zuban.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
  {
    id = "vulture", display = "vulture", detail = "Vulture finds unused code in Python programs. This is useful for cleaning up and finding errors in large code bases. ...", manager = "pypi", pkg = "vulture",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"vulture"},
    version = "2.16", -- snapshot pin
  },
  {
    id = "wasm-language-tools", display = "wasm-language-tools", detail = "Language server and other tools for WebAssembly.", manager = "github", pkg = "",
    categories = {"LSP", "Formatter", "Linter"},
    languages = {"WebAssembly"},
    bin = {"wat_server"},
    repo = "g-plane/wasm-language-tools", asset = {
      linux = { match = "wat_server\\-x86_64\\-linux\\.zip", archive = "zip" },
      mac = { match = "wat_server\\-arm64\\-macos\\.zip", archive = "zip" },
      win = { match = "wat_server\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v0.11.0", -- snapshot pin
  },
  {
    id = "wc-language-server", display = "wc-language-server", detail = "Language server that surfaces Web Components metadata, completions, and diagnostics. Provides rich language features ...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"HTML", "JavaScript", "TypeScript"},
    bin = {"wc-language-server"},
    repo = "wc-toolkit/wc-language-server", asset = {
      mac = { match = "wc\\-language\\-server\\-macos\\-arm64", archive = "none" },
      win = { match = "wc\\-language\\-server\\-windows\\-x64\\.exe", archive = "none" },
    },
    version = "%40wc-toolkit%2Flanguage-server%400.0.6", -- snapshot pin
  },
  {
    id = "wgsl-analyzer", display = "wgsl-analyzer", detail = "A language server implementation for the WGSL shading language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"WGSL"},
    bin = {"wgsl-analyzer"},
    repo = "wgsl-analyzer/wgsl-analyzer", asset = {
      linux = { match = "wgsl\\-analyzer\\-x86_64\\-unknown\\-linux\\-gnu\\.gz", archive = "gz" },
      mac = { match = "wgsl\\-analyzer\\-aarch64\\-apple\\-darwin\\.gz", archive = "gz" },
      win = { match = "wgsl\\-analyzer\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "2026-04-26", -- snapshot pin
  },
  {
    id = "wing", display = "wing", detail = "A programming language for the cloud", manager = "npm", pkg = "winglang",
    categories = {"Compiler", "DAP", "LSP", "Runtime"},
    languages = {"Wing"},
    bin = {"wing"},
    version = "0.85.51", -- snapshot pin
  },
  {
    id = "woke", display = "woke", detail = "Detect non-inclusive language in your source code.", manager = "github", pkg = "",
    categories = {"Linter"},
    bin = {"woke"},
    repo = "get-woke/woke", asset = {
      mac = { match = "woke\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "woke\\-.*\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v0.19.0", -- snapshot pin
  },
  {
    id = "write-good", display = "write-good", detail = "Naive linter for English prose for developers who can't write good and wanna learn to do other stuff good too.", manager = "npm", pkg = "write-good",
    categories = {"Linter"},
    languages = {"Markdown"},
    bin = {"write-good"},
    version = "1.0.8", -- snapshot pin
  },
  {
    id = "xcbeautify", display = "xcbeautify", detail = "A little beautifier tool for xcodebuild", manager = "github", pkg = "",
    categories = {"Runtime"},
    languages = {"Swift"},
    bin = {"xcbeautify"},
    repo = "cpisciotta/xcbeautify", asset = {
      linux = { match = "xcbeautify\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.xz", archive = "none" },
      mac = { match = "xcbeautify\\-.*\\-arm64\\-apple\\-macosx\\.zip", archive = "zip" },
    },
    version = "3.2.1", -- snapshot pin
  },
  {
    id = "xcodegen", display = "xcodegen", detail = "A Swift command line tool for generating your Xcode project", manager = "github", pkg = "",
    categories = {"Runtime"},
    languages = {"Swift"},
    bin = {"xcodegen"},
    repo = "yonaskolb/XcodeGen", asset = {
      linux = { match = "xcodegen\\.zip", archive = "zip" },
      mac = { match = "xcodegen\\.artifactbundle\\.zip", archive = "zip" },
    },
    version = "2.46.0", -- snapshot pin
  },
  {
    id = "xmlformatter", display = "xmlformatter", detail = "xmlformatter is an Open Source Python package that provides formatting of XML documents. xmlformatter differs from ot...", manager = "pypi", pkg = "xmlformatter",
    categories = {"Formatter"},
    languages = {"XML"},
    bin = {"xmlformat"},
    version = "0.2.9", -- snapshot pin
  },
  {
    id = "yaml", display = "yaml-language-server", detail = "Language Server for YAML Files.", manager = "npm", pkg = "yaml-language-server",
    categories = {"LSP"},
    languages = {"YAML"},
    aliases = {"yml", "yaml-language-server", "yaml-language-server"},
    bin = {"yaml-language-server"},
    version = "1.24.0", -- snapshot pin
  },
  {
    id = "yamlfix", display = "yamlfix", detail = "A simple and configurable YAML formatter that keeps comments.", manager = "pypi", pkg = "yamlfix",
    categories = {"Formatter"},
    languages = {"YAML"},
    bin = {"yamlfix"},
    version = "1.19.1", -- snapshot pin
  },
  {
    id = "yamlfmt", display = "yamlfmt", detail = "yamlfmt is an extensible command line tool or library to format yaml files.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"YAML"},
    bin = {"yamlfmt"},
    repo = "google/yamlfmt", asset = {
      mac = { match = "yamlfmt_.*_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "yamlfmt_.*_Windows_x86_64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.21.0", -- snapshot pin
  },
  {
    id = "yamllint", display = "yamllint", detail = "Linter for YAML files. yamllint does not only check for syntax validity, but for weirdnesses like key repetition and ...", manager = "pypi", pkg = "yamllint",
    categories = {"Linter"},
    languages = {"YAML"},
    bin = {"yamllint"},
    version = "1.38.0", -- snapshot pin
  },
  {
    id = "yapf", display = "yapf", detail = "YAPF, Yet Another Python Formatter.", manager = "pypi", pkg = "yapf",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"yapf"},
    version = "0.43.0", -- snapshot pin
  },
  {
    id = "yls-yara", display = "yls-yara", detail = "Language server for the YARA language.", manager = "pypi", pkg = "yls-yara",
    categories = {"LSP"},
    languages = {"YARA"},
    bin = {"yls"},
    version = "1.4.4", -- snapshot pin
  },
  {
    id = "yq", display = "yq", detail = "yq is a portable command-line YAML, JSON, XML, CSV, TOML and properties processor.", manager = "github", pkg = "",
    languages = {"YAML"},
    bin = {"yq"},
    repo = "mikefarah/yq", asset = {
      linux = { match = "yq_linux_amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "yq_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "yq_windows_amd64\\.exe", archive = "none" },
    },
    version = "v4.53.6", -- snapshot pin
  },
  {
    id = "zeek-language-server", display = "zeek-language-server", detail = "This project implements a language server for Zeek script.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Zeek"},
    bin = {"zeek-language-server"},
    repo = "bbannier/zeek-language-server", asset = {
      linux = { match = "zeek\\-language\\-server\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.xz", archive = "none" },
      mac = { match = "zeek\\-language\\-server\\-aarch64\\-apple\\-darwin\\.tar\\.xz", archive = "none" },
    },
    version = "v0.75.1", -- snapshot pin
  },
  {
    id = "zizmor", display = "zizmor", detail = "Static analysis for GitHub Actions", manager = "pypi", pkg = "zizmor",
    categories = {"Linter"},
    languages = {"YAML"},
    bin = {"zizmor"},
    version = "1.30.0", -- snapshot pin
  },
  {
    id = "zk", display = "zk", detail = "A plain text note-taking assistant.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"zk"},
    repo = "zk-org/zk", asset = {
      mac = { match = "zk\\-.*\\-macos\\-arm64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.15.6", -- snapshot pin
  },
  {
    id = "zlint", display = "zlint", detail = "An opinionated linter for the Zig programming language", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Zig"},
    bin = {"zlint"},
    repo = "DonIsaac/zlint", asset = {
      mac = { match = "zlint\\-macos\\-aarch64", archive = "none" },
      win = { match = "zlint\\-windows\\-x86_64\\.exe", archive = "none" },
    },
    version = "v0.9.1", -- snapshot pin
  },
  {
    id = "zls", display = "zls", detail = "Zig LSP implementation + Zig Language Server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Zig"},
    bin = {"zls"},
    repo = "zigtools/zls", asset = {
      mac = { match = "zls\\-aarch64\\-macos\\.tar\\.xz", archive = "none" },
      win = { match = "zls\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "0.16.0", -- snapshot pin
  },
  {
    id = "zprint", display = "zprint", detail = "Beautifully format Clojure and Clojurescript source code and s-expressions.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"zprint"},
    repo = "kkinnear/zprint", asset = {
      linux = { match = "zprintl\\-.*", archive = "none" },
      mac = { match = "zprintma\\-.*", archive = "none" },
    },
    version = "1.3.0", -- snapshot pin
  },
  {
    id = "zprint-clj", display = "zprint-clj", detail = "Node.js wrapper for ZPrint Clojure source code formatter", manager = "npm", pkg = "zprint-clj",
    categories = {"Formatter"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"zprint-clj"},
    version = "0.8.0", -- snapshot pin
  },
  {
    id = "zuban", display = "zuban", detail = "Zuban is a high-performant Mypy-compatible LSP and type checker built in Rust.", manager = "pypi", pkg = "zuban",
    categories = {"Linter", "LSP"},
    languages = {"Python"},
    bin = {"zuban"},
    version = "0.9.3", -- snapshot pin
  },
}
