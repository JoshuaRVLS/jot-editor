-- LSP / language-tooling package catalog shard: revive .. sonarlint-language-server.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
  {
    id = "revive", display = "revive", detail = "~6x faster, stricter, configurable, extensible, and beautiful drop-in replacement for golint.", manager = "golang", pkg = "github.com/mgechev/revive",
    categories = {"Linter"},
    languages = {"Go"},
    bin = {"revive"},
    version = "v1.16.0", -- snapshot pin
  },
  {
    id = "ripper-tags", display = "ripper-tags", detail = "fast, accurate ctags generator for ruby source code using Ripper.", manager = "gem", pkg = "ripper-tags",
    languages = {"Ruby"},
    bin = {"ripper-tags"},
    version = "1.0.2", -- snapshot pin
  },
  {
    id = "rnix-lsp", display = "rnix-lsp", detail = "Language Server for Nix.", manager = "cargo", pkg = "rnix-lsp",
    categories = {"LSP"},
    languages = {"Nix"},
    bin = {"rnix-lsp"},
    version = "0.2.5", -- snapshot pin
  },
  {
    id = "robotcode", display = "robotcode", detail = "The Ultimate Robot Framework Toolset", manager = "pypi", pkg = "robotcode",
    categories = {"LSP", "DAP"},
    languages = {"Robot Framework"},
    bin = {"robotcode"},
    extras = {["extra"] = "all"},
    version = "2.7.0", -- snapshot pin
  },
  {
    id = "robotframework-lsp", display = "robotframework-lsp", detail = "Language Server Protocol implementation for Robot Framework.", manager = "pypi", pkg = "robotframework-lsp",
    categories = {"LSP"},
    languages = {"Robot Framework"},
    bin = {"robotframework_ls"},
    version = "1.13.0", -- snapshot pin
  },
  {
    id = "roc_language_server", display = "roc_language_server", detail = "This is a basic language server for Roc.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Roc"},
    bin = {"roc_language_server"},
    repo = "roc-lang/roc", asset = {
      mac = { match = "roc\\-macos_apple_silicon\\-.*\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "alpha4-rolling", -- snapshot pin
  },
  {
    id = "roslyn-language-server", display = "roslyn-language-server", detail = "Roslyn-based C# language server providing rich IntelliSense, refactoring, and diagnostics. Requires dotnet to be inst...", manager = "nuget", pkg = "roslyn-language-server",
    categories = {"LSP"},
    languages = {"C#"},
    bin = {"roslyn-language-server"},
    version = "5.11.0-1.26380.4", -- snapshot pin
  },
  {
    id = "rpm_lsp_server", display = "rpm_lsp_server", detail = "Language server protocol (LSP) support for RPM Spec files.", manager = "pypi", pkg = "rpm-spec-language-server",
    categories = {"LSP"},
    languages = {"Spec"},
    bin = {"rpm_lsp_server"},
    version = "0.0.2", -- snapshot pin
  },
  {
    id = "rpmlint", display = "rpmlint", detail = "Rpmlint is a tool for checking common errors in RPM packages.", manager = "pypi", pkg = "rpmlint",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"rpmlint"},
    version = "2.10.0", -- snapshot pin
  },
  {
    id = "rshtml-analyzer", display = "rshtml-analyzer", detail = "rshtml-analyzer is an implementation of the Language Server Protocol (LSP) for the RsHtml templating language which e...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"RsHtml"},
    bin = {"rshtml-analyzer"},
    repo = "rshtml/rshtml-analyzer", asset = {
      linux = { match = "rshtml\\-analyzer\\-linux\\-x64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "rshtml\\-analyzer\\-macos\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "rshtml\\-analyzer\\-windows\\-x64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.1.7", -- snapshot pin
  },
  {
    id = "rstcheck", display = "rstcheck", detail = "Checks syntax of reStructuredText and code blocks nested within it.", manager = "pypi", pkg = "rstcheck",
    categories = {"Linter"},
    languages = {"reStructuredText"},
    bin = {"rstcheck"},
    version = "6.3.0", -- snapshot pin
  },
  {
    id = "rubocop", display = "rubocop", detail = "The Ruby Linter/Formatter that Serves and Protects.", manager = "gem", pkg = "rubocop",
    categories = {"Formatter", "Linter", "LSP"},
    languages = {"Ruby"},
    bin = {"rubocop"},
    version = "1.90.0", -- snapshot pin
  },
  {
    id = "ruby-lsp", display = "ruby-lsp", detail = "This gem is an implementation of the language server protocol specification for Ruby, used to improve editor features.", manager = "gem", pkg = "ruby-lsp",
    categories = {"LSP"},
    languages = {"Ruby"},
    bin = {"ruby-lsp"},
    version = "0.26.11", -- snapshot pin
  },
  {
    id = "rubyfmt", display = "rubyfmt", detail = "Ruby Autoformatter!", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Ruby"},
    bin = {"rubyfmt"},
    repo = "fables-tales/rubyfmt", asset = {
      linux = { match = "rubyfmt\\-.*\\-Linux\\-x86_64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "rubyfmt\\-.*\\-Darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.14.1", -- snapshot pin
  },
  {
    id = "ruff", display = "ruff", detail = "An extremely fast Python linter and code formatter, written in Rust.", manager = "github", pkg = "",
    categories = {"Linter", "Formatter", "LSP"},
    languages = {"Python"},
    bin = {"ruff"},
    repo = "astral-sh/ruff", asset = {
      linux = { match = "ruff\\-x86_64\\-unknown\\-linux\\-musl\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "ruff\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ruff\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.16.6", -- snapshot pin
  },
  {
    id = "rufo", display = "rufo", detail = "Rufo is as an opinionated ruby formatter, intended to be used via the command line as a text-editor plugin, to autofo...", manager = "gem", pkg = "rufo",
    categories = {"Formatter"},
    languages = {"Ruby"},
    bin = {"rufo"},
    version = "0.18.2", -- snapshot pin
  },
  {
    id = "rumdl", display = "rumdl", detail = "Fast Markdown linter and formatter.", manager = "github", pkg = "",
    categories = {"Linter", "Formatter", "LSP"},
    languages = {"Markdown"},
    bin = {"rumdl"},
    repo = "rvben/rumdl", asset = {
      linux = { match = "rumdl\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "rumdl\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "rumdl\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.2.64", -- snapshot pin
  },
  {
    id = "rust", display = "rust-analyzer", detail = "rust-analyzer is an implementation of the Language Server Protocol for the Rust programming language. It provides fea...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Rust"},
    aliases = {"rs", "rust-analyzer", "rust-analyzer"},
    bin = {"rust-analyzer"},
    repo = "rust-lang/rust-analyzer", asset = {
      linux = { match = "rust\\-analyzer\\-x86_64\\-unknown\\-linux\\-gnu\\.gz", archive = "gz" },
      mac = { match = "rust\\-analyzer\\-aarch64\\-apple\\-darwin\\.gz", archive = "gz" },
      win = { match = "rust\\-analyzer\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "2026-08-31", -- snapshot pin
  },
  {
    id = "rust_hdl", display = "rust_hdl", detail = "rust_hdl is a VHDL language server and analysis library written in Rust. A complete VHDL language server protocol imp...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"VHDL"},
    bin = {"vhdl_ls"},
    repo = "vhdl-ls/rust_hdl", asset = {
      mac = { match = "vhdl_ls\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "vhdl_ls\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.88.0", -- snapshot pin
  },
  {
    id = "rustywind", display = "rustywind", detail = "CLI for organizing Tailwind CSS classes.", manager = "npm", pkg = "rustywind",
    categories = {"Formatter"},
    languages = {"Angular", "HTML", "JSX", "JavaScript", "TypeScript", "Vue"},
    bin = {"rustywind"},
    version = "0.28.0", -- snapshot pin
  },
  {
    id = "salt-lint", display = "salt-lint", detail = "A command-line utility that checks for best practices in SaltStack.", manager = "pypi", pkg = "salt-lint",
    categories = {"Linter"},
    languages = {"Salt"},
    bin = {"salt-lint"},
    version = "0.9.2", -- snapshot pin
  },
  {
    id = "salt-lsp", display = "salt-lsp", detail = "Salt Language Server Protocol Server.", manager = "pypi", pkg = "salt-lsp",
    categories = {"LSP"},
    languages = {"Salt"},
    bin = {"salt_lsp_server"},
    version = "0.0.1", -- snapshot pin
  },
  {
    id = "selene", display = "selene", detail = "A blazing-fast modern Lua linter written in Rust.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Lua", "Luau"},
    bin = {"selene"},
    repo = "Kampfkarren/selene", asset = {
      mac = { match = "selene\\-.*\\-macos\\.zip", archive = "zip" },
      win = { match = "selene\\-.*\\-windows\\.zip", archive = "zip" },
    },
    version = "0.31.0", -- snapshot pin
  },
  {
    id = "semgrep", display = "semgrep", detail = "Semgrep is a fast, open-source, static analysis engine for finding bugs, detecting vulnerabilities in third-party dep...", manager = "pypi", pkg = "semgrep",
    categories = {"Linter"},
    languages = {"C#", "Go", "JSON", "Java", "JavaScript", "PHP", "Python", "Ruby", "Scala", "TypeScript"},
    bin = {"semgrep", "pysemgrep"},
    version = "1.176.0", -- snapshot pin
  },
  {
    id = "serve-d", display = "serve-d", detail = "Microsoft language server protocol implementation for D using workspace-d.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"D"},
    bin = {"serve-d"},
    repo = "Pure-D/serve-d", asset = {
      mac = { match = "serve\\-d_.*\\-osx\\-x86_64\\.tar\\.xz", archive = "none" },
      win = { match = "serve\\-d_.*\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v0.7.6", -- snapshot pin
  },
  {
    id = "sharpdbg", display = "sharpdbg", detail = "SharpDbg is a .NET managed code debugger supporting the Debug Adapter Protocol, implemented completely in C#/.NET Req...", manager = "nuget", pkg = "SharpDbg.Cli",
    categories = {"DAP"},
    languages = {".NET", "C#", "F#"},
    bin = {"sharpdbg"},
    version = "0.1.13", -- snapshot pin
  },
  {
    id = "shellcheck", display = "shellcheck", detail = "ShellCheck, a static analysis tool for shell scripts.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Bash"},
    bin = {"shellcheck"},
    repo = "vscode-shellcheck/shellcheck-binaries", asset = {
      mac = { match = "shellcheck\\-.*\\.darwin\\.aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "shellcheck\\-.*\\.windows\\.x86_64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.11.0", -- snapshot pin
  },
  {
    id = "shellharden", display = "shellharden", detail = "The corrective bash syntax highlighter.", manager = "cargo", pkg = "shellharden",
    categories = {"Formatter", "Linter"},
    languages = {"Bash"},
    bin = {"shellharden"},
    version = "4.3.2", -- snapshot pin
  },
  {
    id = "shfmt", display = "shfmt", detail = "A shell formatter (sh/bash/zsh/mksh).", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Bash", "Mksh", "Shell", "Zsh"},
    bin = {"shfmt"},
    repo = "mvdan/sh", asset = {
      mac = { match = "shfmt_.*_darwin_arm64", archive = "none" },
      win = { match = "shfmt_.*_windows_amd64\\.exe", archive = "none" },
    },
    version = "v3.14.0", -- snapshot pin
  },
  {
    id = "shopify-cli", display = "shopify-cli", detail = "Command-line interface tool that helps you generate and work with Shopify apps, themes and custom storefronts.", manager = "npm", pkg = "%40shopify/cli",
    categories = {"LSP", "Linter"},
    languages = {"Liquid"},
    bin = {"shopify"},
    version = "4.7.1", -- snapshot pin
  },
  {
    id = "shuck", display = "shuck", detail = "A lightning fast shell linter, formatter, and LSP server for bash, zsh, posix, and mksh dialects.", manager = "github", pkg = "",
    categories = {"LSP", "Linter", "Formatter"},
    languages = {"Bash", "Zsh", "Sh", "Mksh"},
    bin = {"shuck"},
    repo = "ewhauser/shuck", asset = {
      linux = { match = "shuck\\-cli\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.xz", archive = "none" },
      mac = { match = "shuck\\-cli\\-aarch64\\-apple\\-darwin\\.tar\\.xz", archive = "none" },
      win = { match = "shuck\\-cli\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.2.1", -- snapshot pin
  },
  {
    id = "sith-language-server", display = "sith-language-server", detail = "An experimental language server for the Python programming language. - made in Rust.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"sith-lsp"},
    repo = "LaBatata101/sith-language-server", asset = {
      linux = { match = "sith\\-lsp\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "sith\\-lsp\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "sith\\-lsp\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.2.2-alpha", -- snapshot pin
  },
  {
    id = "slang", display = "slang", detail = "Slang is a shading language that makes it easier to build and maintain large shader codebases in a modular and extens...", manager = "github", pkg = "",
    categories = {"LSP", "Compiler"},
    languages = {"Slang"},
    bin = {"slangd", "slangc"},
    repo = "shader-slang/slang", asset = {
      linux = { match = "slang\\-.*\\-linux\\-x86_64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "slang\\-.*\\-macos\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "slang\\-.*\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v2026.17", -- snapshot pin
  },
  {
    id = "slang-server", display = "slang-server", detail = "A SystemVerilog language server based on the Slang library.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"SystemVerilog", "Verilog"},
    bin = {"slang-server"},
    repo = "hudson-trading/slang-server", asset = {
      linux = { match = "slang\\-server\\-old\\-linux\\-x64\\-gcc\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "slang\\-server\\-macos\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "slang\\-server\\-windows\\-x64\\.zip", archive = "zip" },
    },
    version = "v0.2.10", -- snapshot pin
  },
  {
    id = "sleek", display = "sleek", detail = "Sleek is a CLI tool for formatting SQL.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"SQL"},
    bin = {"sleek"},
    repo = "nrempel/sleek", asset = {
      mac = { match = "sleek\\-macos\\-aarch64", archive = "none" },
      win = { match = "sleek\\-windows\\-x86_64\\.exe", archive = "none" },
    },
    version = "v0.5.0", -- snapshot pin
  },
  {
    id = "slint-lsp", display = "slint-lsp", detail = "A LSP Server that adds features like auto-complete and live preview of the .slint files to many editors.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Slint"},
    bin = {"slint-lsp"},
    repo = "slint-ui/slint", asset = {
      mac = { match = "slint\\-lsp\\-macos\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "slint\\-lsp\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v1.17.1", -- snapshot pin
  },
  {
    id = "smithy-language-server", display = "smithy-language-server", detail = "A Language Server Protocol implementation for the Smithy IDL.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Smithy"},
    bin = {"smithy-language-server"},
    repo = "awslabs/smithy-language-server", asset = {
      mac = { match = "smithy\\-language\\-server\\-darwin\\-aarch64\\.zip", archive = "zip" },
      win = { match = "smithy\\-language\\-server\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "0.9.0", -- snapshot pin
  },
  {
    id = "snakefmt", display = "snakefmt", detail = "The uncompromising Snakemake code formatter.", manager = "pypi", pkg = "snakefmt",
    categories = {"Formatter"},
    languages = {"Snakemake"},
    bin = {"snakefmt"},
    version = "2.0.3", -- snapshot pin
  },
  {
    id = "snakeskin-cli", display = "snakeskin-cli", detail = "Snakeskin is an awesome JavaScript template engine with the best support for inheritance.", manager = "npm", pkg = "%40snakeskin/cli",
    categories = {"LSP"},
    languages = {"Snakeskin"},
    bin = {"snakeskin-cli"},
    version = "0.0.8", -- snapshot pin
  },
  {
    id = "snyk", display = "snyk", detail = "Snyk CLI scans and monitors your projects for security vulnerabilities", manager = "github", pkg = "",
    categories = {"LSP", "Linter"},
    languages = {".NET", "Apex", "C#", "C++", "Dart", "Docker", "Elixir", "Go", "Groovy", "Helm", "Java", "JavaScript", "Kotlin", "PHP", "Python", "Ruby", "Rust", "Scala", "Swift", "Terraform", "TypeScript"},
    bin = {"snyk"},
    repo = "snyk/cli", asset = {
      linux = { match = "snyk\\-linux", archive = "none" },
      mac = { match = "snyk\\-macos\\-arm64", archive = "none" },
      win = { match = "snyk\\-win\\.exe", archive = "none" },
    },
    version = "v1.1307.0", -- snapshot pin
  },
  {
    id = "solang", display = "solang", detail = "Solidity Compiler for Solana, Substrate, and ewasm.", manager = "github", pkg = "",
    categories = {"LSP", "Compiler"},
    languages = {"Solidity"},
    bin = {"solang"},
    repo = "hyperledger-labs/solang", asset = {
      mac = { match = "solang\\-mac\\-arm", archive = "none" },
      win = { match = "solang\\.exe", archive = "none" },
    },
    version = "v0.3.4", -- snapshot pin
  },
  {
    id = "solang-llvm", display = "solang-llvm", detail = "Solang requires Solana's LLVM fork. We provide pre-built binaries compatible with Solang.", manager = "github", pkg = "",
    categories = {"LSP", "Compiler"},
    languages = {"Solidity"},
    bin = {},
    repo = "hyperledger/solang-llvm", asset = {
      mac = { match = "llvm15\\.0\\-mac\\-arm\\.tar\\.xz", archive = "none" },
      win = { match = "llvm15\\.0\\-win\\.zip", archive = "zip" },
    },
    version = "llvm15-0", -- snapshot pin
  },
  {
    id = "solargraph", display = "solargraph", detail = "Solargraph is a Ruby gem that provides intellisense features through the language server protocol.", manager = "gem", pkg = "solargraph",
    categories = {"LSP"},
    languages = {"Ruby"},
    bin = {"solargraph"},
    version = "0.60.4", -- snapshot pin
  },
  {
    id = "solhint", display = "solhint", detail = "Solhint is a linting utility for Solidity code.", manager = "npm", pkg = "solhint",
    categories = {"Linter"},
    languages = {"Solidity"},
    bin = {"solhint"},
    version = "6.2.4", -- snapshot pin
  },
  {
    id = "solidity", display = "solidity", detail = "Solidity, the Smart Contract Programming Language.", manager = "github", pkg = "",
    categories = {"Compiler", "LSP"},
    languages = {"Solidity"},
    bin = {"solc"},
    repo = "ethereum/solidity", asset = {
      mac = { match = "solc\\-macos", archive = "none" },
      win = { match = "solc\\-windows\\.exe", archive = "none" },
    },
    version = "v0.8.36", -- snapshot pin
  },
  {
    id = "solidity-ls", display = "solidity-ls", detail = "Solidity language server.", manager = "npm", pkg = "solidity-ls",
    categories = {"LSP"},
    languages = {"Solidity"},
    bin = {"solidity-ls"},
    version = "0.5.4", -- snapshot pin
  },
  {
    id = "some-sass-language-server", display = "some-sass-language-server", detail = "Full support for @use and @forward, including aliases, prefixes and hiding. Rich documentation through SassDoc. Works...", manager = "npm", pkg = "some-sass-language-server",
    categories = {"LSP"},
    languages = {"SCSS"},
    bin = {"some-sass-language-server"},
    version = "2.3.8", -- snapshot pin
  },
  {
    id = "sonarlint-language-server", display = "sonarlint-language-server", detail = "SonarLint Language Server.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"AzureResourceManager", "C", "C++", "C#", "CloudFormation", "CSS", "Docker", "Go", "HTML", "IPython", "Java", "JavaScript", "Kubernetes", "TypeScript", "Python", "PHP", "Terraform", "Text", "XML", "YAML"},
    bin = {"sonarlint-language-server"},
    repo = "SonarSource/sonarlint-vscode", asset = {
      linux = { match = "sonarlint\\-vscode\\-4\\.39\\.0\\.vsix", archive = "none" },
      mac = { match = "sonarlint\\-vscode\\-4\\.39\\.0\\.vsix", archive = "none" },
      win = { match = "sonarlint\\-vscode\\-4\\.39\\.0\\.vsix", archive = "none" },
    },
    version = "4.39.0%2B79757", -- snapshot pin
    runs = {
      ["sonarlint-language-server"] = { kind = "jar", hint = "extension/server/sonarlint-ls.jar" },
    },
  },
}
