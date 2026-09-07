-- LSP / language-tooling package catalog, generated from the mason registry.
-- Source: https://github.com/mason-org/mason-registry
-- Regenerate: python3 tools/mason_import.py <mason-registry-checkout>

local M = {}

M.entries = {
  {
    id = "actionlint", display = "actionlint", detail = "Static checker for GitHub Actions workflow files.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"YAML"},
    bin = {"actionlint"},
    repo = "rhysd/actionlint", asset = {
      mac = { match = "actionlint_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "actionlint_.*_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v1.7.12", -- snapshot pin
  },
  {
    id = "ada-language-server", display = "ada-language-server", detail = "Ada/SPARK language server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Ada"},
    bin = {"ada_language_server"},
    repo = "AdaCore/ada_language_server", asset = {
      linux = { match = "als\\-.*\\-linux\\-x64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "als\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "als\\-.*\\-win32\\-x64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "2026.3.202607051", -- snapshot pin
  },
  {
    id = "aiken", display = "aiken", detail = "A modern smart contract platform for Cardano", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Aiken"},
    bin = {"aiken"},
    repo = "aiken-lang/aiken", asset = {
      mac = { match = "aiken\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "aiken\\-x86_64\\-pc\\-windows\\-msvc\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v1.1.23", -- snapshot pin
  },
  {
    id = "air", display = "air", detail = "R formatter and language server", manager = "github", pkg = "",
    categories = {"Formatter", "LSP"},
    languages = {"R"},
    bin = {"air"},
    repo = "posit-dev/air", asset = {
      linux = { match = "air\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "air\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "air\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.11.0", -- snapshot pin
  },
  {
    id = "alejandra", display = "alejandra", detail = "The Uncompromising Nix Code Formatter", manager = "cargo", pkg = "alejandra_cli",
    categories = {"Formatter"},
    languages = {"Nix"},
    bin = {"alejandra"},
    extras = {["repository_url"] = "https://github.com/kamadorueda/alejandra"},
    version = "4.0.0", -- snapshot pin
  },
  {
    id = "alex", display = "alex", detail = "Catch insensitive, inconsiderate writing.", manager = "npm", pkg = "alex",
    categories = {"Linter"},
    languages = {"Markdown"},
    bin = {"alex"},
    version = "11.0.1", -- snapshot pin
  },
  {
    id = "amber-lsp", display = "amber-lsp", detail = "Amber's Language Server Protocol", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Amber"},
    bin = {"amber-lsp"},
    repo = "amber-lang/amber-lsp", asset = {
      linux = { match = "amber\\-lsp\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "amber\\-lsp\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "amber\\-lsp\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.3.0", -- snapshot pin
  },
  {
    id = "angular-language-server", display = "angular-language-server", detail = "The Angular Language Service provides code editors with a way to get completions, errors, hints, and navigation insid...", manager = "npm", pkg = "%40angular/language-server",
    categories = {"LSP"},
    languages = {"Angular"},
    bin = {"ngserver"},
    version = "22.1.4", -- snapshot pin
  },
  {
    id = "ansible-language-server", display = "ansible-language-server", detail = "Ansible Language Server.", manager = "npm", pkg = "%40ansible/ansible-language-server",
    categories = {"LSP"},
    languages = {"Ansible"},
    bin = {"ansible-language-server"},
    version = "26.6.0", -- snapshot pin
  },
  {
    id = "ansible-lint", display = "ansible-lint", detail = "Ansible Lint is a command-line tool for linting playbooks, roles and collections aimed toward any Ansible users.", manager = "pypi", pkg = "ansible-lint",
    categories = {"Linter"},
    languages = {"Ansible"},
    bin = {"ansible-lint"},
    version = "26.8.0", -- snapshot pin
  },
  {
    id = "antlers-language-server", display = "antlers-language-server", detail = "Provides rich language features for Statamic's Antlers templating language, including code completions, syntax highli...", manager = "npm", pkg = "antlers-language-server",
    categories = {"LSP"},
    languages = {"Antlers"},
    bin = {"antlersls"},
    version = "1.3.17", -- snapshot pin
  },
  {
    id = "apex-language-server", display = "apex-language-server", detail = "The Apex Language Server is an IDE-agnostic way for tools to access code-editing capabilities such as code completion...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Apex"},
    bin = {},
    repo = "forcedotcom/salesforcedx-vscode", asset = {
      linux = { match = "salesforcedx\\-vscode\\-apex\\-.*\\.vsix", archive = "none" },
      mac = { match = "salesforcedx\\-vscode\\-apex\\-.*\\.vsix", archive = "none" },
      win = { match = "salesforcedx\\-vscode\\-apex\\-.*\\.vsix", archive = "none" },
    },
    version = "v67.17.2", -- snapshot pin
  },
  {
    id = "api-linter", display = "api-linter", detail = "A linter for APIs defined in protocol buffers.", manager = "golang", pkg = "github.com/googleapis/api-linter/v2",
    categories = {"Linter"},
    languages = {"Protobuf"},
    bin = {"api-linter"},
    version = "v2.3.1#cmd/api-linter", -- snapshot pin
  },
  {
    id = "arduino-language-server", display = "arduino-language-server", detail = "An Arduino Language Server based on Clangd to Arduino code autocompletion.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Arduino"},
    bin = {"arduino-language-server"},
    repo = "arduino/arduino-language-server", asset = {
      mac = { match = "arduino\\-language\\-server_.*_macOS_64bit\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "arduino\\-language\\-server_.*_Windows_64bit\\.zip", archive = "zip" },
    },
    version = "0.7.7", -- snapshot pin
  },
  {
    id = "asm-lsp", display = "asm-lsp", detail = "Language server for NASM/GAS/GO Assembly.", manager = "cargo", pkg = "asm-lsp",
    categories = {"LSP"},
    languages = {"Assembly"},
    bin = {"asm-lsp"},
    version = "0.10.1", -- snapshot pin
  },
  {
    id = "asmfmt", display = "asmfmt", detail = "Go Assembler Formatter This will format your assembler code in a similar way that gofmt formats your Go code.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Assembly"},
    bin = {"asmfmt"},
    repo = "klauspost/asmfmt", asset = {
      mac = { match = "asmfmt\\-OSX_arm64_.*\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "asmfmt\\-Windows_x86_64_.*\\.zip", archive = "zip" },
    },
    version = "v1.3.2", -- snapshot pin
  },
  {
    id = "ast-grep", display = "ast-grep", detail = "A CLI tool for code structural search, lint and rewriting. Written in Rust.", manager = "github", pkg = "",
    categories = {"Linter", "Formatter", "Runtime", "LSP"},
    languages = {"C", "C++", "Rust", "Go", "Java", "Python", "C#", "JavaScript", "JSX", "TypeScript", "HTML", "CSS", "Kotlin", "Dart", "Lua"},
    bin = {"ast-grep", "sg"},
    repo = "ast-grep/ast-grep", asset = {
      linux = { match = "app\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "app\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "app\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.45.3", -- snapshot pin
  },
  {
    id = "astro-language-server", display = "astro-language-server", detail = "The Astro language server, its structure is inspired by the Svelte Language Server.", manager = "npm", pkg = "%40astrojs/language-server",
    categories = {"LSP"},
    languages = {"Astro"},
    bin = {"astro-ls"},
    version = "2.16.16", -- snapshot pin
  },
  {
    id = "autoflake", display = "autoflake", detail = "autoflake removes unused imports and unused variables from Python code.", manager = "pypi", pkg = "autoflake",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"autoflake"},
    version = "2.4.0", -- snapshot pin
  },
  {
    id = "autohotkey_lsp", display = "autohotkey_lsp", detail = "Autohotkey v2 Language Support using vscode-lsp.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"AutoHotkey"},
    bin = {"autohotkey_lsp"},
    repo = "thqby/vscode-autohotkey2-lsp", asset = {
      linux = { match = "vscode\\-autohotkey2\\-lsp\\-.*\\.vsix", archive = "none" },
      mac = { match = "vscode\\-autohotkey2\\-lsp\\-.*\\.vsix", archive = "none" },
      win = { match = "vscode\\-autohotkey2\\-lsp\\-.*\\.vsix", archive = "none" },
    },
    version = "v3.0.10", -- snapshot pin
    runs = {
      ["autohotkey_lsp"] = { kind = "node", hint = "extension/server/dist/server.js" },
    },
  },
  {
    id = "autopep8", display = "autopep8", detail = "A tool that automatically formats Python code to conform to the PEP 8 style guide.", manager = "pypi", pkg = "autopep8",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"autopep8"},
    version = "2.3.2", -- snapshot pin
  },
  {
    id = "autotools-language-server", display = "autotools-language-server", detail = "Autotools language server, support configure.ac, Makefile.am, Makefile.", manager = "pypi", pkg = "autotools-language-server",
    categories = {"LSP"},
    bin = {"autotools-language-server"},
    version = "0.1.3", -- snapshot pin
  },
  {
    id = "awk-language-server", display = "awk-language-server", detail = "Language Server for AWK.", manager = "npm", pkg = "awk-language-server",
    categories = {"LSP"},
    languages = {"AWK"},
    bin = {"awk-language-server"},
    version = "0.10.6", -- snapshot pin
  },
  {
    id = "azure-pipelines-language-server", display = "azure-pipelines-language-server", detail = "A language server for Azure Pipelines YAML.", manager = "npm", pkg = "azure-pipelines-language-server",
    categories = {"LSP"},
    languages = {"Azure Pipelines"},
    bin = {"azure-pipelines-language-server"},
    version = "0.9.2", -- snapshot pin
    runs = {
      ["azure-pipelines-language-server"] = { kind = "node", hint = "node_modules/azure-pipelines-language-server/out/server.js" },
    },
  },
  {
    id = "bacon", display = "bacon", detail = "Bacon is a background rust code checker", manager = "cargo", pkg = "bacon",
    categories = {"Linter"},
    languages = {"Rust"},
    bin = {"bacon"},
    version = "3.25.0", -- snapshot pin
  },
  {
    id = "bacon-ls", display = "bacon-ls", detail = "Rust diagnostic provider based on Bacon", manager = "cargo", pkg = "bacon-ls",
    categories = {"LSP"},
    languages = {"Rust"},
    bin = {"bacon-ls"},
    version = "0.29.0", -- snapshot pin
  },
  {
    id = "bandit", display = "bandit", detail = "Bandit, a security linter from PyCQA", manager = "pypi", pkg = "bandit",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"bandit"},
    version = "1.9.4", -- snapshot pin
  },
  {
    id = "basedpyright", display = "basedpyright", detail = "Fork of the Pyright static type checker for Python, with extra Pylance features.", manager = "pypi", pkg = "basedpyright",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"basedpyright", "basedpyright-langserver"},
    version = "1.39.10", -- snapshot pin
  },
  {
    id = "bash", display = "bash-language-server", detail = "A language server for Bash.", manager = "npm", pkg = "bash-language-server",
    categories = {"LSP"},
    languages = {"Bash", "Csh", "Ksh", "Sh", "Zsh"},
    aliases = {"sh", "shell", "bashls", "bash-language-server", "bash-language-server"},
    bin = {"bash-language-server"},
    version = "5.6.0", -- snapshot pin
  },
  {
    id = "bash-debug-adapter", display = "bash-debug-adapter", detail = "Bash shell debugger, based on bashdb.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"Bash"},
    bin = {"bash-debug-adapter"},
    repo = "rogalmic/vscode-bash-debug", asset = {
      linux = { match = "bash\\-debug\\-0\\.3\\.9\\.vsix", archive = "none" },
      mac = { match = "bash\\-debug\\-0\\.3\\.9\\.vsix", archive = "none" },
      win = { match = "bash\\-debug\\-0\\.3\\.9\\.vsix", archive = "none" },
    },
    version = "untagged-438733f35feb8659d939", -- snapshot pin
    runs = {
      ["bash-debug-adapter"] = { kind = "node", hint = "extension/out/bashDebug.js" },
    },
  },
  {
    id = "basics-language-server", display = "basics-language-server", detail = "Buffer, path, and snippet completions", manager = "npm", pkg = "basics-language-server",
    categories = {"LSP"},
    bin = {"basics-language-server"},
    version = "1.1.2", -- snapshot pin
  },
  {
    id = "bazelrc-lsp", display = "bazelrc-lsp", detail = "Language Server for `.bazelrc` configuration files", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"bazelrc"},
    bin = {"bazelrc-lsp"},
    repo = "salesforce-misc/bazelrc-lsp", asset = {
      linux = { match = "bazelrc\\-lsp\\-ubuntu", archive = "none" },
      mac = { match = "bazelrc\\-lsp\\-macos", archive = "none" },
      win = { match = "bazelrc\\-lsp\\-windows\\.exe", archive = "none" },
    },
    version = "v0.2.6", -- snapshot pin
  },
  {
    id = "beancount-language-server", display = "beancount-language-server", detail = "A Language Server Protocol (LSP) for beancount files.", manager = "cargo", pkg = "beancount-language-server",
    categories = {"LSP"},
    languages = {"Beancount"},
    bin = {"beancount-language-server"},
    extras = {["repository_url"] = "https://github.com/polarmutex/beancount-language-server"},
    version = "1.9.2", -- snapshot pin
  },
  {
    id = "beanhub-cli", display = "beanhub-cli", detail = "A simple beancount formatter that keeps comments.", manager = "pypi", pkg = "beanhub-cli",
    categories = {"Formatter"},
    languages = {"Beancount"},
    bin = {"bh"},
    version = "3.3.0", -- snapshot pin
  },
  {
    id = "beautysh", display = "beautysh", detail = "beautysh - A Bash beautifier for the masses.", manager = "pypi", pkg = "beautysh",
    categories = {"Formatter"},
    languages = {"Bash", "Csh", "Ksh", "Sh", "Zsh"},
    bin = {"beautysh"},
    version = "6.4.3", -- snapshot pin
  },
  {
    id = "bibtex-tidy", display = "bibtex-tidy", detail = "Cleaner and Formatter for BibTeX files", manager = "npm", pkg = "bibtex-tidy",
    categories = {"Formatter"},
    languages = {"LaTeX"},
    bin = {"bibtex-tidy"},
    version = "1.15.1", -- snapshot pin
  },
  {
    id = "bicep-lsp", display = "bicep-lsp", detail = "Bicep is a declarative language for describing and deploying Azure resources", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Bicep"},
    bin = {"bicep-lsp"},
    repo = "Azure/bicep", asset = {
      linux = { match = "vscode\\-bicep\\.vsix", archive = "none" },
      mac = { match = "vscode\\-bicep\\.vsix", archive = "none" },
      win = { match = "vscode\\-bicep\\.vsix", archive = "none" },
    },
    version = "v0.46.1", -- snapshot pin
    runs = {
      ["bicep-lsp"] = { kind = "dotnet", hint = "extension/bicepLanguageServer/Bicep.LangServer.dll" },
    },
  },
  {
    id = "biome", display = "biome", detail = "Toolchain of the web. Successor to Rome.", manager = "npm", pkg = "@biomejs/biome",
    categories = {"LSP", "Linter", "Formatter"},
    languages = {"JSON", "JavaScript", "TypeScript"},
    bin = {"biome"},
    version = "2.5.11", -- snapshot pin
  },
  {
    id = "black", display = "black", detail = "Black, the uncompromising Python code formatter.", manager = "pypi", pkg = "black",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"black"},
    version = "26.5.1", -- snapshot pin
  },
  {
    id = "blackd-client", display = "blackd-client", detail = "Tiny HTTP client for the Black (blackd) Python code formatter.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"blackd-client"},
    repo = "disrupted/blackd-client", asset = {
      linux = { match = "blackd\\-client_linux", archive = "none" },
      mac = { match = "blackd\\-client_macos", archive = "none" },
    },
    version = "v0.1.1", -- snapshot pin
  },
  {
    id = "blade-formatter", display = "blade-formatter", detail = "An opinionated blade template formatter for Laravel that respects readability.", manager = "npm", pkg = "blade-formatter",
    categories = {"Formatter"},
    languages = {"Blade"},
    bin = {"blade-formatter"},
    version = "1.44.4", -- snapshot pin
  },
  {
    id = "blue", display = "blue", detail = "blue is a somewhat less uncompromising code formatter than black, the OG of Python formatters. We love the idea of au...", manager = "pypi", pkg = "blue",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"blue"},
    version = "0.9.1", -- snapshot pin
  },
  {
    id = "bqls", display = "bqls", detail = "BigQuery language server", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"SQL"},
    bin = {"bqls"},
    repo = "kitagry/bqls", asset = {
      mac = { match = "bqls_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.7.1", -- snapshot pin
  },
  {
    id = "brighterscript", display = "brighterscript", detail = "A superset of Roku's BrightScript language.", manager = "npm", pkg = "brighterscript",
    categories = {"Compiler", "LSP"},
    languages = {"BrighterScript"},
    bin = {"bsc"},
    version = "0.73.0", -- snapshot pin
  },
  {
    id = "brighterscript-formatter", display = "brighterscript-formatter", detail = "A code formatter for BrightScript and BrighterScript.", manager = "npm", pkg = "brighterscript-formatter",
    categories = {"Formatter"},
    languages = {"BrighterScript"},
    bin = {"bsfmt"},
    version = "1.8.1", -- snapshot pin
  },
  {
    id = "brunette", display = "brunette", detail = "A best practice Python code formatter", manager = "pypi", pkg = "brunette",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"brunette"},
    version = "0.2.8", -- snapshot pin
  },
  {
    id = "bsl-language-server", display = "bsl-language-server", detail = "Implementation of Language Server Protocol for Language 1C (BSL).", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"1С:Enterprise", "OneScript"},
    bin = {"bsl-language-server"},
    repo = "1c-syntax/bsl-language-server", asset = {
      linux = { match = "bsl\\-language\\-server\\-.*\\-exec\\.jar", archive = "none" },
      mac = { match = "bsl\\-language\\-server\\-.*\\-exec\\.jar", archive = "none" },
      win = { match = "bsl\\-language\\-server\\-.*\\-exec\\.jar", archive = "none" },
    },
    version = "v1.0.7", -- snapshot pin
    runs = {
      ["bsl-language-server"] = { kind = "jar", hint = "{{source.asset.file}}" },
    },
  },
  {
    id = "bslint", display = "bslint", detail = "A BrighterScript CLI tool to lint your code without compiling your project.", manager = "npm", pkg = "%40rokucommunity/bslint",
    categories = {"Linter"},
    languages = {"BrighterScript"},
    bin = {"bslint"},
    version = "0.8.44", -- snapshot pin
  },
  {
    id = "buf", display = "buf", detail = "The Buf CLI is a one stop shop for your local Protocol Buffers needs. It comes with a linter that enforces good API d...", manager = "github", pkg = "",
    categories = {"Linter", "Formatter", "LSP"},
    languages = {"Protobuf"},
    bin = {"buf"},
    repo = "bufbuild/buf", asset = {
      mac = { match = "buf\\-Darwin\\-arm64", archive = "none" },
      win = { match = "buf\\-Windows\\-x86_64\\.exe", archive = "none" },
    },
    version = "v1.72.0", -- snapshot pin
  },
  {
    id = "buildifier", display = "buildifier", detail = "buildifier is a tool for formatting and linting bazel BUILD, WORKSPACE, and .bzl files.", manager = "github", pkg = "",
    categories = {"Linter", "Formatter"},
    languages = {"Bazel"},
    bin = {"buildifier"},
    repo = "bazelbuild/buildtools", asset = {
      mac = { match = "buildifier\\-darwin\\-arm64", archive = "none" },
      win = { match = "buildifier\\-windows\\-amd64\\.exe", archive = "none" },
    },
    version = "v8.5.1", -- snapshot pin
  },
  {
    id = "c3-lsp", display = "c3-lsp", detail = "c3-lsp is a language server for the c3 language, developed by pherrymason. It provides IDE features to any LSP-compat...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C3"},
    bin = {"c3lsp"},
    repo = "pherrymason/c3-lsp", asset = {
      linux = { match = "c3lsp\\-linux\\-amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "c3lsp\\-darwin\\-arm64\\.zip", archive = "zip" },
      win = { match = "c3lsp\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v0.4.0", -- snapshot pin
  },
  {
    id = "cairo-language-server", display = "cairo-language-server", detail = "Starknet Cairo language server.", manager = "cargo", pkg = "cairo-language-server",
    categories = {"LSP"},
    languages = {"Cairo"},
    bin = {"cairo-language-server"},
    version = "2.15.0", -- snapshot pin
  },
  {
    id = "cbfmt", display = "cbfmt", detail = "A tool to format codeblocks inside markdown and org documents. It iterates over all codeblocks, and formats them with...", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"cbfmt"},
    repo = "lukas-reineke/cbfmt", asset = {
      linux = { match = "cbfmt_linux\\-x86_64_.*\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "cbfmt_macos\\-x86_64_.*\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "cbfmt_windows\\-x86_64\\-msvc_.*\\.zip", archive = "zip" },
    },
    version = "v0.2.0", -- snapshot pin
  },
  {
    id = "cds-lsp", display = "cds-lsp", detail = "Language server for CDS", manager = "npm", pkg = "%40sap/cds-lsp",
    categories = {"LSP", "Formatter"},
    languages = {"CDS"},
    bin = {"cds-lsp", "format-cds"},
    version = "10.0.1", -- snapshot pin
  },
  {
    id = "cfn-lint", display = "cfn-lint", detail = "CloudFormation Linter. Validate AWS CloudFormation YAML/JSON templates against the AWS CloudFormation Resource Specif...", manager = "pypi", pkg = "cfn-lint",
    categories = {"Linter"},
    languages = {"YAML", "JSON", "CloudFormation"},
    bin = {"cfn-lint"},
    version = "1.56.0", -- snapshot pin
  },
  {
    id = "checkmake", display = "checkmake", detail = "checkmake is an experimental tool for linting and checking Makefiles. It may not do what you want it to.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Makefile"},
    bin = {"checkmake"},
    repo = "mrtazz/checkmake", asset = {
      mac = { match = "\\['checkmake\\-.*\\.darwin\\.amd64',\\ 'checkmake\\.1", archive = "none" },
      win = { match = "checkmake\\-.*\\.windows\\.amd64\\.exe", archive = "none" },
    },
    version = "v0.3.2", -- snapshot pin
  },
  {
    id = "checkstyle", display = "checkstyle", detail = "A tool for checking Java source code for adherence to a Code Standard or set of validation rules (best practices).", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Java"},
    bin = {"checkstyle"},
    repo = "checkstyle/checkstyle", asset = {
      linux = { match = ".*\\-all\\.jar", archive = "none" },
      mac = { match = ".*\\-all\\.jar", archive = "none" },
      win = { match = ".*\\-all\\.jar", archive = "none" },
    },
    version = "checkstyle-13.5.0", -- snapshot pin
    runs = {
      ["checkstyle"] = { kind = "jar", hint = "{{version}}-all.jar" },
    },
  },
  {
    id = "circleci-yaml-language-server", display = "circleci-yaml-language-server", detail = "Language server for CircleCI YAML configurations", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"YAML"},
    bin = {"circleci-yaml-language-server"},
    repo = "CircleCI-Public/circleci-yaml-language-server", asset = {
      linux = { match = "\\['linux\\-amd64\\-lsp',\\ 'schema\\.json'\\]", archive = "none" },
      mac = { match = "\\['darwin\\-arm64\\-lsp',\\ 'schema\\.json'\\]", archive = "none" },
      win = { match = "\\['windows\\-amd64\\-lsp\\.exe',\\ 'schema\\.json'\\]", archive = "none" },
    },
    version = "0.38.0", -- snapshot pin
  },
  {
    id = "circom-lsp", display = "circom-lsp", detail = "A Language Server Protocol Implementation for Circom", manager = "cargo", pkg = "circom-lsp",
    categories = {"LSP"},
    languages = {"Circom"},
    bin = {"circom-lsp"},
    version = "0.1.3", -- snapshot pin
  },
  {
    id = "clang-format", display = "clang-format", detail = "clang-format is formatter for C/C++/Java/JavaScript/JSON/Objective-C/Protobuf/C# code.", manager = "pypi", pkg = "clang-format",
    categories = {"Formatter"},
    languages = {"C", "C#", "C++", "JSON", "Java", "JavaScript"},
    bin = {"clang-format", "clang-format-diff.py", "git-clang-format"},
    version = "23.1.0", -- snapshot pin
  },
  {
    id = "clarinet", display = "clarinet", detail = "Clarinet is a simple, modern and opinionated runtime for testing, integrating and deploying Clarity smart contracts.", manager = "github", pkg = "",
    categories = {"LSP", "Runtime"},
    languages = {"Clarity"},
    bin = {"clarinet"},
    repo = "hirosystems/clarinet", asset = {
      mac = { match = "clarinet\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v15.16.0", -- snapshot pin
  },
  {
    id = "claude-agent-acp", display = "claude-agent-acp", detail = "ACP adapter for the Claude Agent SDK", manager = "npm", pkg = "@agentclientprotocol/claude-agent-acp",
    bin = {"claude-agent-acp"},
    version = "0.73.0", -- snapshot pin
  },
  {
    id = "clice", display = "clice", detail = "A next-generation C++ language server for modern C++, focused on high performance and deep code intelligence.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C", "C++"},
    bin = {"clice"},
    repo = "clice-io/clice", asset = {
      linux = { match = "clice\\-.*\\.x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "clice\\-.*\\.aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "clice\\-.*\\.x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.1.2026082603", -- snapshot pin
  },
  {
    id = "clj-kondo", display = "clj-kondo", detail = "Clj-kondo performs static analysis on Clojure, ClojureScript and EDN, without the need of a running REPL. It informs ...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"clj-kondo"},
    repo = "clj-kondo/clj-kondo", asset = {
      linux = { match = "clj\\-kondo\\-.*\\-linux\\-amd64\\.zip", archive = "zip" },
      mac = { match = "clj\\-kondo\\-.*\\-macos\\-aarch64\\.zip", archive = "zip" },
      win = { match = "clj\\-kondo\\-.*\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v2026.08.04", -- snapshot pin
  },
  {
    id = "cljfmt", display = "cljfmt", detail = "A tool for formatting Clojure code", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"cljfmt"},
    repo = "weavejester/cljfmt", asset = {
      linux = { match = "cljfmt\\-.*\\-linux\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "cljfmt\\-.*\\-darwin\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "cljfmt\\-.*\\-win\\-amd64\\.zip", archive = "zip" },
    },
    version = "0.16.5", -- snapshot pin
  },
  {
    id = "clojure-lsp", display = "clojure-lsp", detail = "A Language Server for Clojure(script). Taking a Cursive-like approach of statically analyzing code.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"clojure-lsp"},
    repo = "clojure-lsp/clojure-lsp", asset = {
      linux = { match = "clojure\\-lsp\\-native\\-linux\\-amd64\\.zip", archive = "zip" },
      mac = { match = "clojure\\-lsp\\-native\\-macos\\-aarch64\\.zip", archive = "zip" },
      win = { match = "clojure\\-lsp\\-native\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "2026.07.06-14.34.19", -- snapshot pin
  },
  {
    id = "cmake-language-server", display = "cmake-language-server", detail = "CMake LSP Implementation.", manager = "pypi", pkg = "cmake-language-server",
    categories = {"LSP"},
    languages = {"CMake"},
    bin = {"cmake-language-server"},
    version = "0.1.11", -- snapshot pin
  },
  {
    id = "cmakelang", display = "cmakelang", detail = "Language tools for cmake (format, lint, etc).", manager = "pypi", pkg = "cmakelang",
    categories = {"Formatter", "Linter"},
    languages = {"CMake"},
    bin = {"cmake-annotate", "cmake-format", "cmake-lint", "ctest-to"},
    extras = {["extra"] = "YAML"},
    version = "0.6.13", -- snapshot pin
  },
  {
    id = "cmakelint", display = "cmakelint", detail = "cmakelint parses CMake files and reports style issues.", manager = "pypi", pkg = "cmakelint",
    categories = {"Linter"},
    languages = {"CMake"},
    bin = {"cmakelint"},
    version = "1.4.3", -- snapshot pin
  },
  {
    id = "cobol-language-support", display = "cobol-language-support", detail = "COBOL Language Support provides autocomplete, highlighting and diagnostics for COBOL code and copybooks", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"COBOL"},
    bin = {"cobol-language-support"},
    repo = "eclipse-che4z/che-che4z-lsp-for-cobol", asset = {
      mac = { match = "cobol\\-language\\-support\\-darwin\\-arm64\\-.*\\.vsix", archive = "none" },
      win = { match = "cobol\\-language\\-support\\-win32\\-x64\\-.*\\.vsix", archive = "none" },
    },
    version = "2.5.1", -- snapshot pin
  },
  {
    id = "codebook", display = "codebook", detail = "An unholy spell checker for code.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C", "CSS", "Go", "HTML", "Haskell", "Java", "JavaScript", "Lua", "Markdown", "PHP", "Plain", "Python", "Ruby", "Rust", "TOML", "TypeScript"},
    bin = {"codebook-lsp"},
    repo = "blopker/codebook", asset = {
      linux = { match = "codebook\\-lsp\\-x86_64\\-unknown\\-linux\\-musl\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "codebook\\-lsp\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "codebook\\-lsp\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.3.42", -- snapshot pin
  },
  {
    id = "codelldb", display = "codelldb", detail = "A native debugger based on LLDB.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"C", "C++", "Rust", "Zig"},
    bin = {"codelldb"},
    repo = "vadimcn/vscode-lldb", asset = {
      linux = { match = "codelldb\\-linux\\-x64\\.vsix", archive = "none" },
      mac = { match = "codelldb\\-darwin\\-arm64\\.vsix", archive = "none" },
      win = { match = "codelldb\\-win32\\-x64\\.vsix", archive = "none" },
    },
    version = "v1.12.3", -- snapshot pin
  },
  {
    id = "codeql", display = "codeql", detail = "Discover vulnerabilities across a codebase with CodeQL, our industry-leading semantic code analysis engine. CodeQL le...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"CodeQL"},
    bin = {"codeql"},
    repo = "github/codeql-cli-binaries", asset = {
      mac = { match = "codeql\\-osx64\\.zip", archive = "zip" },
      win = { match = "codeql\\-win64\\.zip", archive = "zip" },
    },
    version = "v2.26.4", -- snapshot pin
  },
  {
    id = "codespell", display = "codespell", detail = "Check code for common misspellings.", manager = "pypi", pkg = "codespell",
    categories = {"Linter"},
    bin = {"codespell"},
    version = "2.4.3", -- snapshot pin
  },
  {
    id = "codex-acp", display = "codex-acp", detail = "ACP adapter for Codex CLI", manager = "npm", pkg = "@agentclientprotocol/codex-acp",
    bin = {"codex-acp"},
    version = "1.8.0", -- snapshot pin
  },
  {
    id = "coffeesense-language-server", display = "coffeesense-language-server", detail = "Language server for CoffeeScript.", manager = "npm", pkg = "coffeesense-language-server",
    categories = {"LSP"},
    languages = {"CoffeeScript"},
    bin = {"coffeesense-language-server"},
    version = "1.15.0", -- snapshot pin
  },
  {
    id = "colorgen-nvim", display = "colorgen-nvim", detail = "Blazingly fast colorscheme generator for Neovim written in Rust.", manager = "cargo", pkg = "colorgen-nvim",
    categories = {"Compiler"},
    bin = {"colorgen-nvim"},
    extras = {["repository_url"] = "https://github.com/ChristianChiarulli/colorgen-nvim"},
    version = "0.1.0", -- snapshot pin
  },
  {
    id = "commitlint", display = "commitlint", detail = "commitlint checks if your commit messages meet the conventional commit format.", manager = "npm", pkg = "%40commitlint/cli",
    categories = {"Linter"},
    bin = {"commitlint"},
    version = "21.2.2", -- snapshot pin
  },
  {
    id = "contextive", display = "contextive", detail = "Supports developers where a complex domain or project specific language is in use by surfacing definitions everywhere...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"F#"},
    bin = {"Contextive.LanguageServer"},
    repo = "dev-cycles/contextive", asset = {
      linux = { match = "Contextive\\.LanguageServer\\-linux\\-x64\\-.*\\.zip", archive = "zip" },
      mac = { match = "Contextive\\.LanguageServer\\-osx\\-arm64\\-.*\\.zip", archive = "zip" },
      win = { match = "Contextive\\.LanguageServer\\-win\\-x64\\-.*\\.zip", archive = "zip" },
    },
    version = "v1.17.8", -- snapshot pin
  },
  {
    id = "copilot-language-server", display = "copilot-language-server", detail = "The Copilot Language Server enables any editor or IDE to integrate with GitHub Copilot via the language server protocol.", manager = "npm", pkg = "%40github/copilot-language-server",
    categories = {"LSP"},
    bin = {"copilot-language-server"},
    version = "1.539.0", -- snapshot pin
  },
  {
    id = "coq-lsp", display = "coq-lsp", detail = "Visual Studio Code Extension and Language Server Protocol for coq", manager = "opam", pkg = "coq-lsp",
    categories = {"LSP"},
    languages = {"Coq"},
    bin = {"coq-lsp"},
    version = "0.1.8+8.19", -- snapshot pin
  },
  {
    id = "cortex-debug", display = "cortex-debug", detail = "Visual Studio Code extension for enhancing debug capabilities for Cortex-M Microcontrollers.", manager = "openvsx", pkg = "",
    categories = {"DAP"},
    languages = {"C", "C++", "Rust"},
    bin = {},
    openvsx = { ns = "marus25", ext = "cortex-debug", version = "1.12.1", file = "marus25.cortex-debug-{{version}}.vsix" },
    version = "1.12.1", -- snapshot pin
  },
  {
    id = "cpp", display = "clangd", detail = "clangd understands your C++ code and adds smart features to your editor: code completion, compile errors, go-to-defin...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C", "C++"},
    aliases = {"c", "c++", "clangd", "clangd"},
    bin = {"clangd"},
    repo = "clangd/clangd", asset = {
      linux = { match = "clangd\\-linux\\-.*\\.zip", archive = "zip" },
      mac = { match = "clangd\\-mac\\-.*\\.zip", archive = "zip" },
      win = { match = "clangd\\-windows\\-.*\\.zip", archive = "zip" },
    },
    version = "22.1.6", -- snapshot pin
  },
  {
    id = "cpplint", display = "cpplint", detail = "Cpplint is a command-line tool to check C/C++ files for style issues following Google's C++ style guide.", manager = "pypi", pkg = "cpplint",
    categories = {"Linter"},
    languages = {"C", "C++"},
    bin = {"cpplint"},
    version = "2.0.2", -- snapshot pin
  },
  {
    id = "cpptools", display = "cpptools", detail = "Official repository for the Microsoft C/C++ extension for VS Code.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"C", "C++", "Rust"},
    bin = {"OpenDebugAD7"},
    repo = "microsoft/vscode-cpptools", asset = {
      mac = { match = "cpptools\\-macOS\\-arm64\\.vsix", archive = "none" },
      win = { match = "cpptools\\-windows\\-x64\\.vsix", archive = "none" },
    },
    version = "v1.33.8", -- snapshot pin
  },
  {
    id = "cqlls", display = "cqlls", detail = "The Best Open Source Language Server for CQL (Cassandra Query Language) ^_^", manager = "cargo", pkg = "cqlls",
    categories = {"LSP"},
    languages = {"CQL"},
    bin = {"cqlls"},
    version = "4.1.0", -- snapshot pin
  },
  {
    id = "crlfmt", display = "crlfmt", detail = "Formatter for Go code that enforces the CockroachDB Style Guide.", manager = "golang", pkg = "github.com/cockroachdb/crlfmt",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"crlfmt"},
    version = "v0.5.0", -- snapshot pin
  },
  {
    id = "cronstrue", display = "cronstrue", detail = "JavaScript library that translates Cron expressions into human readable descriptions.", manager = "npm", pkg = "cronstrue",
    bin = {"cronstrue"},
    version = "3.24.0", -- snapshot pin
  },
  {
    id = "crystalline", display = "crystalline", detail = "A Language Server Protocol implementation for Crystal.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Crystal"},
    bin = {"crystalline"},
    repo = "elbywan/crystalline", asset = {
      mac = { match = "crystalline_arm64\\-apple\\-darwin\\.gz", archive = "gz" },
    },
    version = "v0.19.0", -- snapshot pin
  },
  {
    id = "csharp-language-server", display = "csharp-language-server", detail = "Roslyn-based LSP language server for C#.", manager = "nuget", pkg = "csharp-ls",
    categories = {"LSP"},
    languages = {"C#"},
    bin = {"csharp-ls"},
    version = "0.24.0", -- snapshot pin
  },
  {
    id = "csharpier", display = "csharpier", detail = "CSharpier is an opinionated code formatter for C#.", manager = "nuget", pkg = "csharpier",
    categories = {"Formatter"},
    languages = {"C#"},
    bin = {"csharpier"},
    version = "1.2.6", -- snapshot pin
  },
  {
    id = "cspell", display = "cspell", detail = "A Spell Checker for Code.", manager = "npm", pkg = "cspell",
    categories = {"Linter"},
    bin = {"cspell"},
    version = "10.2.0", -- snapshot pin
  },
  {
    id = "cspell-lsp", display = "cspell-lsp", detail = "Language Server Protocol implementation for CSpell, a spell checker for code.", manager = "npm", pkg = "@vlabo/cspell-lsp",
    categories = {"LSP"},
    bin = {"cspell-lsp"},
    version = "1.1.5", -- snapshot pin
  },
  {
    id = "css", display = "css-lsp", detail = "Language Server Protocol implementation for CSS, SCSS & LESS.", manager = "npm", pkg = "vscode-langservers-extracted",
    categories = {"LSP"},
    languages = {"CSS", "SCSS", "LESS"},
    aliases = {"css-language-server", "css-lsp"},
    bin = {"vscode-css-language-server"},
    version = "4.10.0", -- snapshot pin
  },
  {
    id = "css-variables-language-server", display = "css-variables-language-server", detail = "Autocompletion and go-to-definition for project-wide CSS variables.", manager = "npm", pkg = "css-variables-language-server",
    categories = {"LSP"},
    languages = {"CSS", "SCSS", "LESS"},
    bin = {"css-variables-language-server"},
    version = "2.8.4", -- snapshot pin
  },
  {
    id = "csskit", display = "csskit", detail = "Beautiful, fast, and powerful CSS tooling with zero configuration", manager = "github", pkg = "",
    categories = {"LSP", "Formatter", "Linter"},
    languages = {"CSS"},
    bin = {"csskit"},
    repo = "csskit/csskit", asset = {
      linux = { match = "csskit\\-linux\\-x64", archive = "none" },
      mac = { match = "csskit\\-darwin\\-arm64", archive = "none" },
      win = { match = "csskit\\-win32\\-x64\\.exe", archive = "none" },
    },
    version = "v0.0.31", -- snapshot pin
  },
  {
    id = "cssmodules-language-server", display = "cssmodules-language-server", detail = "Autocompletion and go-to-definition for cssmodules.", manager = "npm", pkg = "cssmodules-language-server",
    categories = {"LSP"},
    languages = {"CSS"},
    bin = {"cssmodules-language-server"},
    version = "1.5.2", -- snapshot pin
  },
  {
    id = "ctags-lsp", display = "ctags-lsp", detail = "A simple LSP server wrapping universal-ctags. Provides code completion, go-to-definition, and document/workspace symb...", manager = "github", pkg = "",
    categories = {"LSP"},
    bin = {"ctags-lsp"},
    repo = "netmute/ctags-lsp", asset = {
      mac = { match = "ctags\\-lsp_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ctags\\-lsp_Windows_x86_64\\.zip", archive = "zip" },
    },
    version = "v0.11.0", -- snapshot pin
  },
  {
    id = "cucumber-language-server", display = "cucumber-language-server", detail = "Cucumber Language Server.", manager = "npm", pkg = "%40cucumber/language-server",
    categories = {"LSP"},
    languages = {"Cucumber"},
    bin = {"cucumber-language-server"},
    version = "1.7.0", -- snapshot pin
  },
  {
    id = "cue", display = "cue", detail = "The Official Language Server implementation for CUE.", manager = "golang", pkg = "cuelang.org/go",
    categories = {"LSP"},
    languages = {"Cue"},
    bin = {"cue"},
    version = "v0.17.1#cmd/cue", -- snapshot pin
  },
  {
    id = "cueimports", display = "cueimports", detail = "CUE tool that updates your import lines, adding missing ones and removing unused ones.", manager = "golang", pkg = "github.com/asdine/cueimports",
    categories = {"Formatter"},
    languages = {"Cue"},
    bin = {"cueimports"},
    version = "v0.3.2#cmd/cueimports", -- snapshot pin
  },
  {
    id = "cuelsp", display = "cuelsp", detail = "Language Server implementation for CUE, with built-in support for Dagger.", manager = "golang", pkg = "github.com/dagger/cuelsp",
    categories = {"LSP"},
    languages = {"Cue"},
    bin = {"cuelsp"},
    version = "v0.3.4#cmd/cuelsp", -- snapshot pin
  },
  {
    id = "curlylint", display = "curlylint", detail = "Experimental HTML templates linting for Jinja, Nunjucks, Django templates, Twig, Liquid.", manager = "pypi", pkg = "curlylint",
    categories = {"Linter"},
    languages = {"Django", "Jinja", "Liquid", "Nunjucks", "Twig"},
    bin = {"curlylint"},
    version = "0.13.1", -- snapshot pin
  },
  {
    id = "custom-elements-languageserver", display = "custom-elements-languageserver", detail = "Custom Elements Language Server provides useful language features for Web Components. Features include code actions, ...", manager = "npm", pkg = "custom-elements-languageserver",
    categories = {"LSP"},
    bin = {"custom-elements-languageserver"},
    version = "1.0.4", -- snapshot pin
  },
  {
    id = "cypher-language-server", display = "cypher-language-server", detail = "Language Server for Cypher query language.", manager = "npm", pkg = "%40neo4j-cypher/language-server",
    categories = {"LSP"},
    languages = {"Cypher"},
    bin = {"cypher-language-server"},
    version = "0.0.0-canary-20250423075344", -- snapshot pin
  },
  {
    id = "darker", display = "darker", detail = "Apply black reformatting to Python files only in regions changed since a given commit.", manager = "pypi", pkg = "darker",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"darker"},
    extras = {["extra"] = "black,isort,flynt"},
    version = "3.0.0", -- snapshot pin
  },
  {
    id = "dart-debug-adapter", display = "dart-debug-adapter", detail = "Dart debug adapter sourced from the Dart VSCode extension.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"Dart"},
    bin = {"dart-debug-adapter"},
    repo = "Dart-Code/Dart-Code", asset = {
      linux = { match = "dart\\-code\\-.*\\.vsix", archive = "none" },
      mac = { match = "dart\\-code\\-.*\\.vsix", archive = "none" },
      win = { match = "dart\\-code\\-.*\\.vsix", archive = "none" },
    },
    version = "v3.142.0", -- snapshot pin
    runs = {
      ["dart-debug-adapter"] = { kind = "node", hint = "extension/out/dist/debug.js" },
    },
  },
  {
    id = "dcm", display = "dcm", detail = "Language server for DCM analyzer", manager = "github", pkg = "",
    categories = {"LSP", "Formatter", "Linter"},
    languages = {"Dart"},
    bin = {"dcm"},
    repo = "CQLabs/homebrew-dcm", asset = {
      mac = { match = "dcm\\-macos\\-arm\\-release\\.zip", archive = "zip" },
      win = { match = "dcm\\-windows\\-release\\.zip", archive = "zip" },
    },
    version = "1.39.2", -- snapshot pin
  },
  {
    id = "debugpy", display = "debugpy", detail = "An implementation of the Debug Adapter Protocol for Python.", manager = "pypi", pkg = "debugpy",
    categories = {"DAP"},
    languages = {"Python"},
    bin = {"debugpy", "debugpy-adapter"},
    version = "1.8.21", -- snapshot pin
    runs = {
      ["debugpy"] = { kind = "pyvenv", hint = "debugpy" },
      ["debugpy-adapter"] = { kind = "pyvenv", hint = "debugpy.adapter" },
    },
  },
  {
    id = "delve", display = "delve", detail = "Delve is a debugger for the Go programming language.", manager = "golang", pkg = "github.com/go-delve/delve",
    categories = {"DAP"},
    languages = {"Go"},
    bin = {"dlv"},
    version = "v1.27.1#cmd/dlv", -- snapshot pin
  },
  {
    id = "deno", display = "deno", detail = "Deno (/ˈdiːnoʊ/, pronounced dee-no) is a JavaScript, TypeScript, and WebAssembly runtime with secure defaults and a g...", manager = "github", pkg = "",
    categories = {"LSP", "Runtime", "Linter"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"deno"},
    repo = "denoland/deno", asset = {
      linux = { match = "deno\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "deno\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "deno\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v2.9.6", -- snapshot pin
  },
  {
    id = "dexter", display = "dexter", detail = "A fast, full-featured Elixir LSP optimized for large codebases.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Elixir"},
    bin = {"dexter"},
    repo = "remoteoss/dexter", asset = {
      mac = { match = "dexter_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.7.1", -- snapshot pin
  },
  {
    id = "dhall-lsp", display = "dhall-lsp", detail = "LSP server implementation for Dhall.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Dhall"},
    bin = {"dhall-lsp-server"},
    repo = "dhall-lang/dhall-haskell", asset = {
      mac = { match = "dhall\\-lsp\\-server\\-1\\.1\\.4\\-x86_64\\-darwin\\.tar\\.bz2", archive = "none" },
      win = { match = "dhall\\-lsp\\-server\\-1\\.1\\.4\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "1.42.2", -- snapshot pin
  },
  {
    id = "diagnostic-languageserver", display = "diagnostic-languageserver", detail = "Diagnostic language server that integrates with linters.", manager = "npm", pkg = "diagnostic-languageserver",
    categories = {"LSP"},
    bin = {"diagnostic-languageserver"},
    version = "1.15.0", -- snapshot pin
  },
  {
    id = "dingo", display = "dingo", detail = "Dingo is a meta-language for Go that adds enhanced type safety and modern syntax (enums, pattern matching, error prop...", manager = "golang", pkg = "github.com/MadAppGang/dingo/cmd/dingo",
    categories = {"Compiler", "Formatter", "Linter"},
    languages = {"Dingo", "Go"},
    bin = {"dingo"},
    version = "v0.14.0", -- snapshot pin
  },
  {
    id = "dingo-lsp", display = "dingo-lsp", detail = "Language Server Protocol implementation for Dingo. Wraps gopls and translates positions between .dingo and .go files ...", manager = "golang", pkg = "github.com/MadAppGang/dingo/cmd/dingo-lsp",
    categories = {"LSP"},
    languages = {"Dingo"},
    bin = {"dingo-lsp"},
    version = "v0.14.0", -- snapshot pin
  },
  {
    id = "django-language-server", display = "django-language-server", detail = "A language server for the Django web framework", manager = "pypi", pkg = "django-language-server",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"djls"},
    version = "6.1.0", -- snapshot pin
  },
  {
    id = "django-template-lsp", display = "django-template-lsp", detail = "A language server for Django templates.", manager = "pypi", pkg = "django-template-lsp",
    categories = {"LSP"},
    languages = {"Python", "Django", "HTML"},
    bin = {"djlsp"},
    version = "1.3.1", -- snapshot pin
  },
  {
    id = "djlint", display = "djlint", detail = "HTML Template Linter and Formatter. Django - Jinja - Nunjucks - Handlebars - GoLang.", manager = "pypi", pkg = "djlint",
    categories = {"Formatter", "Linter"},
    languages = {"Django", "Go", "Nunjucks", "Twig", "Handlebars", "Mustache", "Angular", "Jinja"},
    bin = {"djlint"},
    version = "1.45.0", -- snapshot pin
  },
  {
    id = "docformatter", display = "docformatter", detail = "docformatter automatically formats docstrings to follow a subset of the PEP 257 conventions.", manager = "pypi", pkg = "docformatter",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"docformatter"},
    version = "1.7.8", -- snapshot pin
  },
  {
    id = "docker-compose-language-service", display = "docker-compose-language-service", detail = "A language server for Docker Compose.", manager = "npm", pkg = "%40microsoft/compose-language-service",
    categories = {"LSP"},
    languages = {"Docker"},
    bin = {"docker-compose-langserver"},
    version = "1.0.0", -- snapshot pin
  },
  {
    id = "docker-language-server", display = "docker-language-server", detail = "Language server for Dockerfiles, Compose files, and Bake files.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Docker"},
    bin = {"docker-language-server"},
    repo = "docker/docker-language-server", asset = {
      linux = { match = "docker\\-language\\-server\\-linux\\-amd64\\-.*", archive = "none" },
      mac = { match = "docker\\-language\\-server\\-darwin\\-arm64\\-.*", archive = "none" },
      win = { match = "docker\\-language\\-server\\-windows\\-amd64\\-.*\\.exe", archive = "none" },
    },
    version = "v0.20.1", -- snapshot pin
  },
  {
    id = "dockerfile", display = "Dockerfile", detail = "dockerfile-language-server-nodejs (docker-langserver)", manager = "npm", pkg = "dockerfile-language-server-nodejs",
    categories = {"LSP"},
    languages = {"Dockerfile"},
    aliases = {"docker"},
    bin = {"docker-langserver"},
    win_cmd = "npm install -g dockerfile-language-server-nodejs", win_remove_cmd = "npm uninstall -g dockerfile-language-server-nodejs",
  },
  {
    id = "dockerfile-language-server", display = "dockerfile-language-server", detail = "A language server for Dockerfiles powered by Node.js, TypeScript, and VSCode technologies.", manager = "npm", pkg = "dockerfile-language-server-nodejs",
    categories = {"LSP"},
    languages = {"Docker"},
    bin = {"docker-langserver"},
    version = "0.15.0", -- snapshot pin
  },
  {
    id = "dockerfmt", display = "dockerfmt", detail = "An opinionated Dockerfile formatter.", manager = "golang", pkg = "github.com/reteps/dockerfmt",
    categories = {"Formatter"},
    languages = {"Dockerfile"},
    bin = {"dockerfmt"},
    version = "v0.5.4", -- snapshot pin
  },
  {
    id = "doctoc", display = "doctoc", detail = "API and CLI for generating a markdown TOC (table of contents) for a README or any markdown files.", manager = "npm", pkg = "doctoc",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"doctoc"},
    version = "2.5.0", -- snapshot pin
  },
  {
    id = "dot-language-server", display = "dot-language-server", detail = "A language server for the DOT language.", manager = "npm", pkg = "dot-language-server",
    categories = {"LSP"},
    languages = {"DOT"},
    bin = {"dot-language-server"},
    version = "3.2.0", -- snapshot pin
  },
  {
    id = "dotenv-linter", display = "dotenv-linter", detail = "⚡️Lightning-fast linter for .env files. Written in Rust 🦀", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Dotenv"},
    bin = {"dotenv-linter"},
    repo = "dotenv-linter/dotenv-linter", asset = {
      linux = { match = "dotenv\\-linter\\-linux\\-x86_64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "dotenv\\-linter\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "dotenv\\-linter\\-win\\-x64\\.zip", archive = "zip" },
    },
    version = "v4.0.0", -- snapshot pin
  },
  {
    id = "dprint", display = "dprint", detail = "A pluggable and configurable code formatting platform written in Rust.", manager = "github", pkg = "",
    categories = {"Formatter", "LSP"},
    bin = {"dprint"},
    repo = "dprint/dprint", asset = {
      linux = { match = "dprint\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "dprint\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "dprint\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.57.1", -- snapshot pin
  },
  {
    id = "drools-lsp", display = "drools-lsp", detail = "An implementation of a language server for the Drools Rule Language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Drools"},
    bin = {},
    repo = "kiegroup/drools-lsp", asset = {
      linux = { match = "drools\\-lsp\\-server\\-jar\\-with\\-dependencies\\.jar", archive = "none" },
      mac = { match = "drools\\-lsp\\-server\\-jar\\-with\\-dependencies\\.jar", archive = "none" },
      win = { match = "drools\\-lsp\\-server\\-jar\\-with\\-dependencies\\.jar", archive = "none" },
    },
    version = "latest", -- snapshot pin
  },
  {
    id = "duster", display = "duster", detail = "Automatic configuration for Laravel apps to apply Tighten's standard linting & code standards.", manager = "composer", pkg = "tightenco/duster",
    categories = {"Formatter", "Linter"},
    languages = {"PHP", "Blade"},
    bin = {"duster"},
    version = "v3.4.7", -- snapshot pin
  },
  {
    id = "earthlyls", display = "earthlyls", detail = "A fast language server for earthly.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Earthly"},
    bin = {"earthlyls"},
    repo = "glehmann/earthlyls", asset = {
      mac = { match = "earthlyls\\-.*\\-macos\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "earthlyls\\-.*\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "0.5.5", -- snapshot pin
  },
  {
    id = "editorconfig-checker", display = "editorconfig-checker", detail = "A tool to verify that your files are in harmony with your `.editorconfig`.", manager = "github", pkg = "",
    categories = {"Linter"},
    bin = {"editorconfig-checker"},
    repo = "editorconfig-checker/editorconfig-checker", asset = {
      mac = { match = "ec\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ec\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v3.11.2", -- snapshot pin
  },
  {
    id = "efm", display = "efm", detail = "General purpose Language Server.", manager = "github", pkg = "",
    categories = {"LSP"},
    bin = {"efm-langserver"},
    repo = "mattn/efm-langserver", asset = {
      mac = { match = "efm\\-langserver_.*_darwin_arm64\\.zip", archive = "zip" },
      win = { match = "efm\\-langserver_.*_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.0.57", -- snapshot pin
  },
  {
    id = "elm-format", display = "elm-format", detail = "elm-format formats Elm source code according to a standard set of rules based on the official Elm Style Guide", manager = "npm", pkg = "elm-format",
    categories = {"Formatter"},
    languages = {"Elm"},
    bin = {"elm-format"},
    version = "0.8.8", -- snapshot pin
  },
  {
    id = "elm-language-server", display = "elm-language-server", detail = "Language server implementation for Elm.", manager = "npm", pkg = "%40elm-tooling/elm-language-server",
    categories = {"LSP"},
    languages = {"Elm"},
    bin = {"elm-language-server"},
    version = "2.8.0", -- snapshot pin
  },
  {
    id = "elp", display = "elp", detail = "ELP integrates Erlang into modern IDEs via the language server protocol.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Erlang"},
    bin = {"elp"},
    repo = "WhatsApp/erlang-language-platform", asset = {
      linux = { match = "elp\\-linux\\-x86_64\\-unknown\\-linux\\-gnu\\-otp\\-27\\.3\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "elp\\-macos\\-aarch64\\-apple\\-darwin\\-otp\\-27\\.3\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "2026-08-10", -- snapshot pin
  },
  {
    id = "ember-language-server", display = "ember-language-server", detail = "Language Server Protocol implementation for Ember.js and Glimmer projects.", manager = "npm", pkg = "%40ember-tooling/ember-language-server",
    categories = {"LSP"},
    languages = {"Ember"},
    bin = {"ember-language-server"},
    version = "2.30.9", -- snapshot pin
  },
  {
    id = "emmet-language-server", display = "emmet-language-server", detail = "A language server for emmet.io.", manager = "npm", pkg = "@olrtg/emmet-language-server",
    categories = {"LSP"},
    languages = {"Emmet"},
    bin = {"emmet-language-server"},
    version = "2.8.0", -- snapshot pin
  },
  {
    id = "emmet-ls", display = "emmet-ls", detail = "Emmet support based on LSP.", manager = "npm", pkg = "emmet-ls",
    categories = {"LSP"},
    languages = {"Emmet"},
    bin = {"emmet-ls"},
    version = "0.4.2", -- snapshot pin
  },
  {
    id = "emmylua-codeformat", display = "emmylua-codeformat", detail = "Fast, powerful, and feature-rich Lua formatting and checking tool.  This tool is already bundled with lua_ls, so you ...", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Lua"},
    bin = {"emmylua-codeformat"},
    repo = "CppCXY/EmmyLuaCodeStyle", asset = {
      linux = { match = "linux\\-x64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "win32\\-x64\\.zip", archive = "zip" },
    },
    version = "1.6.0", -- snapshot pin
  },
  {
    id = "emmylua_ls", display = "emmylua_ls", detail = "The language server for Lua, offering extensive features for different Lua versions.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Lua"},
    bin = {"emmylua_ls"},
    repo = "CppCXY/emmylua-analyzer-rust", asset = {
      linux = { match = "emmylua_ls\\-linux\\-aarch64\\-glibc\\.2\\.17\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "emmylua_ls\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "emmylua_ls\\-win32\\-x64\\.zip", archive = "zip" },
    },
    version = "0.25.1", -- snapshot pin
  },
  {
    id = "erb-formatter", display = "erb-formatter", detail = "Format ERB files with speed and precision.", manager = "gem", pkg = "erb-formatter",
    categories = {"Formatter"},
    languages = {"HTML", "Ruby"},
    bin = {"erb-format"},
    version = "0.7.3", -- snapshot pin
  },
  {
    id = "erb-lint", display = "erb-lint", detail = "erb-lint is a tool to help lint your ERB or HTML files using the included linters or by writing your own.", manager = "gem", pkg = "erb_lint",
    categories = {"Linter"},
    languages = {"HTML", "Ruby"},
    bin = {"erblint"},
    version = "0.9.0", -- snapshot pin
  },
  {
    id = "erg", display = "erg", detail = "A statically typed language that can deeply improve the Python ecosystem.", manager = "github", pkg = "",
    categories = {"Compiler", "LSP"},
    languages = {"Erg"},
    bin = {"erg"},
    repo = "erg-lang/erg", asset = {
      linux = { match = "erg\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "erg\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "erg\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.6.53", -- snapshot pin
  },
  {
    id = "erg-language-server", display = "erg-language-server", detail = "ELS is a language server for the Erg programing language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Erg"},
    bin = {"els"},
    repo = "erg-lang/erg-language-server", asset = {
      linux = { match = "els\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "els\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "els\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.1.12", -- snapshot pin
  },
  {
    id = "esbonio", display = "esbonio", detail = "A Language Server for Sphinx projects.", manager = "pypi", pkg = "esbonio",
    categories = {"LSP"},
    languages = {"Sphinx"},
    bin = {"esbonio"},
    version = "2.1.0", -- snapshot pin
  },
  {
    id = "eslint-lsp", display = "eslint-lsp", detail = "Language Server Protocol implementation for ESLint. The server uses the ESLint library installed in the opened worksp...", manager = "npm", pkg = "vscode-langservers-extracted",
    categories = {"LSP"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"vscode-eslint-language-server"},
    version = "4.10.0", -- snapshot pin
  },
  {
    id = "eslint_d", display = "eslint_d", detail = "Makes eslint the fastest linter on the planet.", manager = "npm", pkg = "eslint_d",
    categories = {"Linter"},
    languages = {"TypeScript", "JavaScript"},
    bin = {"eslint_d"},
    version = "15.0.3", -- snapshot pin
  },
  {
    id = "eugene", display = "eugene", detail = "Helps you write zero downtime schema migrations for postgres.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"SQL"},
    bin = {"eugene"},
    repo = "kaaveland/eugene", asset = {
      mac = { match = "eugene\\-aarch64\\-apple\\-darwin", archive = "none" },
    },
    version = "0.8.3", -- snapshot pin
  },
  {
    id = "expert", display = "expert", detail = "Expert is the official language server implementation for the Elixir programming language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Elixir"},
    bin = {"expert"},
    repo = "elixir-lang/expert", asset = {
      linux = { match = "expert_linux_amd64", archive = "none" },
      mac = { match = "expert_darwin_arm64", archive = "none" },
      win = { match = "expert_windows_amd64\\.exe", archive = "none" },
    },
    version = "v0.1.9", -- snapshot pin
  },
  {
    id = "facility-language-server", display = "facility-language-server", detail = "Facility Service Definition language. This version of Facility Service Definition requires dotnet (.NET 6.0) to be in...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Facility Service Definition"},
    bin = {"facility-language-server"},
    repo = "FacilityApi/FacilityLanguageServer", asset = {
      linux = { match = "Facility\\.LanguageServer\\.zip", archive = "zip" },
      mac = { match = "Facility\\.LanguageServer\\.zip", archive = "zip" },
      win = { match = "Facility\\.LanguageServer\\.zip", archive = "zip" },
    },
    version = "v2.5.1", -- snapshot pin
    runs = {
      ["facility-language-server"] = { kind = "dotnet", hint = "libexec/Facility.LanguageServer.dll" },
    },
  },
  {
    id = "fantomas", display = "fantomas", detail = "Fantomas is an opinionated code formatter for F#.", manager = "nuget", pkg = "fantomas",
    categories = {"Formatter"},
    languages = {"F#"},
    bin = {"fantomas"},
    version = "7.0.5", -- snapshot pin
  },
  {
    id = "fennel-language-server", display = "fennel-language-server", detail = "Fennel language server protocol (LSP) support.", manager = "cargo", pkg = "fennel-language-server",
    categories = {"LSP"},
    languages = {"Fennel"},
    bin = {"fennel-language-server"},
    extras = {["repository_url"] = "https://github.com/rydesun/fennel-language-server", ["rev"] = "true"},
    version = "59005549ca1191bf2aa364391e6bf2371889b4f8", -- snapshot pin
  },
  {
    id = "findent", display = "findent", detail = "findent indents/beautifies/converts and can optionally generate the dependencies of Fortran sources.", manager = "pypi", pkg = "findent",
    categories = {"Formatter"},
    languages = {"Fortran"},
    bin = {"findent"},
    version = "4.3.6", -- snapshot pin
  },
  {
    id = "fish-lsp", display = "fish-lsp", detail = "LSP implementation for the fish shell language", manager = "npm", pkg = "fish-lsp",
    categories = {"LSP"},
    languages = {"Fish"},
    bin = {"fish-lsp"},
    version = "1.1.4", -- snapshot pin
  },
  {
    id = "fixjson", display = "fixjson", detail = "A JSON file fixer/formatter for humans using (relaxed) JSON5.", manager = "npm", pkg = "fixjson",
    categories = {"Formatter"},
    languages = {"JSON"},
    bin = {"fixjson"},
    version = "1.1.2", -- snapshot pin
  },
  {
    id = "flake8", display = "flake8", detail = "flake8 is a python tool that glues together pycodestyle, pyflakes, mccabe, and third-party plugins to check the style...", manager = "pypi", pkg = "flake8",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"flake8"},
    version = "7.3.0", -- snapshot pin
  },
  {
    id = "flakeheaven", display = "flakeheaven", detail = "flakeheaven is a python linter built around flake8 to enable inheritable and complex toml configuration.", manager = "pypi", pkg = "flakeheaven",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"flakeheaven"},
    version = "3.3.0", -- snapshot pin
  },
  {
    id = "flux-lsp", display = "flux-lsp", detail = "Implementation of Language Server Protocol for the Flux language.", manager = "cargo", pkg = "flux-lsp",
    categories = {"LSP"},
    languages = {"Flux"},
    bin = {"flux-lsp"},
    extras = {["repository_url"] = "https://github.com/influxdata/flux-lsp"},
    version = "0.8.40", -- snapshot pin
  },
  {
    id = "foam-language-server", display = "foam-language-server", detail = "A language server for OpenFOAM case files.", manager = "npm", pkg = "foam-language-server",
    categories = {"LSP"},
    languages = {"OpenFOAM"},
    bin = {"foam-ls"},
    version = "0.4.6", -- snapshot pin
  },
  {
    id = "fortitude", display = "fortitude", detail = "Fortran linter, written in Rust.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Fortran"},
    bin = {"fortitude"},
    repo = "PlasmaFAIR/fortitude", asset = {
      linux = { match = "fortitude\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "fortitude\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "fortitude\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.9.2", -- snapshot pin
  },
  {
    id = "fortls", display = "fortls", detail = "fortls - Fortran Language Server.", manager = "pypi", pkg = "fortls",
    categories = {"LSP"},
    languages = {"Fortran"},
    bin = {"fortls"},
    version = "3.2.2", -- snapshot pin
  },
  {
    id = "fourmolu", display = "fourmolu", detail = "A fork of Ormolu that uses four space indentation and allows arbitrary configuration.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Haskell"},
    bin = {"fourmolu"},
    repo = "fourmolu/fourmolu", asset = {
      linux = { match = "fourmolu\\-.*\\-linux\\-x86_64", archive = "none" },
      mac = { match = "fourmolu\\-.*\\-osx\\-x86_64", archive = "none" },
    },
    version = "v0.19.0.1", -- snapshot pin
  },
  {
    id = "fprettify", display = "fprettify", detail = "fprettify is an auto-formatter for modern Fortran code that imposes strict whitespace formatting, written in Python.", manager = "pypi", pkg = "fprettify",
    categories = {"Formatter"},
    languages = {"Fortran"},
    bin = {"fprettify"},
    version = "0.3.7", -- snapshot pin
  },
  {
    id = "fsautocomplete", display = "fsautocomplete", detail = "F# language server using Language Server Protocol.", manager = "nuget", pkg = "fsautocomplete",
    categories = {"LSP"},
    languages = {"F#"},
    bin = {"fsautocomplete"},
    version = "0.83.0", -- snapshot pin
  },
  {
    id = "gci", display = "gci", detail = "GCI, a tool that control golang package import order and make it always deterministic.", manager = "golang", pkg = "github.com/daixiang0/gci",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"gci"},
    version = "v0.14.0", -- snapshot pin
  },
  {
    id = "gdscript-formatter", display = "gdscript-formatter", detail = "A faster code formatter for GDScript and Godot 4.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"GDScript"},
    bin = {"gdscript-formatter"},
    repo = "GDQuest/GDScript-formatter", asset = {
      mac = { match = "gdscript\\-formatter\\-.*\\-macos\\-aarch64\\.zip", archive = "zip" },
      win = { match = "gdscript\\-formatter\\-.*\\-windows\\-x86_64\\.exe\\.zip", archive = "zip" },
    },
    version = "0.24.0", -- snapshot pin
  },
  {
    id = "gdtoolkit", display = "gdtoolkit", detail = "A set of tools for daily work with GDScript.", manager = "pypi", pkg = "gdtoolkit",
    categories = {"Linter", "Formatter"},
    languages = {"GDScript"},
    bin = {"gdlint", "gdformat"},
    version = "4.5.0", -- snapshot pin
  },
  {
    id = "gersemi", display = "gersemi", detail = "gersemi - A formatter to make your CMake code the real treasure.", manager = "pypi", pkg = "gersemi",
    categories = {"Formatter"},
    languages = {"CMake"},
    bin = {"gersemi"},
    version = "0.28.1", -- snapshot pin
  },
  {
    id = "gh", display = "gh", detail = "gh is GitHub on the command line. It brings pull requests, issues, and other GitHub concepts to the terminal next to ...", manager = "github", pkg = "",
    bin = {"gh"},
    repo = "cli/cli", asset = {
      mac = { match = "gh_.*_macOS_arm64\\.zip", archive = "zip" },
      win = { match = "gh_.*_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v2.100.0", -- snapshot pin
  },
  {
    id = "gh-actions-language-server", display = "gh-actions-language-server", detail = "GitHub Actions Language Server", manager = "npm", pkg = "%40actions/languageserver",
    categories = {"LSP"},
    languages = {"YAML"},
    bin = {"gh-actions-language-server", "actions-languageserver"},
    version = "0.3.61", -- snapshot pin
  },
  {
    id = "ginko_ls", display = "ginko_ls", detail = "ginko_ls is meant to be a feature-complete language server for device-trees. Language servers can be used in many edi...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"DTS"},
    bin = {"ginko_ls"},
    repo = "Schottkyc137/ginko", asset = {
      linux = { match = "ginko_ls\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "ginko_ls\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "ginko_ls\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.0.8", -- snapshot pin
  },
  {
    id = "gitlab-ci-ls", display = "gitlab-ci-ls", detail = "An experimental language server for Gitlab CI.", manager = "cargo", pkg = "gitlab-ci-ls",
    categories = {"LSP"},
    languages = {"YAML"},
    bin = {"gitlab-ci-ls"},
    version = "1.4.0", -- snapshot pin
  },
  {
    id = "gitleaks", display = "gitleaks", detail = "Gitleaks helps you protect and discover secrets in git repositories.", manager = "github", pkg = "",
    categories = {"Linter"},
    bin = {"gitleaks"},
    repo = "gitleaks/gitleaks", asset = {
      mac = { match = "gitleaks_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "gitleaks_.*_windows_x64\\.zip", archive = "zip" },
    },
    version = "v8.30.1", -- snapshot pin
  },
  {
    id = "gitlint", display = "gitlint", detail = "Gitlint is a git commit message linter written in Python: it checks your commit messages for style.", manager = "pypi", pkg = "gitlint",
    categories = {"Linter"},
    bin = {"gitlint"},
    version = "0.19.1", -- snapshot pin
  },
  {
    id = "gitui", display = "gitui", detail = "Blazing fast terminal-ui for git written in Rust.", manager = "github", pkg = "",
    bin = {"gitui"},
    repo = "extrawurst/gitui", asset = {
      mac = { match = "gitui\\-mac\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "gitui\\-win\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.28.1", -- snapshot pin
  },
  {
    id = "glint", display = "glint", detail = "Glint is a set of tools to aid in developing code that uses the Glimmer VM for rendering, such as Ember.js v3.24+ and...", manager = "npm", pkg = "%40glint/core",
    categories = {"LSP", "Linter"},
    languages = {"Handlebars", "Glimmer", "TypeScript", "JavaScript"},
    bin = {"glint", "glint-language-server"},
    version = "1.5.2", -- snapshot pin
  },
  {
    id = "glow", display = "glow", detail = "Render markdown on the CLI, with pizzazz!", manager = "github", pkg = "",
    languages = {"Markdown"},
    bin = {"glow"},
    repo = "charmbracelet/glow", asset = {
      mac = { match = "glow_.*_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "glow_.*_Windows_x86_64\\.zip", archive = "zip" },
    },
    version = "v3.0.0", -- snapshot pin
  },
  {
    id = "glsl_analyzer", display = "glsl_analyzer", detail = "Language server for GLSL (autocomplete, goto-definition, formatter, and more)", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"OpenGL"},
    bin = {"glsl_analyzer"},
    repo = "nolanderc/glsl_analyzer", asset = {
      mac = { match = "aarch64\\-macos\\.zip", archive = "zip" },
      win = { match = "x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v1.7.1", -- snapshot pin
  },
  {
    id = "gn-language-server", display = "gn-language-server", detail = "A language server for GN, the build configuration language used in Chromium, Fuchsia, and other projects.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"GN"},
    bin = {"gn-language-server"},
    repo = "google/gn-language-server", asset = {
      mac = { match = "gn\\-language\\-server\\-.*\\-darwin\\-aarch64", archive = "none" },
      win = { match = "gn\\-language\\-server\\-.*\\-windows\\-x86_64\\.exe", archive = "none" },
    },
    version = "v1.16.0", -- snapshot pin
  },
  {
    id = "go", display = "gopls", detail = "gopls (pronounced \"Go please\") is the official Go language server developed by the Go team. It provides IDE features ...", manager = "golang", pkg = "golang.org/x/tools/gopls",
    categories = {"LSP"},
    languages = {"Go"},
    aliases = {"golang", "gopls", "gopls"},
    bin = {"gopls"},
    version = "v0.23.0", -- snapshot pin
  },
  {
    id = "go-debug-adapter", display = "go-debug-adapter", detail = "Go debug adapter sourced from the VSCode Go extension.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"Go"},
    bin = {"go-debug-adapter"},
    repo = "golang/vscode-go", asset = {
      linux = { match = "go\\-.*\\.vsix", archive = "none" },
      mac = { match = "go\\-.*\\.vsix", archive = "none" },
      win = { match = "go\\-.*\\.vsix", archive = "none" },
    },
    version = "v0.56.1", -- snapshot pin
    runs = {
      ["go-debug-adapter"] = { kind = "node", hint = "extension/dist/debugAdapter.js" },
    },
  },
  {
    id = "gofumpt", display = "gofumpt", detail = "A stricter gofmt.", manager = "golang", pkg = "mvdan.cc/gofumpt",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"gofumpt"},
    version = "v0.11.0", -- snapshot pin
  },
  {
    id = "goimports", display = "goimports", detail = "A golang formatter which formats your code in the same style as gofmt and additionally updates your Go import lines, ...", manager = "golang", pkg = "golang.org/x/tools",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"goimports"},
    version = "v0.49.0#cmd/goimports", -- snapshot pin
  },
  {
    id = "goimports-reviser", display = "goimports-reviser", detail = "Tool for Golang to sort goimports by 3-4 groups: std, general, company (optional), and project dependencies. Also, fo...", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"goimports-reviser"},
    repo = "incu6us/goimports-reviser", asset = {
      mac = { match = "goimports\\-reviser_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "goimports\\-reviser_.*_windows_amd64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v3.13.2", -- snapshot pin
  },
  {
    id = "golangci-lint", display = "golangci-lint", detail = "golangci-lint is a fast Go linters runner. It runs linters in parallel, uses caching, supports yaml config, has integ...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Go"},
    bin = {"golangci-lint"},
    repo = "golangci/golangci-lint", asset = {
      mac = { match = "golangci\\-lint\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "golangci\\-lint\\-.*\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v2.13.2", -- snapshot pin
  },
  {
    id = "golangci-lint-langserver", display = "golangci-lint-langserver", detail = "golangci-lint language server.", manager = "golang", pkg = "github.com/nametake/golangci-lint-langserver",
    categories = {"LSP"},
    languages = {"Go"},
    bin = {"golangci-lint-langserver"},
    version = "v0.0.12", -- snapshot pin
  },
  {
    id = "golines", display = "golines", detail = "A golang formatter that fixes long lines.", manager = "golang", pkg = "github.com/golangci/golines",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"golines"},
    version = "v0.15.0", -- snapshot pin
  },
  {
    id = "gomodifytags", display = "gomodifytags", detail = "Go tool to modify/update field tags in structs.", manager = "golang", pkg = "github.com/fatih/gomodifytags",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"gomodifytags"},
    version = "v1.17.0", -- snapshot pin
  },
  {
    id = "google-java-format", display = "google-java-format", detail = "google-java-format is a program that reformats Java source code to comply with Google Java Style.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Java"},
    bin = {"google-java-format"},
    repo = "google/google-java-format", asset = {
      mac = { match = "google\\-java\\-format_darwin\\-arm64", archive = "none" },
      win = { match = "google\\-java\\-format_windows\\-x86\\-64\\.exe", archive = "none" },
    },
    version = "v1.36.1", -- snapshot pin
  },
  {
    id = "gotests", display = "gotests", detail = "Gotests is a Golang commandline tool that generates table driven tests based on its target source files' function and...", manager = "golang", pkg = "github.com/cweill/gotests",
    categories = {"Formatter"},
    languages = {"Go"},
    bin = {"gotests"},
    version = "v1.9.0#gotests", -- snapshot pin
  },
  {
    id = "gotestsum", display = "gotestsum", detail = "'go test' runner with output optimized for humans, JUnit XML for CI integration, and a summary of the test results.", manager = "golang", pkg = "gotest.tools/gotestsum",
    languages = {"Go"},
    bin = {"gotestsum"},
    version = "v1.13.0", -- snapshot pin
  },
  {
    id = "gradle-language-server", display = "gradle-language-server", detail = "Gradle language server.", manager = "openvsx", pkg = "",
    categories = {"LSP"},
    languages = {"Gradle"},
    bin = {"gradle-language-server"},
    openvsx = { ns = "vscjava", ext = "vscode-gradle", version = "3.17.3", file = "vscjava.vscode-gradle-{{version}}.vsix" },
    version = "3.17.3", -- snapshot pin
    runs = {
      ["gradle-language-server"] = { kind = "jar", hint = "extension/lib/gradle-language-server.jar" },
    },
  },
  {
    id = "grammarly-languageserver", display = "grammarly-languageserver", detail = "A language server implementation on top of Grammarly's SDK.", manager = "npm", pkg = "grammarly-languageserver",
    categories = {"LSP"},
    languages = {"Markdown", "Text"},
    bin = {"grammarly-languageserver"},
    version = "0.0.4", -- snapshot pin
  },
  {
    id = "graphql-language-service-cli", display = "graphql-language-service-cli", detail = "GraphQL Language Service provides an interface for building GraphQL language services for IDEs.", manager = "npm", pkg = "graphql-language-service-cli",
    categories = {"LSP"},
    languages = {"GraphQL"},
    bin = {"graphql-lsp"},
    version = "3.5.0", -- snapshot pin
  },
  {
    id = "hadolint", display = "hadolint", detail = "Dockerfile linter, validate inline bash, written in Haskell.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Docker"},
    bin = {"hadolint"},
    repo = "hadolint/hadolint", asset = {
      mac = { match = "hadolint\\-macos\\-arm64", archive = "none" },
      win = { match = "hadolint\\-Windows\\-x86_64\\.exe", archive = "none" },
    },
    version = "v2.15.1", -- snapshot pin
  },
  {
    id = "haml-lint", display = "haml-lint", detail = "haml-lint is a tool to help keep your HAML files clean and readable. In addition to HAML-specific style and lint chec...", manager = "gem", pkg = "haml_lint",
    categories = {"Linter"},
    languages = {"HAML"},
    bin = {"haml-lint"},
    version = "0.78.0", -- snapshot pin
  },
  {
    id = "harper-ls", display = "harper-ls", detail = "The Grammar Checker for Developers.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown", "Rust", "TypeScript", "JavaScript", "Python", "Go", "C", "C++", "Ruby", "C#", "TOML", "Lua", "Java", "LaTeX"},
    bin = {"harper-ls"},
    repo = "elijah-potter/harper", asset = {
      linux = { match = "harper\\-ls\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "harper\\-ls\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "harper\\-ls\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v2.9.1", -- snapshot pin
  },
  {
    id = "haxe-language-server", display = "haxe-language-server", detail = "Language Server Protocol implementation for the Haxe language.", manager = "openvsx", pkg = "",
    categories = {"LSP"},
    languages = {"Haxe"},
    bin = {"haxe-language-server"},
    openvsx = { ns = "nadako", ext = "vshaxe", version = "2.34.2", file = "nadako.vshaxe-{{version}}.vsix" },
    version = "2.34.2", -- snapshot pin
    runs = {
      ["haxe-language-server"] = { kind = "node", hint = "extension/bin/server.js" },
    },
  },
  {
    id = "hclfmt", display = "hclfmt", detail = "A command to format HCL files", manager = "golang", pkg = "github.com/hashicorp/hcl/v2",
    categories = {"Formatter"},
    languages = {"HCL"},
    bin = {"hclfmt"},
    version = "v2.24.0#cmd/hclfmt", -- snapshot pin
  },
  {
    id = "hdl-checker", display = "hdl-checker", detail = "HDL Checker is a language server that wraps VHDL/Verilg/SystemVerilog tools that aims to reduce the boilerplate code ...", manager = "pypi", pkg = "hdl-checker",
    categories = {"LSP"},
    languages = {"VHDL", "Verilog", "SystemVerilog"},
    bin = {"hdl_checker"},
    version = "0.7.4", -- snapshot pin
  },
  {
    id = "helm-ls", display = "helm-ls", detail = "A language server that offers Helm support in early development - programmed in Go.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Helm"},
    bin = {"helm_ls"},
    repo = "mrjosh/helm-ls", asset = {
      linux = { match = "helm_ls_linux_amd64", archive = "none" },
      mac = { match = "helm_ls_darwin_arm64", archive = "none" },
      win = { match = "helm_ls_windows_amd64\\.exe", archive = "none" },
    },
    version = "v0.5.4", -- snapshot pin
  },
  {
    id = "herb-language-server", display = "herb-language-server", detail = "Powerful and seamless HTML-aware ERB parsing and tooling.", manager = "npm", pkg = "@herb-tools/language-server",
    categories = {"LSP"},
    languages = {"HTML", "Ruby"},
    bin = {"herb-language-server"},
    version = "0.10.3", -- snapshot pin
  },
  {
    id = "hlint", display = "hlint", detail = "HLint is a tool for suggesting possible improvements to Haskell code. These suggestions include ideads such as using ...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Haskell"},
    bin = {"hlint"},
    repo = "ndmitchell/hlint", asset = {
      mac = { match = "hlint\\-.*\\-x86_64\\-osx\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "hlint\\-.*\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v3.8", -- snapshot pin
  },
  {
    id = "hoon-language-server", display = "hoon-language-server", detail = "Language Server for Hoon. Middleware to translate between the Language Server Protocol and your Urbit.", manager = "npm", pkg = "%40urbit/hoon-language-server",
    categories = {"LSP"},
    languages = {"Hoon"},
    bin = {"hoon-language-server"},
    version = "0.1.2", -- snapshot pin
  },
  {
    id = "html", display = "html-lsp", detail = "Language Server Protocol implementation for HTML.", manager = "npm", pkg = "vscode-langservers-extracted",
    categories = {"LSP"},
    languages = {"HTML"},
    aliases = {"htm", "html-language-server", "html-lsp"},
    bin = {"vscode-html-language-server"},
    version = "4.10.0", -- snapshot pin
  },
  {
    id = "htmlbeautifier", display = "htmlbeautifier", detail = "A normaliser/beautifier for HTML that also understands embedded Ruby. Ideal for tidying up Rails templates.", manager = "gem", pkg = "htmlbeautifier",
    categories = {"Formatter"},
    languages = {"HTML", "Ruby"},
    bin = {"htmlbeautifier"},
    version = "1.4.3", -- snapshot pin
  },
  {
    id = "htmlhint", display = "htmlhint", detail = "The Static Code Analysis Tool for your HTML", manager = "npm", pkg = "htmlhint",
    categories = {"Linter"},
    languages = {"HTML"},
    bin = {"htmlhint"},
    version = "1.9.2", -- snapshot pin
  },
  {
    id = "htmx-lsp", display = "htmx-lsp", detail = "An experimental LSP for HTMX.", manager = "cargo", pkg = "htmx-lsp",
    categories = {"LSP"},
    languages = {"HTMX"},
    bin = {"htmx-lsp"},
    version = "0.1.0", -- snapshot pin
  },
  {
    id = "hydra-lsp", display = "hydra-lsp", detail = "LSP for Hydra config files", manager = "pypi", pkg = "hydra-lsp",
    categories = {"LSP"},
    languages = {"YAML"},
    bin = {"hydra-lsp"},
    version = "0.1.3", -- snapshot pin
  },
  {
    id = "hylo-language-server", display = "hylo-language-server", detail = "hylo-language-server is an implementation of the Language Server Protocol for the Hylo programming language that prov...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Hylo"},
    bin = {"hylo-language-server"},
    repo = "hylo-lang/hylo-language-server", asset = {
      mac = { match = "hylo\\-language\\-server\\-macos\\-arm64\\.tar\\.zst", archive = "none" },
      win = { match = "hylo\\-language\\-server\\-windows\\-x64\\.tar\\.zst", archive = "none" },
    },
    version = "v0.0.18", -- snapshot pin
  },
  {
    id = "hyprls", display = "hyprls", detail = "A LSP server for Hyprland config files", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Hypr"},
    bin = {"hyprls"},
    repo = "hyprland-community/hyprls", asset = {
      linux = { match = "hyprls", archive = "none" },
      mac = { match = "hyprls\\-macos", archive = "none" },
      win = { match = "hyprls\\.exe", archive = "none" },
    },
    version = "v0.14.0", -- snapshot pin
  },
  {
    id = "iferr", display = "iferr", detail = "Go tool to generate if err != nil block for the current function.", manager = "golang", pkg = "github.com/koron/iferr",
    languages = {"Go"},
    bin = {"iferr"},
    version = "v1.0.0", -- snapshot pin
  },
  {
    id = "impl", display = "impl", detail = "impl generates method stubs for implementing an interface.", manager = "golang", pkg = "github.com/josharian/impl",
    languages = {"Go"},
    bin = {"impl"},
    version = "v1.5.0", -- snapshot pin
  },
  {
    id = "isort", display = "isort", detail = "isort is a Python utility / library to sort imports alphabetically.", manager = "pypi", pkg = "isort",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"isort"},
    version = "9.0.1", -- snapshot pin
  },
  {
    id = "java-debug-adapter", display = "java-debug-adapter", detail = "The debug server implementation for Java. It conforms to the debugger adapter protocol.", manager = "openvsx", pkg = "",
    categories = {"DAP"},
    languages = {"Java"},
    bin = {},
    openvsx = { ns = "vscjava", ext = "vscode-java-debug", version = "0.59.0", file = "vscjava.vscode-java-debug-{{version}}.vsix" },
    version = "0.59.0", -- snapshot pin
  },
  {
    id = "java-test", display = "java-test", detail = "The Test Runner for Java works with java-debug-adapter to provide the following features: - Run/Debug test cases - Cu...", manager = "openvsx", pkg = "",
    categories = {"DAP"},
    languages = {"Java"},
    bin = {},
    openvsx = { ns = "vscjava", ext = "vscode-java-test", version = "0.46.0", file = "vscjava.vscode-java-test-{{version}}.vsix" },
    version = "0.46.0", -- snapshot pin
  },
  {
    id = "jayvee-language-server", display = "jayvee-language-server", detail = "Jayvee is a domain-specific language and runtime for automated processing of data pipelines.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Jayvee"},
    bin = {"jayvee-language-server"},
    repo = "jvalue/jayvee", asset = {
      linux = { match = "jayvee\\.vsix", archive = "none" },
      mac = { match = "jayvee\\.vsix", archive = "none" },
      win = { match = "jayvee\\.vsix", archive = "none" },
    },
    version = "v0.5.0-alpha", -- snapshot pin
    runs = {
      ["jayvee-language-server"] = { kind = "node", hint = "extension/language-server.cjs" },
    },
  },
  {
    id = "jdtls", display = "jdtls", detail = "Java language server.", manager = "generic", pkg = "",
    categories = {"LSP"},
    languages = {"Java"},
    bin = {"jdtls"},
    dl = {
      linux = { files = {["jdtls.tar.gz"] = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-1.60.0-202606262232.tar.gz", ["lombok.jar"] = "https://projectlombok.org/downloads/lombok.jar"}, bin = "" },
      mac = { files = {["jdtls.tar.gz"] = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-1.60.0-202606262232.tar.gz", ["lombok.jar"] = "https://projectlombok.org/downloads/lombok.jar"}, bin = "" },
      win = { files = {["jdtls.tar.gz"] = "https://download.eclipse.org/jdtls/snapshots/jdt-language-server-1.60.0-202606262232.tar.gz", ["lombok.jar"] = "https://projectlombok.org/downloads/lombok.jar"}, bin = "" },
    },
    version = "v1.60.0", -- snapshot pin
    runs = {
      ["jdtls"] = { kind = "python", hint = "bin/jdtls" },
    },
  },
  {
    id = "jedi-language-server", display = "jedi-language-server", detail = "A Python language server exclusively for Jedi. If Jedi supports it well, this language server should too.", manager = "pypi", pkg = "jedi-language-server",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"jedi-language-server"},
    version = "0.47.0", -- snapshot pin
  },
  {
    id = "jinja-lsp", display = "jinja-lsp", detail = "Experimental jinja lsp server, with autocomplete, syntax highlighting, hover, goto definition, code actions and linting.", manager = "github", pkg = "",
    categories = {"LSP", "Linter", "Compiler"},
    languages = {"Django", "Jinja", "Nunjucks"},
    bin = {"jinja-lsp"},
    repo = "uros-5/jinja-lsp", asset = {
      linux = { match = "jinja\\-lsp\\-x86_64\\-unknown\\-linux\\-gnu", archive = "none" },
      mac = { match = "jinja\\-lsp\\-aarch64\\-apple\\-darwin", archive = "none" },
      win = { match = "jinja\\-lsp\\-x86_64\\-pc\\-windows\\-msvc\\.exe", archive = "none" },
    },
    version = "v0.2.3", -- snapshot pin
  },
  {
    id = "jls", display = "jls", detail = "Java language server using Java compiler API, optimized for Neovim.", manager = "github", pkg = "",
    categories = {"LSP", "DAP"},
    languages = {"Java"},
    bin = {"jls", "jls-debug-adapter"},
    repo = "idelice/jls", asset = {
      linux = { match = "\\['jls\\-linux\\-x64\\.tar\\.gz',\\ 'jls\\-linux\\-x64\\.tar\\.gz\\.sha256'\\]", archive = "none" },
      mac = { match = "\\['jls\\-macos\\-aarch64\\.tar\\.gz',\\ 'jls\\-macos\\-aarch64\\.tar\\.gz\\.sha256'\\]", archive = "none" },
      win = { match = "\\['jls\\-windows\\-x64\\.zip',\\ 'jls\\-windows\\-x64\\.zip\\.sha256'\\]", archive = "none" },
    },
    version = "v0.7.2", -- snapshot pin
  },
  {
    id = "joker", display = "joker", detail = "Small Clojure interpreter, linter and formatter.", manager = "github", pkg = "",
    categories = {"Formatter", "Linter"},
    languages = {"Clojure", "ClojureScript"},
    bin = {"joker"},
    repo = "candid82/joker", asset = {
      mac = { match = "joker\\-mac\\-amd64\\.zip", archive = "zip" },
      win = { match = "joker\\-win\\-amd64\\.zip", archive = "zip" },
    },
    version = "v1.10.0", -- snapshot pin
  },
  {
    id = "jq", display = "jq", detail = "Command-line JSON processor.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"JSON"},
    bin = {"jq"},
    repo = "stedolan/jq", asset = {
      mac = { match = "jq\\-macos\\-arm64", archive = "none" },
      win = { match = "jq\\-windows\\-amd64\\.exe", archive = "none" },
    },
    version = "jq-1.7", -- snapshot pin
  },
  {
    id = "jq-lsp", display = "jq-lsp", detail = "jq-lsp is a language server for the jq language, developed by Mattias Wadman. It provides IDE features to any LSP-com...", manager = "golang", pkg = "github.com/wader/jq-lsp",
    categories = {"LSP"},
    languages = {"Jq"},
    bin = {"jq-lsp"},
    version = "v0.1.18", -- snapshot pin
  },
  {
    id = "js-debug-adapter", display = "js-debug-adapter", detail = "The VS Code JavaScript debugger.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"js-debug-adapter"},
    repo = "microsoft/vscode-js-debug", asset = {
      linux = { match = "js\\-debug\\-dap\\-.*\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "js\\-debug\\-dap\\-.*\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "js\\-debug\\-dap\\-.*\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v1.117.0", -- snapshot pin
  },
  {
    id = "json", display = "json-lsp", detail = "Language Server Protocol implementation for JSON.", manager = "npm", pkg = "vscode-langservers-extracted",
    categories = {"LSP"},
    languages = {"JSON"},
    aliases = {"json-language-server", "json-lsp"},
    bin = {"vscode-json-language-server"},
    version = "4.10.0", -- snapshot pin
  },
  {
    id = "json-repair", display = "json-repair", detail = "A package to repair broken json strings", manager = "pypi", pkg = "json-repair",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"json_repair"},
    version = "0.63.4", -- snapshot pin
  },
  {
    id = "json-to-struct", display = "json-to-struct", detail = "A simple command-line tool for generating to struct definitions from JSON.", manager = "golang", pkg = "github.com/tmc/json-to-struct",
    languages = {"Go"},
    bin = {"json-to-struct"},
    version = "v0.1.0", -- snapshot pin
  },
  {
    id = "jsonld-lsp", display = "jsonld-lsp", detail = "JSON-LD Language Server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"JSON-LD"},
    bin = {"jsonld-language-server"},
    repo = "ajuvercr/jsonld-lsp", asset = {
      linux = { match = "jsonld\\-language\\-server\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "jsonld\\-language\\-server\\-x86_64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "jsonld\\-language\\-server\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v1.0.5", -- snapshot pin
  },
  {
    id = "jsonlint", display = "jsonlint", detail = "A pure JavaScript version of the service provided at jsonlint.com.", manager = "npm", pkg = "jsonlint",
    categories = {"Linter"},
    languages = {"JSON"},
    bin = {"jsonlint"},
    version = "1.6.3", -- snapshot pin
  },
  {
    id = "jsonnet-language-server", display = "jsonnet-language-server", detail = "A Language Server Protocol (LSP) server for Jsonnet (https://jsonnet.org).", manager = "golang", pkg = "github.com/grafana/jsonnet-language-server",
    categories = {"LSP"},
    languages = {"Jsonnet"},
    bin = {"jsonnet-language-server"},
    version = "v0.17.0", -- snapshot pin
  },
  {
    id = "jsonnetfmt", display = "jsonnetfmt", detail = "jsonnetfmt is a command line tool to format jsonnetfmt files.", manager = "golang", pkg = "github.com/google/go-jsonnet",
    categories = {"Formatter"},
    languages = {"Jsonnet"},
    bin = {"jsonnetfmt"},
    version = "v0.22.0#cmd/jsonnetfmt", -- snapshot pin
  },
  {
    id = "jupytext", display = "jupytext", detail = "Jupyter Notebooks as Markdown Documents, Julia, Python or R scripts", manager = "pypi", pkg = "jupytext",
    categories = {"Formatter"},
    languages = {"Python", "Julia", "R", "Markdown"},
    bin = {"jupytext"},
    version = "1.19.5", -- snapshot pin
  },
  {
    id = "just-lsp", display = "just-lsp", detail = "just-lsp is a server implementation of the language server protocol for just, the command runner.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Just"},
    bin = {"just-lsp"},
    repo = "terror/just-lsp", asset = {
      linux = { match = "just\\-lsp\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "just\\-lsp\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "just\\-lsp\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.7.1", -- snapshot pin
  },
  {
    id = "kakehashi", display = "kakehashi", detail = "Language server that bridges the gap between languages, editors, and tooling", manager = "github", pkg = "",
    categories = {"LSP"},
    bin = {"kakehashi"},
    repo = "atusy/kakehashi", asset = {
      mac = { match = "kakehashi\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "kakehashi\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v1.0.0", -- snapshot pin
  },
  {
    id = "kcl", display = "kcl", detail = "KCL is an open-source, constraint-based record and functional language that enhances the writing of complex configura...", manager = "github", pkg = "",
    categories = {"Compiler", "Formatter", "LSP", "Linter"},
    languages = {"KCL"},
    bin = {"kcl", "kcl-language-server"},
    repo = "kcl-lang/kcl", asset = {
      linux = { match = "kclvm\\-.*\\-linux\\-amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "kclvm\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "kclvm\\-.*\\-windows\\.zip", archive = "zip" },
    },
    version = "v0.11.2", -- snapshot pin
  },
  {
    id = "kdlfmt", display = "kdlfmt", detail = "A code formatter for KDL documents.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"KDL"},
    bin = {"kdlfmt"},
    repo = "hougesen/kdlfmt", asset = {
      linux = { match = "kdlfmt\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.xz", archive = "none" },
      mac = { match = "kdlfmt\\-aarch64\\-apple\\-darwin\\.tar\\.xz", archive = "none" },
      win = { match = "kdlfmt\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.1.7", -- snapshot pin
  },
  {
    id = "kmp-lsp", display = "kmp-lsp", detail = "Kotlin Multiplatform Language Server", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Kotlin", "Java", "Swift"},
    bin = {"kmp-lsp"},
    repo = "Hessesian/kmp-lsp", asset = {
      linux = { match = "kmp\\-lsp\\-linux\\-x86_64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "kmp\\-lsp\\-darwin\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "kmp\\-lsp\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v0.26.0", -- snapshot pin
  },
  {
    id = "kos-language-server", display = "kos-language-server", detail = "Language Server for Kerboscript from the kOS Kerbal Space Program mod.", manager = "npm", pkg = "kos-language-server",
    categories = {"LSP", "Linter", "Formatter"},
    languages = {"Kerboscript"},
    bin = {"kls"},
    version = "1.1.5", -- snapshot pin
  },
  {
    id = "kotlin-debug-adapter", display = "kotlin-debug-adapter", detail = "Kotlin/JVM debugging for any editor/IDE using the Debug Adapter Protocol.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"Kotlin"},
    bin = {"kotlin-debug-adapter"},
    repo = "fwcd/kotlin-debug-adapter", asset = {
      mac = { match = "adapter\\.zip", archive = "zip" },
      win = { match = "adapter\\.zip", archive = "zip" },
    },
    version = "0.4.4", -- snapshot pin
  },
  {
    id = "kotlin-lsp", display = "kotlin-lsp", detail = "Kotlin Language Server and plugin for Visual Studio Code", manager = "generic", pkg = "",
    categories = {"LSP"},
    languages = {"Kotlin"},
    bin = {"intellij-server"},
    dl = {
      linux = { files = {["kotlin-lsp.tar.gz"] = "https://download-cdn.jetbrains.com/language-server/kotlin-server/262.9593.0/kotlin-server-262.9593.0.tar.gz"}, bin = "kotlin-server-{{ version | strip_prefix \"kotlin-lsp/v\" }}/bin/intellij-server" },
      mac = { files = {["kotlin-lsp.zip"] = "https://download-cdn.jetbrains.com/language-server/kotlin-server/262.9593.0/kotlin-server-262.9593.0.sit"}, bin = "kotlin-server-{{ version | strip_prefix \"kotlin-lsp/v\" }}/bin/intellij-server" },
      win = { files = {["kotlin-lsp.zip"] = "https://download-cdn.jetbrains.com/language-server/kotlin-server/262.9593.0/kotlin-server-262.9593.0.win.zip"}, bin = "bin/intellij-server.exe" },
    },
    version = "kotlin-lsp/v262.9593.0", -- snapshot pin
  },
  {
    id = "ktfmt", display = "ktfmt", detail = "A program that reformats Kotlin source code to comply with the common community conventions.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Kotlin"},
    bin = {"ktfmt"},
    repo = "facebook/ktfmt", asset = {
      linux = { match = "ktfmt\\-.*\\-with\\-dependencies\\.jar", archive = "none" },
      mac = { match = "ktfmt\\-.*\\-with\\-dependencies\\.jar", archive = "none" },
      win = { match = "ktfmt\\-.*\\-with\\-dependencies\\.jar", archive = "none" },
    },
    version = "v0.64", -- snapshot pin
    runs = {
      ["ktfmt"] = { kind = "jar", hint = "{{source.asset.file}}" },
    },
  },
  {
    id = "kube-linter", display = "kube-linter", detail = "KubeLinter is a static analysis tool that checks Kubernetes YAML files and Helm charts to ensure the applications rep...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Helm", "YAML"},
    bin = {"kube-linter"},
    repo = "stackrox/kube-linter", asset = {
      mac = { match = "kube\\-linter\\-darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "kube\\-linter\\-windows\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.8.3", -- snapshot pin
  },
  {
    id = "kubescape", display = "kubescape", detail = "Kubescape is an open-source Kubernetes security platform for your IDE, CI/CD pipelines, and clusters. It includes ris...", manager = "github", pkg = "",
    categories = {"Linter", "Runtime"},
    languages = {"Helm", "YAML"},
    bin = {"kubescape"},
    repo = "kubescape/kubescape", asset = {
      mac = { match = "kubescape_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "kubescape_.*_windows_amd64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v4.0.13", -- snapshot pin
  },
  {
    id = "kulala-fmt", display = "kulala-fmt", detail = "kulala-fmt An opinionated .http and .rest files linter and formatter.", manager = "npm", pkg = "%40mistweaverco/kulala-fmt",
    categories = {"Formatter", "Linter"},
    languages = {"http"},
    bin = {"kulala-fmt"},
    version = "4.5.3", -- snapshot pin
  },
  {
    id = "language-server-bitbake", display = "language-server-bitbake", detail = "A language server for BitBake.", manager = "npm", pkg = "language-server-bitbake",
    categories = {"LSP"},
    languages = {"BitBake"},
    bin = {"language-server-bitbake"},
    version = "2.10.0", -- snapshot pin
  },
  {
    id = "laravel-ls", display = "laravel-ls", detail = "Laravel Language Server written in go.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"PHP"},
    bin = {"laravel-ls"},
    repo = "laravel-ls/laravel-ls", asset = {
      linux = { match = "laravel\\-ls\\-.*\\-linux\\-amd64", archive = "none" },
      mac = { match = "laravel\\-ls\\-.*\\-darwin\\-arm64", archive = "none" },
      win = { match = "laravel\\-ls\\-.*\\-windows\\-amd64", archive = "none" },
    },
    version = "v0.1.0", -- snapshot pin
  },
  {
    id = "latexindent", display = "latexindent", detail = "latexindent.pl is a perl script to beautify/tidy/format/indent (add horizontal leading space to) code within environm...", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"LaTeX"},
    bin = {"latexindent"},
    repo = "cmhughes/latexindent.pl", asset = {
      linux = { match = "latexindent\\-linux", archive = "none" },
      mac = { match = "latexindent\\-macos", archive = "none" },
      win = { match = "latexindent\\.exe", archive = "none" },
    },
    version = "V4.0.2", -- snapshot pin
  },
  {
    id = "lean-language-server", display = "lean-language-server", detail = "Lean3 Language Server.", manager = "npm", pkg = "lean-language-server",
    categories = {"LSP"},
    languages = {"Lean 3"},
    bin = {"lean-language-server"},
    version = "3.4.0", -- snapshot pin
  },
  {
    id = "lelwel", display = "lelwel", detail = "LL(1) parser generator for Rust.", manager = "cargo", pkg = "lelwel",
    categories = {"LSP"},
    languages = {"Lelwel"},
    bin = {"lelwel-ls", "llw"},
    extras = {["features"] = "lsp,cli"},
    version = "0.10.4", -- snapshot pin
  },
  {
    id = "lemminx", display = "lemminx", detail = "XML Language Server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"XML"},
    bin = {"lemminx"},
    repo = "redhat-developer/vscode-xml", asset = {
      linux = { match = "lemminx\\-linux\\-x86_64\\.zip", archive = "zip" },
      mac = { match = "lemminx\\-osx\\-aarch_64\\.zip", archive = "zip" },
      win = { match = "lemminx\\-win32\\.zip", archive = "zip" },
    },
    version = "0.29.3", -- snapshot pin
  },
  {
    id = "lemmy-help", display = "lemmy-help", detail = "Every one needs help, so lemmy-help you! A CLI to generate vim/nvim help doc from emmylua.", manager = "github", pkg = "",
    languages = {"Lua"},
    bin = {"lemmy-help"},
    repo = "numToStr/lemmy-help", asset = {
      linux = { match = "lemmy\\-help\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "lemmy\\-help\\-x86_64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "lemmy\\-help\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.11.0", -- snapshot pin
  },
  {
    id = "lemonade", display = "lemonade", detail = "Lemonade is a remote utility tool. (copy, paste and open browser) over TCP.", manager = "golang", pkg = "github.com/lemonade-command/lemonade",
    bin = {"lemonade"},
    version = "v1.1.2", -- snapshot pin
  },
  {
    id = "llm-ls", display = "llm-ls", detail = "LSP server leveraging AI/LLMs based for code completion.", manager = "github", pkg = "",
    categories = {"LSP"},
    bin = {"llm-ls"},
    repo = "huggingface/llm-ls", asset = {
      linux = { match = "llm\\-ls\\-x86_64\\-unknown\\-linux\\-gnu\\.gz", archive = "gz" },
      mac = { match = "llm\\-ls\\-aarch64\\-apple\\-darwin\\.gz", archive = "gz" },
      win = { match = "llm\\-ls\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.5.3", -- snapshot pin
  },
  {
    id = "local-lua-debugger-vscode", display = "local-lua-debugger-vscode", detail = "A simple Lua debugger which requires no additional dependencies.", manager = "openvsx", pkg = "",
    categories = {"DAP"},
    languages = {"Lua"},
    bin = {},
    openvsx = { ns = "tomblind", ext = "local-lua-debugger-vscode", version = "0.3.3", file = "tomblind.local-lua-debugger-vscode-{{version}}.vsix" },
    version = "0.3.3", -- snapshot pin
  },
  {
    id = "ltex-ls-plus", display = "ltex-ls-plus", detail = "LTeX Language Server+: LSP language server for LanguageTool 🔍✔️ with support for LaTeX 🎓, Markdown 📝, and others.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Text", "Markdown", "LaTeX", "reStructuredText"},
    bin = {"ltex-ls-plus", "ltex-cli-plus"},
    repo = "ltex-plus/ltex-ls-plus", asset = {
      linux = { match = "ltex\\-ls\\-plus\\-.*\\-linux\\-x64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "ltex\\-ls\\-plus\\-.*\\-mac\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ltex\\-ls\\-plus\\-.*\\-windows\\-x64\\.zip", archive = "zip" },
    },
    version = "18.7.0", -- snapshot pin
  },
  {
    id = "lua", display = "lua-language-server", detail = "A language server that offers Lua language support - programmed in Lua.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Lua"},
    aliases = {"lua_ls", "luals", "lua-language-server"},
    bin = {"lua-language-server"},
    repo = "LuaLS/lua-language-server", asset = {
      linux = { match = "lua\\-language\\-server\\-.*\\-linux\\-x64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "lua\\-language\\-server\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "lua\\-language\\-server\\-.*\\-win32\\-x64\\.zip", archive = "zip" },
    },
    version = "3.19.1", -- snapshot pin
  },
  {
    id = "luacheck", display = "luacheck", detail = "A tool for linting and static analysis of Lua code.", manager = "luarocks", pkg = "luacheck",
    categories = {"Linter"},
    languages = {"Lua"},
    bin = {"luacheck"},
    version = "1.1.0", -- snapshot pin
  },
  {
    id = "luafmt", display = "luafmt", detail = "A command-line tool for formatting Lua code.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Lua"},
    bin = {"luafmt"},
    repo = "EmmyLuaLs/emmylua-analyzer-rust", asset = {
      linux = { match = "luafmt\\-linux\\-x64\\-glibc\\.2\\.17\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "luafmt\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "luafmt\\-win32\\-x64\\.zip", archive = "zip" },
    },
    version = "0.25.1", -- snapshot pin
  },
  {
    id = "luaformatter", display = "luaformatter", detail = "Code formatter for Lua.", manager = "luarocks", pkg = "luaformatter",
    categories = {"Formatter"},
    languages = {"Lua"},
    bin = {"lua-format"},
    extras = {["repository_url"] = "https://luarocks.org/dev"},
    version = "scm-1", -- snapshot pin
  },
  {
    id = "luau-lsp", display = "luau-lsp", detail = "An implementation of a language server for the Luau programming language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Luau"},
    bin = {"luau-lsp"},
    repo = "JohnnyMorganz/luau-lsp", asset = {
      mac = { match = "luau\\-lsp\\-macos\\.zip", archive = "zip" },
      win = { match = "luau\\-lsp\\-win64\\.zip", archive = "zip" },
    },
    version = "1.69.0", -- snapshot pin
  },
  {
    id = "lwc-language-server", display = "lwc-language-server", detail = "Language Server for Lightning Web Components.", manager = "npm", pkg = "%40salesforce/lwc-language-server",
    categories = {"LSP"},
    languages = {"HTML", "JavaScript"},
    bin = {"lwc-language-server"},
    version = "4.12.13", -- snapshot pin
  },
  {
    id = "m68k-lsp-server", display = "m68k-lsp-server", detail = "Language Server Protocol implementation for Motorola 68000 assembly", manager = "npm", pkg = "m68k-lsp-server",
    categories = {"LSP"},
    languages = {"M68K"},
    bin = {"m68k-lsp-server"},
    version = "0.11.2", -- snapshot pin
  },
  {
    id = "markdown", display = "Markdown", detail = "markdown-language-features (markdown-language-server)", manager = "npm", pkg = "markdown-language-features",
    categories = {"LSP"},
    languages = {"Markdown"},
    aliases = {"md"},
    bin = {"markdown-language-server"},
    win_cmd = "npm install -g markdown-language-features", win_remove_cmd = "npm uninstall -g markdown-language-features",
  },
  {
    id = "markdown-oxide", display = "markdown-oxide", detail = "Markdown language server with advanced linking support made to be completely compatible with Obsidian; An Obsidian La...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"markdown-oxide"},
    repo = "feel-ix-343/markdown-oxide", asset = {
      linux = { match = "markdown\\-oxide\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "markdown\\-oxide\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "markdown\\-oxide\\-.*\\-x86_64\\-pc\\-windows\\-gnu\\.zip", archive = "zip" },
    },
    version = "v0.25.12", -- snapshot pin
  },
  {
    id = "markdown-toc", display = "markdown-toc", detail = "API and CLI for generating a markdown TOC (table of contents) for a README or any markdown files.", manager = "npm", pkg = "markdown-toc",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"markdown-toc"},
    version = "1.2.0", -- snapshot pin
  },
  {
    id = "markdownlint", display = "markdownlint", detail = "A Node.js style checker and lint tool for Markdown/CommonMark files.", manager = "npm", pkg = "markdownlint-cli",
    categories = {"Linter", "Formatter"},
    languages = {"Markdown"},
    bin = {"markdownlint"},
    version = "0.49.1", -- snapshot pin
  },
  {
    id = "markdownlint-cli2", display = "markdownlint-cli2", detail = "A fast, flexible, configuration-based command-line interface for linting Markdown/CommonMark files with the markdownl...", manager = "npm", pkg = "markdownlint-cli2",
    categories = {"Linter", "Formatter"},
    languages = {"Markdown"},
    bin = {"markdownlint-cli2"},
    version = "0.23.2", -- snapshot pin
  },
  {
    id = "markmap-cli", display = "markmap-cli", detail = "Visualize your Markdown as mindmaps.", manager = "npm", pkg = "markmap-cli",
    languages = {"Markdown"},
    bin = {"markmap"},
    version = "0.18.12", -- snapshot pin
  },
  {
    id = "marko-language-server", display = "marko-language-server", detail = "A language server (implementing the language server protocol) for Marko.", manager = "npm", pkg = "%40marko/language-server",
    categories = {"LSP"},
    languages = {"Marko"},
    bin = {"marko-language-server"},
    version = "3.4.0", -- snapshot pin
  },
  {
    id = "marksman", display = "marksman", detail = "Markdown LSP server providing completion, cross-references, diagnostics, and more.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"marksman"},
    repo = "artempyanykh/marksman", asset = {
      mac = { match = "marksman\\-macos", archive = "none" },
      win = { match = "marksman\\.exe", archive = "none" },
    },
    version = "2026-02-08", -- snapshot pin
  },
  {
    id = "markuplint", display = "markuplint", detail = "An HTML linter for all markup developers.", manager = "npm", pkg = "markuplint",
    categories = {"Linter"},
    languages = {"HTML"},
    bin = {"markuplint"},
    version = "4.18.3", -- snapshot pin
  },
  {
    id = "mbake", display = "mbake", detail = "Makefile formatter and linter.", manager = "pypi", pkg = "mbake",
    categories = {"Formatter", "Linter"},
    languages = {"Makefile"},
    bin = {"mbake"},
    version = "1.4.6", -- snapshot pin
  },
  {
    id = "mdformat", display = "mdformat", detail = "CommonMark compliant Markdown formatter.", manager = "pypi", pkg = "mdformat",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"mdformat"},
    version = "1.0.0", -- snapshot pin
  },
  {
    id = "mdsf", display = "mdsf", detail = "Format markdown code blocks using your favorite code formatters.", manager = "cargo", pkg = "mdsf",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"mdsf"},
    version = "0.12.1", -- snapshot pin
  },
  {
    id = "mdslw", display = "mdslw", detail = "The MarkDown Sentence Line Wrapper, an auto-formatter that prepares your markdown for easy diff'ing.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"mdslw"},
    repo = "razziel89/mdslw", asset = {
      mac = { match = "mdslw_x86_64\\-apple\\-darwin", archive = "none" },
      win = { match = "mdslw_x86_64\\-pc\\-windows\\-gnu\\.exe", archive = "none" },
    },
    version = "0.17.2", -- snapshot pin
  },
  {
    id = "mdx-analyzer", display = "mdx-analyzer", detail = "This package provides a language server for MDX. The language server provides IntelliSense based on TypeScript, as we...", manager = "npm", pkg = "%40mdx-js/language-server",
    categories = {"LSP"},
    languages = {"MDX"},
    bin = {"mdx-language-server"},
    version = "0.6.4", -- snapshot pin
  },
  {
    id = "mesonlsp", display = "mesonlsp", detail = "An unofficial, unendorsed language server for meson written in C++", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Meson"},
    bin = {"mesonlsp"},
    repo = "JCWasmx86/mesonlsp", asset = {
      mac = { match = "mesonlsp\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "mesonlsp\\-x86_64\\-pc\\-windows\\-gnu\\.zip", archive = "zip" },
    },
    version = "v5.0.4", -- snapshot pin
  },
  {
    id = "millet", display = "millet", detail = "A language server for Standard ML.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Standard ML"},
    bin = {"millet"},
    repo = "azdavis/millet", asset = {
      linux = { match = "millet\\-ls\\-x86_64\\-unknown\\-linux\\-gnu\\.gz", archive = "gz" },
      mac = { match = "millet\\-ls\\-aarch64\\-apple\\-darwin\\.gz", archive = "gz" },
      win = { match = "millet\\-ls\\-x86_64\\-pc\\-windows\\-msvc\\.gz", archive = "gz" },
    },
    version = "v0.15.2", -- snapshot pin
  },
  {
    id = "miss_hit", display = "miss_hit", detail = "Free and open source code quality tools for MATLAB and Octave.", manager = "pypi", pkg = "miss-hit",
    categories = {"Formatter", "Linter"},
    languages = {"Matlab", "Octave"},
    bin = {"mh_style", "mh_lint"},
    version = "0.9.44", -- snapshot pin
  },
  {
    id = "misspell", display = "misspell", detail = "Correct commonly misspelled English words in source files.", manager = "golang", pkg = "github.com/client9/misspell",
    categories = {"Linter"},
    bin = {"misspell"},
    version = "v0.3.4#cmd/misspell", -- snapshot pin
  },
  {
    id = "mmdc", display = "mmdc", detail = "Command line tool for the Mermaid library.", manager = "npm", pkg = "@mermaid-js/mermaid-cli",
    languages = {"Markdown", "Mermaid"},
    bin = {"mmdc"},
    version = "11.17.0", -- snapshot pin
  },
  {
    id = "motoko-lsp", display = "motoko-lsp", detail = "Language server for the Motoko programming language.", manager = "openvsx", pkg = "",
    categories = {"LSP"},
    languages = {"Motoko"},
    bin = {"motoko-lsp"},
    openvsx = { ns = "dfinity-foundation", ext = "vscode-motoko", version = "0.23.0", file = "dfinity-foundation.vscode-motoko-{{version}}.vsix" },
    version = "0.23.0", -- snapshot pin
    runs = {
      ["motoko-lsp"] = { kind = "node", hint = "extension/out/server.js" },
    },
  },
  {
    id = "move-analyzer", display = "move-analyzer", detail = "move-analyzer is a language server implementation for the Move programming language.", manager = "cargo", pkg = "move-analyzer",
    categories = {"LSP"},
    languages = {"Move"},
    bin = {"move-analyzer"},
    extras = {["repository_url"] = "https://github.com/move-language/move", ["rev"] = "true", ["locked"] = "false"},
    version = "ea70797099baea64f05194a918cebd69ed02b285", -- snapshot pin
  },
  {
    id = "mpls", display = "mpls", detail = "Markdown Preview Language Server", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"mpls"},
    repo = "mhersson/mpls", asset = {
      linux = { match = "mpls_.*_linux_amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "mpls_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "mpls_.*_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.22.0", -- snapshot pin
  },
  {
    id = "ms-terraform-lsp", display = "ms-terraform-lsp", detail = "Microsoft Terraform Providers Language Server. Completion, hover documentation and schema validation for the azapi, a...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Terraform"},
    bin = {"ms-terraform-lsp"},
    repo = "Azure/ms-terraform-lsp", asset = {
      mac = { match = "ms\\-terraform\\-lsp_.*_darwin_arm64\\.zip", archive = "zip" },
      win = { match = "ms\\-terraform\\-lsp_.*_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.10.0", -- snapshot pin
  },
  {
    id = "mutt-language-server", display = "mutt-language-server", detail = "A language server for (neo)mutt's muttrc.", manager = "pypi", pkg = "mutt-language-server",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"mutt-language-server"},
    version = "0.1.2", -- snapshot pin
  },
  {
    id = "mypy", display = "mypy", detail = "Mypy is a static type checker for Python.", manager = "pypi", pkg = "mypy",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"mypy", "dmypy", "mypyc"},
    version = "2.3.1", -- snapshot pin
  },
  {
    id = "neocmakelsp", display = "neocmakelsp", detail = "CMake LSP implementation based on Tower and Tree-sitter.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"CMake"},
    bin = {"neocmakelsp"},
    repo = "neocmakelsp/neocmakelsp", asset = {
      linux = { match = "neocmakelsp\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "neocmakelsp\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.11.1", -- snapshot pin
  },
  {
    id = "netcoredbg", display = "netcoredbg", detail = "NetCoreDbg is a managed code debugger with MI interface for CoreCLR.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {".NET", "C#", "F#"},
    bin = {"netcoredbg"},
    repo = "Samsung/netcoredbg", asset = {
      linux = { match = "netcoredbg\\-linux\\-amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "netcoredbg\\-osx\\-amd64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "netcoredbg\\-win64\\.zip", archive = "zip" },
    },
    version = "3.1.3-1062", -- snapshot pin
  },
  {
    id = "nextflow-language-server", display = "nextflow-language-server", detail = "A language server for Nextflow.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Nextflow"},
    bin = {"nextflow-language-server"},
    repo = "nextflow-io/language-server", asset = {
      linux = { match = "language\\-server\\-all\\.jar", archive = "none" },
      mac = { match = "language\\-server\\-all\\.jar", archive = "none" },
      win = { match = "language\\-server\\-all\\.jar", archive = "none" },
    },
    version = "v25.10.3", -- snapshot pin
    runs = {
      ["nextflow-language-server"] = { kind = "jar", hint = "language-server-all.jar" },
    },
  },
  {
    id = "nextls", display = "nextls", detail = "NextLS is the language server for Elixir that just works.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Elixir"},
    bin = {"nextls"},
    repo = "elixir-tools/next-ls", asset = {
      mac = { match = "next_ls_darwin_arm64", archive = "none" },
      win = { match = "next_ls_windows_amd64\\.exe", archive = "none" },
    },
    version = "v0.23.4", -- snapshot pin
  },
  {
    id = "nginx-config-formatter", display = "nginx-config-formatter", detail = "nginx config file formatter/beautifier written in Python with no additional dependencies.", manager = "pypi", pkg = "nginxfmt",
    categories = {"Formatter"},
    languages = {"nginx"},
    bin = {"nginxfmt"},
    version = "1.4.0", -- snapshot pin
  },
  {
    id = "nginx-language-server", display = "nginx-language-server", detail = "A language server for nginx configuration files.", manager = "pypi", pkg = "nginx-language-server",
    categories = {"LSP"},
    languages = {"nginx"},
    bin = {"nginx-language-server"},
    version = "0.9.0", -- snapshot pin
  },
  {
    id = "nickel-lang-lsp", display = "nickel-lang-lsp", detail = "The Nickel Language Server (NLS) is a language server for the Nickel programming language. NLS offers error messages,...", manager = "cargo", pkg = "nickel-lang-lsp",
    categories = {"LSP"},
    languages = {"Nickel"},
    bin = {"nls"},
    version = "1.17.0", -- snapshot pin
  },
  {
    id = "nil", display = "nil", detail = "Language Server for Nix.", manager = "cargo", pkg = "nil",
    categories = {"LSP"},
    languages = {"Nix"},
    bin = {"nil"},
    extras = {["repository_url"] = "https://github.com/oxalica/nil"},
    version = "2025-06-13", -- snapshot pin
  },
  {
    id = "nimlangserver", display = "nimlangserver", detail = "The Nim language server implementation (based on nimsuggest)", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Nim"},
    bin = {"nimlangserver"},
    repo = "nim-lang/langserver", asset = {
      mac = { match = "nimlangserver\\-macos\\-arm64\\.zip", archive = "zip" },
      win = { match = "nimlangserver\\-windows\\-amd64\\.zip", archive = "zip" },
    },
    version = "v1.14.0", -- snapshot pin
  },
  {
    id = "nixpkgs-fmt", display = "nixpkgs-fmt", detail = "Nix code formatter for nixpkgs", manager = "cargo", pkg = "nixpkgs-fmt",
    categories = {"Formatter"},
    languages = {"Nix"},
    bin = {"nixpkgs-fmt"},
    version = "1.3.0", -- snapshot pin
  },
  {
    id = "nomad", display = "nomad", detail = "Nomad is an easy-to-use, flexible, and performant workload orchestrator that can deploy a mix of microservice, batch,...", manager = "generic", pkg = "",
    categories = {"Formatter", "Linter", "Runtime"},
    languages = {"Nomad"},
    bin = {"nomad"},
    dl = {
      linux = { files = {["nomad.zip"] = "https://releases.hashicorp.com/nomad/2.0.5/nomad_2.0.5_linux_amd64.zip"}, bin = "nomad" },
      mac = { files = {["nomad.zip"] = "https://releases.hashicorp.com/nomad/2.0.5/nomad_2.0.5_darwin_amd64.zip"}, bin = "nomad" },
      win = { files = {["nomad"] = "https://releases.hashicorp.com/nomad/2.0.5/nomad_2.0.5_windows_amd64.zip"}, bin = "nomad" },
    },
    version = "v2.0.5", -- snapshot pin
  },
  {
    id = "nomicfoundation-solidity-language-server", display = "nomicfoundation-solidity-language-server", detail = "Solidity language server by NomicFoundation", manager = "npm", pkg = "%40nomicfoundation/solidity-language-server",
    categories = {"LSP"},
    languages = {"Solidity"},
    bin = {"nomicfoundation-solidity-language-server"},
    version = "0.8.25", -- snapshot pin
  },
  {
    id = "npm-groovy-lint", display = "npm-groovy-lint", detail = "Lint, format and auto-fix your Groovy / Jenkinsfile / Gradle files using command line.", manager = "npm", pkg = "npm-groovy-lint",
    categories = {"Linter", "Formatter"},
    languages = {"Groovy"},
    bin = {"npm-groovy-lint"},
    version = "18.0.0", -- snapshot pin
  },
  {
    id = "ntt", display = "ntt", detail = "Modern tools for TTCN-3", manager = "github", pkg = "",
    categories = {"LSP", "Runtime"},
    languages = {"TTCN-3"},
    bin = {"ntt"},
    repo = "nokia/ntt", asset = {
      mac = { match = "ntt_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ntt_windows_x86_64\\.zip", archive = "zip" },
    },
    version = "v0.23.2", -- snapshot pin
  },
  {
    id = "nxls", display = "nxls", detail = "A language server that provides code completion and more for Nx workspaces.", manager = "npm", pkg = "nxls",
    categories = {"LSP"},
    languages = {"JSON"},
    bin = {"nxls"},
    version = "1.11.2", -- snapshot pin
  },
  {
    id = "ocaml-lsp", display = "ocaml-lsp", detail = "OCaml Language Server Protocol implementation.", manager = "opam", pkg = "ocaml-lsp-server",
    categories = {"LSP"},
    languages = {"OCaml"},
    bin = {"ocamllsp"},
    version = "1.27.0", -- snapshot pin
  },
  {
    id = "ocamlearlybird", display = "ocamlearlybird", detail = "OCaml debug adapter.", manager = "opam", pkg = "earlybird",
    categories = {"DAP"},
    languages = {"OCaml"},
    bin = {"ocamlearlybird"},
    version = "1.3.3", -- snapshot pin
  },
  {
    id = "ocamlformat", display = "ocamlformat", detail = "ocamlformat is a tool for formatting OCaml code. It automatically adjusts the layout of your code to follow the recom...", manager = "opam", pkg = "ocamlformat",
    categories = {"Formatter"},
    languages = {"OCaml"},
    bin = {"ocamlformat"},
    version = "0.29.0", -- snapshot pin
  },
  {
    id = "oelint-adv", display = "oelint-adv", detail = "Linter for bitbake recipes.", manager = "pypi", pkg = "oelint-adv",
    categories = {"Linter"},
    languages = {"BitBake"},
    bin = {"oelint-adv"},
    version = "9.11.2", -- snapshot pin
  },
  {
    id = "ols", display = "ols", detail = "Language server for Odin. This project is still in early development.", manager = "github", pkg = "",
    categories = {"LSP", "Formatter"},
    languages = {"Odin"},
    bin = {"ols", "odinfmt"},
    repo = "DanielGavin/ols", asset = {
      linux = { match = "ols\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "ols\\-arm64\\-darwin\\.zip", archive = "zip" },
      win = { match = "ols\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "nightly", -- snapshot pin
  },
  {
    id = "omnisharp", display = "omnisharp", detail = "OmniSharp language server based on Roslyn workspaces. This version of Omnisharp requires dotnet (.NET 6.0) to be inst...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C#"},
    bin = {"OmniSharp"},
    repo = "OmniSharp/omnisharp-roslyn", asset = {
      mac = { match = "omnisharp\\-osx\\-arm64\\-net6\\.0\\.zip", archive = "zip" },
      win = { match = "omnisharp\\-win\\-x64\\-net6\\.0\\.zip", archive = "zip" },
    },
    version = "v1.39.15", -- snapshot pin
    runs = {
      ["OmniSharp"] = { kind = "dotnet", hint = "libexec/OmniSharp.dll" },
    },
  },
  {
    id = "omnisharp-mono", display = "omnisharp-mono", detail = "OmniSharp language server based on Roslyn workspaces. This version of Omnisharp requires Mono to be installed on Linu...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"C#"},
    bin = {"omnisharp-mono"},
    repo = "OmniSharp/omnisharp-roslyn", asset = {
      mac = { match = "omnisharp\\-osx\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "omnisharp\\-win\\-x64\\.zip", archive = "zip" },
    },
    version = "v1.39.15", -- snapshot pin
  },
  {
    id = "opa", display = "opa", detail = "Open Policy Agent (OPA) is an open source, general-purpose policy engine.", manager = "github", pkg = "",
    categories = {"Linter", "Formatter", "Compiler", "Runtime"},
    languages = {"Rego"},
    bin = {"opa"},
    repo = "open-policy-agent/opa", asset = {
      mac = { match = "opa_darwin_arm64_static", archive = "none" },
      win = { match = "opa_windows_amd64\\.exe", archive = "none" },
    },
    version = "v1.20.2", -- snapshot pin
  },
  {
    id = "opencl-language-server", display = "opencl-language-server", detail = "Provides an OpenCL kernel diagnostics.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"OpenCL"},
    bin = {"opencl-language-server"},
    repo = "Galarius/opencl-language-server", asset = {
      mac = { match = "opencl\\-language\\-server\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "opencl\\-language\\-server\\-win32\\-x86_64\\.zip", archive = "zip" },
    },
    version = "0.8.0", -- snapshot pin
  },
  {
    id = "openedge-language-server", display = "openedge-language-server", detail = "OpenEdge Language Server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Progress", "OpenEdge"},
    bin = {},
    repo = "vscode-abl/vscode-abl", asset = {
      linux = { match = "abl\\-lsp\\.jar", archive = "none" },
      mac = { match = "abl\\-lsp\\.jar", archive = "none" },
      win = { match = "abl\\-lsp\\.jar", archive = "none" },
    },
    version = "V1.4.21", -- snapshot pin
  },
  {
    id = "openscad-language-server", display = "openscad-language-server", detail = "A Language Server Protocol server for OpenSCAD", manager = "cargo", pkg = "openscad-language-server",
    categories = {"LSP"},
    languages = {"OpenSCAD"},
    bin = {"openscad-language-server"},
    version = "0.1.0", -- snapshot pin
  },
  {
    id = "openscad-lsp", display = "openscad-lsp", detail = "Language Server Protocol implementation for OpenSCAD, written in Rust.", manager = "cargo", pkg = "openscad-lsp",
    categories = {"LSP"},
    languages = {"OpenSCAD"},
    bin = {"openscad-lsp"},
    version = "2.0.2", -- snapshot pin
  },
  {
    id = "ormolu", display = "ormolu", detail = "A formatter for Haskell source code.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Haskell"},
    bin = {"ormolu"},
    repo = "tweag/ormolu", asset = {
      mac = { match = "ormolu\\-aarch64\\-darwin\\.zip", archive = "zip" },
      win = { match = "ormolu\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "0.8.1.1", -- snapshot pin
  },
  {
    id = "oxfmt", display = "oxfmt", detail = "Prettier-compatible code formatter powered by Oxc", manager = "npm", pkg = "oxfmt",
    categories = {"Formatter"},
    languages = {"Angular", "CSS", "Flow", "GraphQL", "HTML", "JSON", "JSX", "JavaScript", "LESS", "Markdown", "SCSS", "TypeScript", "Vue", "YAML"},
    bin = {"oxfmt"},
    version = "0.66.0", -- snapshot pin
  },
  {
    id = "oxlint", display = "oxlint", detail = "High-performance linter for JavaScript and TypeScript written in Rust.", manager = "npm", pkg = "oxlint",
    categories = {"LSP", "Linter"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"oxlint"},
    version = "1.81.0", -- snapshot pin
  },
  {
    id = "palantir-java-format", display = "palantir-java-format", detail = "A modern, lambda-friendly, 120 character Java formatter.", manager = "generic", pkg = "",
    categories = {"Formatter"},
    languages = {"Java"},
    bin = {"palantir-java-format"},
    dl = {
      linux = { files = {["palantir-java-format"] = "https://repo1.maven.org/maven2/com/palantir/javaformat/palantir-java-format-native/2.97.0/palantir-java-format-native-2.97.0-nativeImage-linux-glibc_x86-64.bin"}, bin = "palantir-java-format" },
      mac = { files = {["palantir-java-format"] = "https://repo1.maven.org/maven2/com/palantir/javaformat/palantir-java-format-native/2.97.0/palantir-java-format-native-2.97.0-nativeImage-macos_aarch64.bin"}, bin = "palantir-java-format" },
    },
    version = "2.97.0", -- snapshot pin
  },
  {
    id = "panache", display = "panache", detail = "An LSP, formatter, and linter for Markdown, Quarto, and R Markdown.", manager = "github", pkg = "",
    categories = {"LSP", "Formatter", "Linter"},
    languages = {"Markdown", "Quarto", "R Markdown"},
    bin = {"panache"},
    repo = "jolars/panache", asset = {
      linux = { match = "panache\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "panache\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "panache\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v3.8.0", -- snapshot pin
  },
  {
    id = "pbls", display = "pbls", detail = "A language server implementation for Google Protocol Buffers.", manager = "cargo", pkg = "pbls",
    categories = {"LSP"},
    languages = {"Protobuf"},
    bin = {"pbls"},
    extras = {["repository_url"] = "https://git.sr.ht/~rrc/pbls"},
    version = "1.0.1", -- snapshot pin
  },
  {
    id = "perlnavigator", display = "perlnavigator", detail = "Perl Language Server that includes perl critic and code navigation.", manager = "npm", pkg = "perlnavigator-server",
    categories = {"LSP"},
    languages = {"Perl"},
    bin = {"perlnavigator"},
    version = "0.8.20", -- snapshot pin
    runs = {
      ["perlnavigator"] = { kind = "node", hint = "node_modules/perlnavigator-server/out/server.js" },
    },
  },
  {
    id = "pest-language-server", display = "pest-language-server", detail = "A language server for Pest grammar.", manager = "cargo", pkg = "pest-language-server",
    categories = {"LSP"},
    languages = {"Pest"},
    bin = {"pest-language-server"},
    version = "0.3.14", -- snapshot pin
  },
  {
    id = "php", display = "intelephense", detail = "Professional PHP tooling for any Language Server Protocol capable editor.", manager = "npm", pkg = "intelephense",
    categories = {"LSP"},
    languages = {"PHP"},
    aliases = {"intelephense", "intelephense"},
    bin = {"intelephense"},
    version = "1.18.5", -- snapshot pin
  },
  {
    id = "php-cs-fixer", display = "php-cs-fixer", detail = "The PHP Coding Standards Fixer (PHP CS Fixer) tool fixes your code to follow standards; whether you want to follow PH...", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"PHP"},
    bin = {"php-cs-fixer"},
    repo = "PHP-CS-Fixer/PHP-CS-Fixer", asset = {
      linux = { match = "php\\-cs\\-fixer\\.phar", archive = "none" },
      mac = { match = "php\\-cs\\-fixer\\.phar", archive = "none" },
      win = { match = "php\\-cs\\-fixer\\.phar", archive = "none" },
    },
    version = "v3.95.24", -- snapshot pin
    runs = {
      ["php-cs-fixer"] = { kind = "php", hint = "php-cs-fixer.phar" },
    },
  },
  {
    id = "php-debug-adapter", display = "php-debug-adapter", detail = "PHP Debug Adapter 🐞⛔.", manager = "github", pkg = "",
    categories = {"DAP"},
    languages = {"PHP"},
    bin = {"php-debug-adapter"},
    repo = "xdebug/vscode-php-debug", asset = {
      linux = { match = "php\\-debug\\-.*\\.vsix", archive = "none" },
      mac = { match = "php\\-debug\\-.*\\.vsix", archive = "none" },
      win = { match = "php\\-debug\\-.*\\.vsix", archive = "none" },
    },
    version = "v1.40.1", -- snapshot pin
    runs = {
      ["php-debug-adapter"] = { kind = "node", hint = "extension/out/phpDebug.js" },
    },
  },
  {
    id = "phpactor", display = "phpactor", detail = "Phpactor is an intelligent Completion and Refactoring tool for PHP which is available over it’s own RPC protocol and ...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"PHP"},
    bin = {"phpactor"},
    repo = "phpactor/phpactor", asset = {
      linux = { match = "phpactor\\.phar", archive = "none" },
      mac = { match = "phpactor\\.phar", archive = "none" },
      win = { match = "phpactor\\.phar", archive = "none" },
    },
    version = "2026.07.22.0", -- snapshot pin
    runs = {
      ["phpactor"] = { kind = "php", hint = "phpactor.phar" },
    },
  },
  {
    id = "phpantom_lsp", display = "phpantom_lsp", detail = "A lightweight PHP language server with deep type intelligence — generics, Laravel support, and PHPStan annotations. W...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"PHP"},
    bin = {"phpantom_lsp"},
    repo = "PHPantom-dev/phpantom_lsp", asset = {
      linux = { match = "phpantom_lsp\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "phpantom_lsp\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "phpantom_lsp\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.10.0", -- snapshot pin
  },
  {
    id = "phpcbf", display = "phpcbf", detail = "phpcbf automatically corrects coding standard violations that would be detected by phpcs.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"PHP"},
    bin = {"phpcbf"},
    repo = "PHPCSStandards/PHP_CodeSniffer", asset = {
      linux = { match = "phpcbf\\.phar", archive = "none" },
      mac = { match = "phpcbf\\.phar", archive = "none" },
      win = { match = "phpcbf\\.phar", archive = "none" },
    },
    version = "4.0.4", -- snapshot pin
    runs = {
      ["phpcbf"] = { kind = "php", hint = "phpcbf.phar" },
    },
  },
  {
    id = "phpcs", display = "phpcs", detail = "phpcs tokenizes PHP, JavaScript and CSS files to detect violations of a defined standard.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"PHP"},
    bin = {"phpcs"},
    repo = "PHPCSStandards/PHP_CodeSniffer", asset = {
      linux = { match = "phpcs\\.phar", archive = "none" },
      mac = { match = "phpcs\\.phar", archive = "none" },
      win = { match = "phpcs\\.phar", archive = "none" },
    },
    version = "4.0.4", -- snapshot pin
    runs = {
      ["phpcs"] = { kind = "php", hint = "phpcs.phar" },
    },
  },
  {
    id = "phpmd", display = "phpmd", detail = "PHPMD is a spin-off project of PHP Depend and aims to be a PHP equivalent of the well known Java tool PMD. PHPMD can ...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"PHP"},
    bin = {"phpmd"},
    repo = "phpmd/phpmd", asset = {
      linux = { match = "phpmd\\.phar", archive = "none" },
      mac = { match = "phpmd\\.phar", archive = "none" },
      win = { match = "phpmd\\.phar", archive = "none" },
    },
    version = "2.15.0", -- snapshot pin
    runs = {
      ["phpmd"] = { kind = "php", hint = "phpmd.phar" },
    },
  },
  {
    id = "phpstan", display = "phpstan", detail = "PHP Static Analysis Tool - discover bugs in your code without running it!", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"PHP"},
    bin = {"phpstan"},
    repo = "phpstan/phpstan", asset = {
      linux = { match = "phpstan\\.phar", archive = "none" },
      mac = { match = "phpstan\\.phar", archive = "none" },
      win = { match = "phpstan\\.phar", archive = "none" },
    },
    version = "2.2.13", -- snapshot pin
    runs = {
      ["phpstan"] = { kind = "php", hint = "phpstan.phar" },
    },
  },
  {
    id = "pinact", display = "pinact", detail = "Update & pin GitHub Workflows and Actions to commit SHAs", manager = "github", pkg = "",
    languages = {"YAML"},
    bin = {"pinact"},
    repo = "suzuki-shunsuke/pinact", asset = {
      mac = { match = "pinact_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "pinact_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v4.1.1", -- snapshot pin
  },
  {
    id = "pint", display = "pint", detail = "Laravel Pint is an opinionated PHP code style fixer for minimalists.", manager = "composer", pkg = "laravel/pint",
    categories = {"Formatter"},
    languages = {"PHP"},
    bin = {"pint"},
    version = "v1.30.5", -- snapshot pin
  },
  {
    id = "pkl-lsp", display = "pkl-lsp", detail = "Language server for Pkl, implementing the server-side of the Language Server Protocol.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Pkl"},
    bin = {"pkl-lsp"},
    repo = "apple/pkl-lsp", asset = {
      linux = { match = "pkl\\-lsp\\-.*\\.jar", archive = "none" },
      mac = { match = "pkl\\-lsp\\-.*\\.jar", archive = "none" },
      win = { match = "pkl\\-lsp\\-.*\\.jar", archive = "none" },
    },
    version = "0.8.0", -- snapshot pin
    runs = {
      ["pkl-lsp"] = { kind = "jar", hint = "{{source.asset.file}}" },
    },
  },
  {
    id = "postgres-language-server", display = "postgres-language-server", detail = "A collection of language tools and a Language Server Protocol (LSP) implementation for Postgres, focusing on develope...", manager = "github", pkg = "",
    categories = {"LSP", "Linter"},
    languages = {"Postgres", "SQL"},
    bin = {"postgres-language-server"},
    repo = "supabase-community/postgres-language-server", asset = {
      linux = { match = "postgres\\-language\\-server_x86_64\\-unknown\\-linux\\-gnu", archive = "none" },
      mac = { match = "postgres\\-language\\-server_aarch64\\-apple\\-darwin", archive = "none" },
      win = { match = "postgres\\-language\\-server_x86_64\\-pc\\-windows\\-msvc\\.exe", archive = "none" },
    },
    version = "0.25.7", -- snapshot pin
  },
  {
    id = "powershell-editor-services", display = "powershell-editor-services", detail = "A common platform for PowerShell development support in any editor or application.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"PowerShell"},
    bin = {},
    repo = "PowerShell/PowerShellEditorServices", asset = {
      linux = { match = "PowerShellEditorServices\\.zip", archive = "zip" },
      mac = { match = "PowerShellEditorServices\\.zip", archive = "zip" },
      win = { match = "PowerShellEditorServices\\.zip", archive = "zip" },
    },
    version = "v4.7.0", -- snapshot pin
  },
  {
    id = "prettier", display = "prettier", detail = "Prettier is an opinionated code formatter.", manager = "npm", pkg = "prettier",
    categories = {"Formatter"},
    languages = {"Angular", "CSS", "Flow", "GraphQL", "HTML", "JSON", "JSX", "JavaScript", "LESS", "Markdown", "SCSS", "TypeScript", "Vue", "YAML"},
    bin = {"prettier"},
    version = "3.9.6", -- snapshot pin
  },
  {
    id = "prettierd", display = "prettierd", detail = "Prettier, as a daemon, for ludicrous formatting speed.", manager = "npm", pkg = "%40fsouza/prettierd",
    categories = {"Formatter"},
    languages = {"Angular", "CSS", "Flow", "GraphQL", "HTML", "JSON", "JSX", "JavaScript", "LESS", "Markdown", "SCSS", "TypeScript", "Vue", "YAML"},
    bin = {"prettierd"},
    version = "0.29.0", -- snapshot pin
  },
  {
    id = "pretty-php", display = "pretty-php", detail = "The opinionated PHP code formatter", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"PHP"},
    bin = {"pretty-php"},
    repo = "lkrms/pretty-php", asset = {
      linux = { match = "pretty\\-php\\.phar", archive = "none" },
      mac = { match = "pretty\\-php\\.phar", archive = "none" },
      win = { match = "pretty\\-php\\.phar", archive = "none" },
    },
    version = "v0.4.95", -- snapshot pin
    runs = {
      ["pretty-php"] = { kind = "php", hint = "pretty-php.phar" },
    },
  },
  {
    id = "prettydiff", display = "prettydiff", detail = "Beautifier and language aware code comparison tool for many languages. It also minifies and a few other things.", manager = "npm", pkg = "prettydiff",
    categories = {"Formatter"},
    languages = {"HTML"},
    bin = {"prettydiff"},
    version = "101.2.6", -- snapshot pin
  },
  {
    id = "prettypst", display = "prettypst", detail = "Formatter for Typst", manager = "cargo", pkg = "prettypst",
    categories = {"Formatter"},
    languages = {"Typst"},
    bin = {"prettypst"},
    extras = {["repository_url"] = "https://github.com/antonWetzel/prettypst"},
    version = "2.0.0", -- snapshot pin
  },
  {
    id = "prisma-language-server", display = "prisma-language-server", detail = "Any editor that is compatible with the Language Server Protocol can create clients that can use the features provided...", manager = "npm", pkg = "%40prisma/language-server",
    categories = {"LSP"},
    languages = {"Prisma"},
    bin = {"prisma-language-server"},
    version = "31.12.0", -- snapshot pin
  },
  {
    id = "prometheus-pint", display = "prometheus-pint", detail = "Prometheus rule linter/validator", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"PromQL"},
    bin = {"prometheus-pint"},
    repo = "cloudflare/pint", asset = {
      mac = { match = "pint\\-.*\\-darwin\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "pint\\-.*\\-windows\\-amd64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.87.0", -- snapshot pin
  },
  {
    id = "proselint", display = "proselint", detail = "proselint is a linter for English prose. It places the world's greatest writers and editors by your side, where they ...", manager = "pypi", pkg = "proselint",
    categories = {"Linter"},
    languages = {"Text", "Markdown"},
    bin = {"proselint"},
    version = "0.16.0", -- snapshot pin
  },
  {
    id = "prosemd-lsp", display = "prosemd-lsp", detail = "An experimental proofreading and linting language server for markdown files.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"prosemd-lsp"},
    repo = "kitten/prosemd-lsp", asset = {
      linux = { match = "prosemd\\-lsp\\-linux", archive = "none" },
      mac = { match = "prosemd\\-lsp\\-macos", archive = "none" },
      win = { match = "prosemd\\-lsp\\-windows\\.exe", archive = "none" },
    },
    version = "v0.1.0", -- snapshot pin
  },
  {
    id = "protolint", display = "protolint", detail = "protolint is the pluggable linting/fixing utility for Protocol Buffer files (proto2+proto3).", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Protobuf"},
    bin = {"protolint", "protoc-gen-protolint"},
    repo = "yoheimuta/protolint", asset = {
      mac = { match = "protolint_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "protolint_.*_windows_amd64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.57.0", -- snapshot pin
  },
  {
    id = "protols", display = "protols", detail = "A Simple LSP for proto3 protobuf files.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Protobuf"},
    bin = {"protols"},
    repo = "coder3101/protols", asset = {
      mac = { match = "protols\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "protols\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.14.1", -- snapshot pin
  },
  {
    id = "pug-lsp", display = "pug-lsp", detail = "An implementation of the Language Protocol Server for [Pug.js](http://pugjs.org)", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Pug"},
    bin = {"pug-lsp"},
    repo = "opa-oz/pug-lsp", asset = {
      mac = { match = "pug\\-lsp_darwin_all\\.zip", archive = "zip" },
      win = { match = "pug\\-lsp_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.1.0", -- snapshot pin
  },
  {
    id = "puppet-editor-services", display = "puppet-editor-services", detail = "Puppet Language Server for editors.", manager = "github", pkg = "",
    categories = {"LSP", "DAP"},
    languages = {"Puppet"},
    bin = {"puppet-languageserver", "puppet-debugserver"},
    repo = "puppetlabs/puppet-editor-services", asset = {
      linux = { match = "puppet_editor_services_.*\\.zip", archive = "zip" },
      mac = { match = "puppet_editor_services_.*\\.zip", archive = "zip" },
      win = { match = "puppet_editor_services_.*\\.zip", archive = "zip" },
    },
    version = "v2.0.4", -- snapshot pin
    runs = {
      ["puppet-languageserver"] = { kind = "ruby", hint = "libexec/puppet-languageserver" },
      ["puppet-debugserver"] = { kind = "ruby", hint = "libexec/puppet-debugserver" },
    },
  },
  {
    id = "purescript-language-server", display = "purescript-language-server", detail = "Node-based Language Server Protocol server for PureScript based on the PureScript IDE server (aka psc-ide / purs ide ...", manager = "npm", pkg = "purescript-language-server",
    categories = {"LSP"},
    languages = {"PureScript"},
    bin = {"purescript-language-server"},
    version = "0.18.5", -- snapshot pin
  },
  {
    id = "purescript-tidy", display = "purescript-tidy", detail = "A syntax tidy-upper (formatter) for PureScript.", manager = "npm", pkg = "purs-tidy",
    categories = {"Formatter"},
    languages = {"PureScript"},
    bin = {"purs-tidy"},
    version = "0.11.1", -- snapshot pin
  },
  {
    id = "pydoclint", display = "pydoclint", detail = "A very fast Python docstring linter.", manager = "pypi", pkg = "pydoclint",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pydoclint"},
    version = "0.9.1", -- snapshot pin
  },
  {
    id = "pydocstyle", display = "pydocstyle", detail = "pydocstyle is a static analysis tool for checking compliance with Python docstring conventions.", manager = "pypi", pkg = "pydocstyle",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pydocstyle"},
    extras = {["extra"] = "toml"},
    version = "6.3.0", -- snapshot pin
  },
  {
    id = "pyflakes", display = "pyflakes", detail = "A simple program which checks Python source files for errors.  Pyflakes analyzes programs and detects various errors....", manager = "pypi", pkg = "pyflakes",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pyflakes"},
    version = "3.4.0", -- snapshot pin
  },
  {
    id = "pyink", display = "pyink", detail = "Pyink is a Python formatter, forked from Black with a few different formatting behaviors.", manager = "pypi", pkg = "pyink",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"pyink"},
    version = "26.5.1", -- snapshot pin
  },
  {
    id = "pylama", display = "pylama", detail = "Code audit tool for Python.", manager = "pypi", pkg = "pylama",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pylama"},
    extras = {["extra"] = "all"},
    version = "8.4.1", -- snapshot pin
  },
  {
    id = "pylint", display = "pylint", detail = "Pylint is a static code analyser for Python 2 or 3.", manager = "pypi", pkg = "pylint",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pylint"},
    version = "4.0.8", -- snapshot pin
  },
  {
    id = "pylyzer", display = "pylyzer", detail = "A fast static code analyzer & language server for Python.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"pylyzer"},
    repo = "mtshiba/pylyzer", asset = {
      mac = { match = "pylyzer\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "pylyzer\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.0.82", -- snapshot pin
  },
  {
    id = "pymarkdownlnt", display = "pymarkdownlnt", detail = "PyMarkdown is primarily a Markdown linter.", manager = "pypi", pkg = "pymarkdownlnt",
    categories = {"Linter"},
    languages = {"Markdown"},
    bin = {"pymarkdownlnt"},
    version = "0.9.39", -- snapshot pin
  },
  {
    id = "pyment", display = "pyment", detail = "Create, update or convert docstrings in existing Python files, managing several styles.", manager = "pypi", pkg = "pyment",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"pyment"},
    version = "0.3.3", -- snapshot pin
  },
  {
    id = "pymobiledevice3", display = "pymobiledevice3", detail = "Pure python3 implementation for working with iDevices (iPhone, etc...).", manager = "pypi", pkg = "pymobiledevice3",
    categories = {"Runtime"},
    languages = {"Python"},
    bin = {"pymobiledevice3"},
    version = "11.3.1", -- snapshot pin
  },
  {
    id = "pyproject-flake8", display = "pyproject-flake8", detail = "A monkey patching wrapper to connect flake8 with pyproject.toml configuration.", manager = "pypi", pkg = "pyproject-flake8",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"pflake8"},
    version = "7.0.0", -- snapshot pin
  },
  {
    id = "pyproject-fmt", display = "pyproject-fmt", detail = "Format your pyproject.toml file", manager = "pypi", pkg = "pyproject-fmt",
    categories = {"Formatter"},
    languages = {"Python", "TOML"},
    bin = {"pyproject-fmt"},
    version = "2.29.3", -- snapshot pin
  },
  {
    id = "pyre", display = "pyre", detail = "Pyre is a performant type checker for Python compliant with PEP 484.", manager = "pypi", pkg = "pyre-check",
    categories = {"LSP", "Linter"},
    languages = {"Python"},
    bin = {"pyre"},
    version = "0.9.25", -- snapshot pin
  },
  {
    id = "pyrefly", display = "pyrefly", detail = "Pyrefly, a faster Python type checker written in Rust", manager = "pypi", pkg = "pyrefly",
    categories = {"Linter", "LSP"},
    languages = {"Python"},
    bin = {"pyrefly"},
    version = "1.2.0", -- snapshot pin
  },
  {
    id = "pyright", display = "pyright", detail = "Static type checker for Python.", manager = "npm", pkg = "pyright",
    categories = {"LSP"},
    languages = {"Python"},
    aliases = {"pyright"},
    bin = {"pyright", "pyright-langserver"},
    version = "1.1.413", -- snapshot pin
  },
  {
    id = "pytest-language-server", display = "pytest-language-server", detail = "A language server for pytest", manager = "pypi", pkg = "pytest-language-server",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"pytest-language-server"},
    version = "0.24.0", -- snapshot pin
  },
  {
    id = "python", display = "python-lsp-server", detail = "Fork of the python-language-server project, maintained by the Spyder IDE team and the community.", manager = "pypi", pkg = "python-lsp-server",
    categories = {"LSP"},
    languages = {"Python"},
    aliases = {"py", "pylsp", "python-lsp-server"},
    bin = {"pylsp"},
    extras = {["extra"] = "all"},
    version = "1.15.0", -- snapshot pin
  },
  {
    id = "qmlls", display = "qmlls", detail = "QML Language Server is a tool shipped with Qt that helps you write code in your favorite (LSP-supporting) editor.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"QML"},
    bin = {"qmlls"},
    repo = "TheQtCompanyRnD/qmlls-workflow", asset = {
      linux = { match = "qmlls\\-ubuntu\\-nightly\\-.*\\.zip", archive = "zip" },
      mac = { match = "qmlls\\-macos\\-nightly\\-.*\\.zip", archive = "zip" },
      win = { match = "qmlls\\-windows\\-nightly\\-.*\\.zip", archive = "zip" },
    },
    version = "0.7", -- snapshot pin
  },
  {
    id = "quick-lint-js", display = "quick-lint-js", detail = "Over 130× faster than ESLint, quick-lint-js gives you instant feedback as you code. Find bugs in your JavaScript befo...", manager = "generic", pkg = "",
    categories = {"LSP", "Linter"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"quick-lint-js"},
    dl = {
      linux = { files = {["linux.tar.gz"] = "https://c.quick-lint-js.com/releases/3.2.0/manual/linux.tar.gz"}, bin = "quick-lint-js/bin/quick-lint-js" },
      mac = { files = {["macos.tar.gz"] = "https://c.quick-lint-js.com/releases/3.2.0/manual/macos.tar.gz"}, bin = "quick-lint-js/bin/quick-lint-js" },
      win = { files = {["windows.zip"] = "https://c.quick-lint-js.com/releases/3.2.0/manual/windows.zip"}, bin = "bin/quick-lint-js.exe" },
    },
    version = "3.2.0", -- snapshot pin
  },
  {
    id = "rassumfrassum", display = "rassumfrassum", detail = "Connect an LSP client to multiple LSP servers.", manager = "pypi", pkg = "rassumfrassum",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"rass"},
    version = "0.3.4", -- snapshot pin
  },
  {
    id = "rdbg", display = "rdbg", detail = "Debug Adapter Protocol implementation for Ruby, powered by rdbg.", manager = "gem", pkg = "debug",
    categories = {"DAP"},
    languages = {"Ruby"},
    bin = {"rdbg"},
    version = "1.11.1", -- snapshot pin
  },
  {
    id = "reason-language-server", display = "reason-language-server", detail = "A language server for reason, in reason.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Reason"},
    bin = {"reason-language-server"},
    repo = "jaredly/reason-language-server", asset = {
      mac = { match = "rls\\-macos\\.zip", archive = "zip" },
      win = { match = "rls\\-windows\\.zip", archive = "zip" },
    },
    version = "1.7.13", -- snapshot pin
  },
  {
    id = "reformat-gherkin", display = "reformat-gherkin", detail = "Reformat-gherkin automatically formats Gherkin files.", manager = "pypi", pkg = "reformat-gherkin",
    categories = {"Formatter"},
    languages = {"Cucumber"},
    bin = {"reformat-gherkin"},
    version = "3.0.1", -- snapshot pin
  },
  {
    id = "refurb", display = "refurb", detail = "A tool for refurbishing and modernizing Python codebases.", manager = "pypi", pkg = "refurb",
    categories = {"Linter"},
    languages = {"Python"},
    bin = {"refurb"},
    version = "2.3.1", -- snapshot pin
  },
  {
    id = "regal", display = "regal", detail = "A linter for Rego, with support for running as an LSP server.", manager = "github", pkg = "",
    categories = {"Linter", "LSP"},
    languages = {"Rego"},
    bin = {"regal"},
    repo = "styrainc/regal", asset = {
      mac = { match = "regal_Darwin_arm64", archive = "none" },
      win = { match = "regal_Windows_x86_64\\.exe", archive = "none" },
    },
    version = "v0.42.0", -- snapshot pin
  },
  {
    id = "regols", display = "regols", detail = "OPA Rego language server", manager = "golang", pkg = "github.com/kitagry/regols",
    categories = {"LSP"},
    languages = {"Rego"},
    bin = {"regols"},
    version = "v0.2.4", -- snapshot pin
  },
  {
    id = "remark-cli", display = "remark-cli", detail = "Command line interface to inspect and change markdown files with remark.", manager = "npm", pkg = "remark-cli",
    categories = {"Formatter"},
    languages = {"Markdown"},
    bin = {"remark"},
    version = "12.0.1", -- snapshot pin
  },
  {
    id = "remark-language-server", display = "remark-language-server", detail = "A language server to lint and format markdown files with remark.", manager = "npm", pkg = "remark-language-server",
    categories = {"LSP"},
    languages = {"Markdown"},
    bin = {"remark-language-server"},
    version = "3.0.0", -- snapshot pin
  },
  {
    id = "reorder-python-imports", display = "reorder-python-imports", detail = "Tool for automatically reordering python imports. Similar to isort but uses static analysis more.", manager = "pypi", pkg = "reorder-python-imports",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"reorder-python-imports"},
    version = "3.17.0", -- snapshot pin
  },
  {
    id = "rescript-language-server", display = "rescript-language-server", detail = "Language Server for ReScript.", manager = "npm", pkg = "%40rescript/language-server",
    categories = {"LSP"},
    languages = {"ReScript"},
    bin = {"rescript-language-server"},
    version = "1.74.0", -- snapshot pin
  },
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
  {
    id = "sorbet", display = "sorbet", detail = "Sorbet is a fast, powerful type checker designed for Ruby.", manager = "gem", pkg = "sorbet",
    categories = {"LSP"},
    languages = {"Ruby"},
    bin = {"srb"},
    version = "0.6.13480", -- snapshot pin
  },
  {
    id = "sourcery", display = "sourcery", detail = "Sourcery is a tool available in your IDE, GitHub, or as a CLI that suggests refactoring improvements to help make you...", manager = "pypi", pkg = "sourcery",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"sourcery"},
    version = "1.45.0", -- snapshot pin
  },
  {
    id = "sphinx-lint", display = "sphinx-lint", detail = "Linter for stylistic and formal issues in Sphinx documentation", manager = "pypi", pkg = "sphinx-lint",
    categories = {"Linter"},
    languages = {"reStructuredText", "Python"},
    bin = {"sphinx-lint"},
    version = "1.0.2", -- snapshot pin
  },
  {
    id = "spyglassmc-language-server", display = "spyglassmc-language-server", detail = "This is a language server wrapped around some other Spyglass packages.", manager = "npm", pkg = "%40spyglassmc/language-server",
    categories = {"LSP"},
    languages = {"MCFunction"},
    bin = {"spyglassmc-language-server"},
    version = "0.4.68", -- snapshot pin
  },
  {
    id = "sql", display = "SQL", detail = "sql-language-server (sqls)", manager = "npm", pkg = "sql-language-server",
    categories = {"LSP"},
    languages = {"SQL"},
    aliases = {"sqls"},
    bin = {"sql-language-server"},
    win_cmd = "npm install -g sql-language-server", win_remove_cmd = "npm uninstall -g sql-language-server",
  },
  {
    id = "sql-formatter", display = "sql-formatter", detail = "A whitespace formatter for different query languages.", manager = "npm", pkg = "sql-formatter",
    categories = {"Formatter"},
    languages = {"SQL"},
    bin = {"sql-formatter"},
    version = "15.8.2", -- snapshot pin
  },
  {
    id = "sqlfluff", display = "sqlfluff", detail = "SQLFluff is a dialect-flexible and configurable SQL linter.", manager = "pypi", pkg = "sqlfluff",
    categories = {"Linter"},
    languages = {"SQL"},
    bin = {"sqlfluff"},
    version = "4.3.0", -- snapshot pin
  },
  {
    id = "sqlfmt", display = "sqlfmt", detail = "sqlfmt formats your dbt SQL files so you don't have to. It is similar in nature to black, gofmt, and rustfmt (but for...", manager = "pypi", pkg = "shandy-sqlfmt",
    categories = {"Formatter"},
    languages = {"SQL"},
    bin = {"sqlfmt"},
    extras = {["extra"] = "jinjafmt"},
    version = "0.32.0", -- snapshot pin
  },
  {
    id = "sqlls", display = "sqlls", detail = "SQL Language Server.", manager = "npm", pkg = "sql-language-server",
    categories = {"LSP"},
    languages = {"SQL"},
    bin = {"sql-language-server"},
    version = "1.7.1", -- snapshot pin
  },
  {
    id = "sqls", display = "sqls", detail = "SQL language server written in Go.", manager = "golang", pkg = "github.com/sqls-server/sqls",
    categories = {"LSP"},
    languages = {"SQL"},
    bin = {"sqls"},
    version = "v0.2.48", -- snapshot pin
  },
  {
    id = "sqruff", display = "sqruff", detail = "A high-speed SQL linter written in Rust.", manager = "github", pkg = "",
    categories = {"Formatter", "Linter"},
    languages = {"SQL"},
    bin = {"sqruff"},
    repo = "quarylabs/sqruff", asset = {
      mac = { match = "sqruff\\-darwin\\-aarch64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "sqruff\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v0.40.0", -- snapshot pin
  },
  {
    id = "squawk", display = "squawk", detail = "Linter for Postgres migrations & SQL", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Postgres", "SQL"},
    bin = {"squawk"},
    repo = "sbdchd/squawk", asset = {
      linux = { match = "squawk\\-linux\\-musl\\-x64", archive = "none" },
      mac = { match = "squawk\\-darwin\\-arm64", archive = "none" },
      win = { match = "squawk\\-windows\\-x64\\.exe", archive = "none" },
    },
    version = "v2.64.0", -- snapshot pin
  },
  {
    id = "stan-language-server", display = "stan-language-server", detail = "Language server for the Stan probabilistic programming language.", manager = "npm", pkg = "stan-language-server-bin",
    categories = {"LSP"},
    languages = {"Stan"},
    bin = {"stan-language-server"},
    version = "0.4.10", -- snapshot pin
  },
  {
    id = "standardjs", display = "standardjs", detail = "JavaScript Style Guide, with linter & automatic code fixer.", manager = "npm", pkg = "standard",
    categories = {"Linter", "Formatter"},
    languages = {"JavaScript"},
    bin = {"standard"},
    version = "17.1.2", -- snapshot pin
  },
  {
    id = "standardrb", display = "standardrb", detail = "Ruby Style Guide, with linter and automatic code fixer.", manager = "gem", pkg = "standard",
    categories = {"Formatter", "Linter"},
    languages = {"Ruby"},
    bin = {"standardrb"},
    version = "1.56.0", -- snapshot pin
  },
  {
    id = "starlark-rust", display = "starlark-rust", detail = "A Rust implementation of the Starlark language", manager = "cargo", pkg = "starlark_bin",
    categories = {"LSP"},
    languages = {"Starlark"},
    bin = {"starlark"},
    version = "0.14.2", -- snapshot pin
  },
  {
    id = "starpls", display = "starpls", detail = "Starpls is an LSP implementation for Starlark, the configuration language used by Bazel and Buck2.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Starlark"},
    bin = {"starpls"},
    repo = "withered-magic/starpls", asset = {
      mac = { match = "starpls\\-darwin\\-arm64", archive = "none" },
      win = { match = "starpls\\-windows\\-amd64\\.exe", archive = "none" },
    },
    version = "v0.1.22", -- snapshot pin
  },
  {
    id = "staticcheck", display = "staticcheck", detail = "The advanced Go linter.", manager = "golang", pkg = "honnef.co/go/tools",
    categories = {"Linter"},
    languages = {"Go"},
    bin = {"staticcheck"},
    version = "v0.8.1#cmd/staticcheck", -- snapshot pin
  },
  {
    id = "statix", display = "statix", detail = "lints and suggestions for the nix programming language", manager = "cargo", pkg = "statix",
    categories = {"Linter"},
    languages = {"Nix"},
    bin = {"statix"},
    extras = {["repository_url"] = "https://github.com/oppiliappan/statix"},
    version = "v0.5.8", -- snapshot pin
  },
  {
    id = "steep", display = "steep", detail = "Static type checker for Ruby", manager = "gem", pkg = "steep",
    categories = {"LSP"},
    languages = {"Ruby"},
    bin = {"steep"},
    version = "2.0.0", -- snapshot pin
  },
  {
    id = "stimulus-language-server", display = "stimulus-language-server", detail = "Intelligent Stimulus tooling", manager = "npm", pkg = "stimulus-language-server",
    categories = {"LSP"},
    languages = {"Blade", "HTML", "PHP", "Ruby"},
    bin = {"stimulus-language-server"},
    version = "1.0.4", -- snapshot pin
  },
  {
    id = "stree", display = "stree", detail = "A fast Ruby parser and formatter.", manager = "gem", pkg = "syntax_tree",
    categories = {"LSP"},
    languages = {"Ruby"},
    bin = {"stree"},
    version = "6.3.0", -- snapshot pin
  },
  {
    id = "stylelint", display = "stylelint", detail = "A mighty CSS linter that helps you avoid errors and enforce conventions.", manager = "npm", pkg = "stylelint",
    categories = {"Linter"},
    languages = {"CSS", "Sass", "SCSS", "LESS"},
    bin = {"stylelint"},
    version = "17.14.1", -- snapshot pin
  },
  {
    id = "stylelint-language-server", display = "stylelint-language-server", detail = "A stylelint Language Server.", manager = "npm", pkg = "@stylelint/language-server",
    categories = {"LSP"},
    languages = {"Stylelint"},
    bin = {"stylelint-language-server"},
    version = "1.1.1", -- snapshot pin
  },
  {
    id = "stylelint-lsp", display = "stylelint-lsp", detail = "A stylelint Language Server.", manager = "npm", pkg = "stylelint-lsp",
    categories = {"LSP"},
    languages = {"Stylelint"},
    bin = {"stylelint-lsp"},
    version = "2.0.1", -- snapshot pin
  },
  {
    id = "stylua", display = "stylua", detail = "An opinionated Lua code formatter.", manager = "github", pkg = "",
    categories = {"Formatter", "LSP"},
    languages = {"Lua", "Luau"},
    bin = {"stylua"},
    repo = "johnnymorganz/stylua", asset = {
      linux = { match = "stylua\\-linux\\-x86_64\\.zip", archive = "zip" },
      mac = { match = "stylua\\-macos\\-aarch64\\.zip", archive = "zip" },
      win = { match = "stylua\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "v2.5.2", -- snapshot pin
  },
  {
    id = "superhtml", display = "superhtml", detail = "HTML Language Server & Templating Language Library", manager = "github", pkg = "",
    categories = {"LSP", "Formatter"},
    languages = {"HTML", "SuperHTML"},
    bin = {"superhtml"},
    repo = "kristoff-it/superhtml", asset = {
      mac = { match = "aarch64\\-macos\\.zip", archive = "zip" },
      win = { match = "x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v0.7.0", -- snapshot pin
  },
  {
    id = "svelte-language-server", display = "svelte-language-server", detail = "A language server (implementing the language server protocol) for Svelte.", manager = "npm", pkg = "svelte-language-server",
    categories = {"LSP"},
    languages = {"Svelte"},
    bin = {"svelteserver"},
    version = "0.18.4", -- snapshot pin
  },
  {
    id = "svlangserver", display = "svlangserver", detail = "A language server for systemverilog that has been tested to work with coc.nvim, VSCode, Sublime Text 4, emacs, and Ne...", manager = "npm", pkg = "%40imc-trading/svlangserver",
    categories = {"LSP"},
    languages = {"SystemVerilog"},
    bin = {"svlangserver"},
    version = "0.4.1", -- snapshot pin
  },
  {
    id = "swiftformat", display = "swiftformat", detail = "A command-line tool and Xcode Extension for formatting Swift code.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Swift"},
    bin = {"swiftformat"},
    repo = "nicklockwood/SwiftFormat", asset = {
      linux = { match = "swiftformat\\.artifactbundle\\.zip", archive = "zip" },
      mac = { match = "swiftformat\\.artifactbundle\\.zip", archive = "zip" },
    },
    version = "0.63.0", -- snapshot pin
  },
  {
    id = "swiftlint", display = "swiftlint", detail = "A tool to enforce Swift style and conventions.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Swift"},
    bin = {"swiftlint"},
    repo = "realm/SwiftLint", asset = {
      linux = { match = "SwiftLintBinary\\.artifactbundle\\.zip", archive = "zip" },
      mac = { match = "SwiftLintBinary\\.artifactbundle\\.zip", archive = "zip" },
    },
    version = "0.65.1", -- snapshot pin
  },
  {
    id = "symfony-lsp", display = "symfony-lsp", detail = "A language server for Symfony applications.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"PHP", "Twig", "YAML", "JavaScript", "TypeScript"},
    bin = {"symfony-lsp"},
    repo = "symfony/language-tools", asset = {
      mac = { match = "symfony\\-lsp\\-.*\\-macos\\-arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "symfony\\-lsp\\-.*\\-windows\\-x64\\.zip", archive = "zip" },
    },
    version = "v0.19.0", -- snapshot pin
  },
  {
    id = "systemd-lsp", display = "systemd-lsp", detail = "a language server implementation for systemd unit files made in rust", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"systemd"},
    bin = {"systemd-lsp"},
    repo = "JFryy/systemd-lsp", asset = {
      linux = { match = "systemd\\-lsp\\-x86_64\\-unknown\\-linux\\-gnu", archive = "none" },
      mac = { match = "systemd\\-lsp\\-x86_64\\-apple\\-darwin", archive = "none" },
      win = { match = "systemd\\-lsp\\-x86_64\\-pc\\-windows\\-msvc\\.exe", archive = "none" },
    },
    version = "v2026.08.03", -- snapshot pin
  },
  {
    id = "systemdlint", display = "systemdlint", detail = "Systemd Unitfile Linter", manager = "pypi", pkg = "systemdlint",
    categories = {"Linter"},
    languages = {"systemd"},
    bin = {"systemdlint"},
    version = "1.4.0", -- snapshot pin
  },
  {
    id = "tabby-agent", display = "tabby-agent", detail = "Tabby is a self-hosted AI coding assistant, offering an open-source and on-premises alternative to GitHub Copilot.", manager = "npm", pkg = "tabby-agent",
    categories = {"LSP"},
    bin = {"tabby-agent"},
    version = "1.8.0", -- snapshot pin
  },
  {
    id = "tailwindcss-language-server", display = "tailwindcss-language-server", detail = "Language Server Protocol implementation for Tailwind CSS.", manager = "npm", pkg = "%40tailwindcss/language-server",
    categories = {"LSP"},
    languages = {"CSS"},
    bin = {"tailwindcss-language-server"},
    version = "0.16.0", -- snapshot pin
  },
  {
    id = "taplo", display = "taplo", detail = "A versatile, feature-rich TOML toolkit.", manager = "github", pkg = "",
    categories = {"LSP", "Formatter"},
    languages = {"TOML"},
    bin = {"taplo"},
    repo = "tamasfe/taplo", asset = {
      mac = { match = "taplo\\-darwin\\-aarch64\\.gz", archive = "gz" },
      win = { match = "taplo\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "0.10.0", -- snapshot pin
  },
  {
    id = "tclint", display = "tclint", detail = "Modern dev tools for Tcl • includes a linter, formatter, and editor integration.", manager = "pypi", pkg = "tclint",
    categories = {"Linter", "Formatter", "LSP"},
    languages = {"Tcl"},
    bin = {"tclint", "tclfmt", "tclsp"},
    version = "0.9.0", -- snapshot pin
  },
  {
    id = "tectonic", display = "tectonic", detail = "Tectonic is a modernized, complete, self-contained TeX/LaTeX engine, powered by XeTeX and TeXLive.", manager = "github", pkg = "",
    categories = {"Compiler"},
    languages = {"LaTeX"},
    bin = {"tectonic"},
    repo = "tectonic-typesetting/tectonic", asset = {
      linux = { match = "tectonic\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "tectonic\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "tectonic\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "tectonic%400.15.0", -- snapshot pin
  },
  {
    id = "templ", display = "templ", detail = "templ is the official language server for the templ HTML templating language. It provides IDE features to any LSP-com...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Go", "HTML"},
    bin = {"templ"},
    repo = "a-h/templ", asset = {
      mac = { match = "templ_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "templ_Windows_x86_64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.3.1020", -- snapshot pin
  },
  {
    id = "termux-language-server", display = "termux-language-server", detail = "A language server for some specific bash scripts.", manager = "pypi", pkg = "termux-language-server",
    categories = {"LSP"},
    languages = {"Bash"},
    bin = {"termux-language-server"},
    version = "0.1.9", -- snapshot pin
  },
  {
    id = "terraform", display = "terraform", detail = "Terraform enables you to safely and predictably create, change, and improve infrastructure. It is a source-available ...", manager = "generic", pkg = "",
    categories = {"Formatter", "Linter", "Runtime"},
    languages = {"Terraform"},
    bin = {"terraform"},
    dl = {
      linux = { files = {["terraform.zip"] = "https://releases.hashicorp.com/terraform/1.16.1/terraform_1.16.1_linux_amd64.zip"}, bin = "terraform" },
      mac = { files = {["terraform.zip"] = "https://releases.hashicorp.com/terraform/1.16.1/terraform_1.16.1_darwin_amd64.zip"}, bin = "terraform" },
      win = { files = {["terraform.zip"] = "https://releases.hashicorp.com/terraform/1.16.1/terraform_1.16.1_windows_amd64.zip"}, bin = "terraform.exe" },
    },
    version = "v1.16.1", -- snapshot pin
  },
  {
    id = "terraform-ls", display = "terraform-ls", detail = "Terraform Language Server.", manager = "generic", pkg = "",
    categories = {"LSP"},
    languages = {"Terraform"},
    bin = {"terraform-ls"},
    dl = {
      linux = { files = {["terraform-ls.zip"] = "https://releases.hashicorp.com/terraform-ls/0.39.0/terraform-ls_0.39.0_linux_amd64.zip"}, bin = "terraform-ls" },
      mac = { files = {["terraform-ls.zip"] = "https://releases.hashicorp.com/terraform-ls/0.39.0/terraform-ls_0.39.0_darwin_amd64.zip"}, bin = "terraform-ls" },
      win = { files = {["terraform-ls.zip"] = "https://releases.hashicorp.com/terraform-ls/0.39.0/terraform-ls_0.39.0_windows_amd64.zip"}, bin = "terraform-ls.exe" },
    },
    version = "v0.39.0", -- snapshot pin
  },
  {
    id = "terragrunt-ls", display = "terragrunt-ls", detail = "Language server for Terragrunt configuration files.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"HCL"},
    bin = {"terragrunt-ls"},
    repo = "gruntwork-io/terragrunt-ls", asset = {
      linux = { match = "terragrunt\\-ls_linux_amd64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "terragrunt\\-ls_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "terragrunt\\-ls_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.0.6", -- snapshot pin
  },
  {
    id = "tex-fmt", display = "tex-fmt", detail = "An extremely fast LaTeX formatter written in Rust.", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"LaTeX"},
    bin = {"tex-fmt"},
    repo = "WGUNDERWOOD/tex-fmt", asset = {
      linux = { match = "tex\\-fmt\\-x86_64\\-linux\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "tex\\-fmt\\-aarch64\\-macos\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "tex\\-fmt\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v0.5.7", -- snapshot pin
  },
  {
    id = "texlab", display = "texlab", detail = "An implementation of the Language Server Protocol for LaTeX.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"LaTeX"},
    bin = {"texlab"},
    repo = "latex-lsp/texlab", asset = {
      linux = { match = "texlab\\-x86_64\\-linux\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "texlab\\-aarch64\\-macos\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "texlab\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v5.26.0", -- snapshot pin
  },
  {
    id = "textlint", display = "textlint", detail = "The pluggable natural language linter for text and markdown.", manager = "npm", pkg = "textlint",
    categories = {"Linter"},
    languages = {"Text", "Markdown"},
    bin = {"textlint"},
    version = "15.8.0", -- snapshot pin
  },
  {
    id = "textlsp", display = "textlsp", detail = "Language server for text spell and grammar check with various tools.", manager = "pypi", pkg = "textLSP",
    categories = {"LSP"},
    languages = {"Text", "LaTeX", "Org"},
    bin = {"textlsp"},
    version = "0.4.0", -- snapshot pin
  },
  {
    id = "tflint", display = "tflint", detail = "A Pluggable Terraform Linter.", manager = "github", pkg = "",
    categories = {"LSP", "Linter"},
    languages = {"Terraform"},
    bin = {"tflint"},
    repo = "terraform-linters/tflint", asset = {
      mac = { match = "tflint_darwin_arm64\\.zip", archive = "zip" },
      win = { match = "tflint_windows_amd64\\.zip", archive = "zip" },
    },
    version = "v0.64.0", -- snapshot pin
  },
  {
    id = "tfsec", display = "tfsec", detail = "Security scanner for your Terraform code", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Terraform"},
    bin = {"tfsec"},
    repo = "aquasecurity/tfsec", asset = {
      mac = { match = "tfsec\\-darwin\\-arm64", archive = "none" },
      win = { match = "tfsec\\-windows\\-amd64\\.exe", archive = "none" },
    },
    version = "v1.28.14", -- snapshot pin
  },
  {
    id = "thriftls", display = "thriftls", detail = "thriftls is a Thrift language server. It provides IDE features to any LSP-compatible editor.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Thrift"},
    bin = {"thriftls"},
    repo = "joyme123/thrift-ls", asset = {
      mac = { match = "thriftls\\-darwin\\-arm64", archive = "none" },
      win = { match = "thriftls\\-windows\\-amd64\\.exe", archive = "none" },
    },
    version = "v0.2.12", -- snapshot pin
  },
  {
    id = "tilt", display = "tilt", detail = "Define your dev environment as code. For microservice apps on Kubernetes.", manager = "github", pkg = "",
    categories = {"Runtime", "LSP"},
    languages = {"Starlark"},
    bin = {"tilt"},
    repo = "tilt-dev/tilt", asset = {
      linux = { match = "tilt\\..*\\.linux\\-alpine\\.x86_64\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "tilt\\..*\\.mac\\.arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "tilt\\..*\\.windows\\.x86_64\\.zip", archive = "zip" },
    },
    version = "v0.37.7", -- snapshot pin
  },
  {
    id = "tinymist", display = "tinymist", detail = "Tinymist [ˈtaɪni mɪst] is an integrated language service for Typst [taɪpst]. You can also call it \"微霭\" [wēi ǎi] in Ch...", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Typst"},
    bin = {"tinymist"},
    repo = "Myriad-Dreamin/tinymist", asset = {
      linux = { match = "tinymist\\-linux\\-x64", archive = "none" },
      mac = { match = "tinymist\\-darwin\\-arm64", archive = "none" },
      win = { match = "tinymist\\-win32\\-x64\\.exe", archive = "none" },
    },
    version = "v0.15.4", -- snapshot pin
  },
  {
    id = "tofu-ls", display = "tofu-ls", detail = "OpenTofu Language Server", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"OpenTofu"},
    bin = {"tofu-ls"},
    repo = "opentofu/tofu-ls", asset = {
      mac = { match = "tofu\\-ls_Darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "tofu\\-ls_Windows_x86_64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.5.3", -- snapshot pin
  },
  {
    id = "tombi", display = "tombi", detail = "TOML Formatter / Linter / Language Server", manager = "github", pkg = "",
    categories = {"Formatter", "Linter", "LSP"},
    languages = {"TOML"},
    bin = {"tombi"},
    repo = "tombi-toml/tombi", asset = {
      mac = { match = "tombi\\-cli\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "tombi\\-cli\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v1.5.1", -- snapshot pin
  },
  {
    id = "tree-sitter-cli", display = "tree-sitter-cli", detail = "The Tree-sitter CLI allows you to develop, test, and use Tree-sitter grammars from the command line. It works on MacO...", manager = "github", pkg = "",
    categories = {"Compiler"},
    bin = {"tree-sitter"},
    repo = "tree-sitter/tree-sitter", asset = {
      mac = { match = "tree\\-sitter\\-macos\\-arm64\\.gz", archive = "gz" },
      win = { match = "tree\\-sitter\\-cli\\-windows\\-x64\\.zip", archive = "zip" },
    },
    version = "v0.27.0", -- snapshot pin
  },
  {
    id = "trivy", display = "trivy", detail = "Find vulnerabilities, misconfigurations, secrets, SBOM in containers, Kubernetes, code repositories, clouds and more.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"C", "C#", "C++", "Dart", "Docker", "Elixir", "Go", "Helm", "Java", "JavaScript", "PHP", "Python", "Ruby", "Rust", "Terraform", "TypeScript"},
    bin = {"trivy"},
    repo = "aquasecurity/trivy", asset = {
      mac = { match = "trivy_.*_macOS\\-ARM64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "trivy_.*_Windows\\-64bit\\.zip", archive = "zip" },
    },
    version = "v0.74.0", -- snapshot pin
  },
  {
    id = "trufflehog", display = "trufflehog", detail = "Find and verify credentials.", manager = "github", pkg = "",
    categories = {"Linter"},
    bin = {"trufflehog"},
    repo = "trufflesecurity/trufflehog", asset = {
      mac = { match = "trufflehog_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "trufflehog_.*_windows_amd64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v3.97.4", -- snapshot pin
  },
  {
    id = "ts-standard", display = "ts-standard", detail = "Typescript style guide, linter, and formatter using StandardJS.", manager = "npm", pkg = "ts-standard",
    categories = {"Linter", "Formatter"},
    languages = {"TypeScript"},
    bin = {"ts-standard"},
    version = "12.0.2", -- snapshot pin
  },
  {
    id = "ts_query_ls", display = "ts_query_ls", detail = "An LSP implementation for Tree-sitter's query files", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Query"},
    bin = {"ts_query_ls"},
    repo = "ribru17/ts_query_ls", asset = {
      linux = { match = "ts_query_ls\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "ts_query_ls\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "ts_query_ls\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v3.16.0", -- snapshot pin
  },
  {
    id = "tsc", display = "tsc", detail = "TypeScript is a language for application-scale JavaScript. TypeScript adds optional types to JavaScript that support ...", manager = "npm", pkg = "typescript",
    categories = {"Compiler", "LSP"},
    languages = {"TypeScript", "JavaScript"},
    bin = {"tsc"},
    version = "7.0.2", -- snapshot pin
  },
  {
    id = "tsgo", display = "tsgo", detail = "Native TypeScript compiler port. Language Server Protocol support with partial features including errors, hover, go t...", manager = "npm", pkg = "@typescript/native-preview",
    categories = {"LSP"},
    languages = {"TypeScript", "JavaScript"},
    bin = {"tsgo"},
    version = "7.0.0-dev.20260707.2", -- snapshot pin
  },
  {
    id = "tsp-server", display = "tsp-server", detail = "The language server for TypeSpec, a language for defining cloud service APIs and shapes.", manager = "npm", pkg = "%40typespec/compiler",
    categories = {"LSP"},
    languages = {"Typespec"},
    bin = {"tsp-server"},
    version = "1.15.0", -- snapshot pin
  },
  {
    id = "turtle-language-server", display = "turtle-language-server", detail = "A language server (by Stardog) for Turtle", manager = "npm", pkg = "turtle-language-server",
    categories = {"LSP"},
    languages = {"Turtle"},
    bin = {"turtle-language-server"},
    version = "3.5.0", -- snapshot pin
  },
  {
    id = "twig-cs-fixer", display = "twig-cs-fixer", detail = "A tool to automatically fix Twig Coding Standards issues", manager = "composer", pkg = "vincentlanglet/twig-cs-fixer",
    categories = {"Formatter"},
    languages = {"Twig"},
    bin = {"twig-cs-fixer"},
    version = "4.1.0", -- snapshot pin
  },
  {
    id = "twigcs", display = "twigcs", detail = "The missing checkstyle for twig! Twigcs aims to be what phpcs is to php. It checks your codebase for violations on co...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Twig"},
    bin = {"twigcs"},
    repo = "friendsoftwig/twigcs", asset = {
      linux = { match = "twigcs\\.phar", archive = "none" },
      mac = { match = "twigcs\\.phar", archive = "none" },
      win = { match = "twigcs\\.phar", archive = "none" },
    },
    version = "6.5.0", -- snapshot pin
    runs = {
      ["twigcs"] = { kind = "php", hint = "twigcs.phar" },
    },
  },
  {
    id = "twiggy-language-server", display = "twiggy-language-server", detail = "Twig Language Server.", manager = "npm", pkg = "twiggy-language-server",
    categories = {"LSP", "Linter"},
    languages = {"Twig", "HTML"},
    bin = {"twiggy-language-server"},
    version = "26.4.1", -- snapshot pin
    runs = {
      ["twiggy-language-server"] = { kind = "node", hint = "node_modules/twiggy-language-server/dist/server.js" },
    },
  },
  {
    id = "ty", display = "ty", detail = "An extremely fast Python type checker and language server, written in Rust.", manager = "pypi", pkg = "ty",
    categories = {"Linter", "LSP"},
    languages = {"Python"},
    bin = {"ty"},
    version = "0.0.78", -- snapshot pin
  },
  {
    id = "typescript", display = "typescript-language-server", detail = "TypeScript & JavaScript Language Server.", manager = "npm", pkg = "typescript-language-server",
    categories = {"LSP"},
    languages = {"TypeScript", "JavaScript"},
    aliases = {"javascript", "js", "jsx", "ts", "tsx", "mts", "cts", "ts-server", "typescript-language-server"},
    bin = {"typescript-language-server"},
    version = "6.0.0", -- snapshot pin
  },
  {
    id = "typos", display = "typos", detail = "Source code spell checker", manager = "github", pkg = "",
    categories = {"Linter"},
    bin = {"typos"},
    repo = "crate-ci/typos", asset = {
      mac = { match = "typos\\-.*\\-x86_64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "typos\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v1.50.1", -- snapshot pin
  },
  {
    id = "typos-lsp", display = "typos-lsp", detail = "Source code spell checker", manager = "github", pkg = "",
    categories = {"LSP"},
    bin = {"typos-lsp"},
    repo = "tekumara/typos-vscode", asset = {
      linux = { match = "typos\\-lsp\\-.*\\-x86_64\\-unknown\\-linux\\-gnu\\.tar\\.gz", archive = "tar.gz" },
      mac = { match = "typos\\-lsp\\-.*\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "typos\\-lsp\\-.*\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "v0.1.56", -- snapshot pin
  },
  {
    id = "typstyle", display = "typstyle", detail = "Beautiful and reliable typst code formatter", manager = "github", pkg = "",
    categories = {"Formatter"},
    languages = {"Typst"},
    bin = {"typstyle"},
    repo = "Enter-tainer/typstyle", asset = {
      mac = { match = "typstyle\\-aarch64\\-apple\\-darwin", archive = "none" },
      win = { match = "typstyle\\-aarch64\\-pc\\-windows\\-msvc\\.exe", archive = "none" },
    },
    version = "v0.15.1", -- snapshot pin
  },
  {
    id = "unocss-language-server", display = "unocss-language-server", detail = "Language Server Protocol implementation for UnoCSS.", manager = "npm", pkg = "unocss-language-server",
    categories = {"LSP"},
    languages = {"CSS"},
    bin = {"unocss-language-server"},
    version = "0.1.9", -- snapshot pin
  },
  {
    id = "usort", display = "usort", detail = "Safe, minimal import sorting for Python projects.", manager = "pypi", pkg = "usort",
    categories = {"Formatter"},
    languages = {"Python"},
    bin = {"usort"},
    version = "1.1.3", -- snapshot pin
  },
  {
    id = "uv", display = "uv", detail = "An extremely fast Python package and project manager, written in Rust", manager = "github", pkg = "",
    languages = {"Python"},
    bin = {"uv", "uvw", "uvx"},
    repo = "astral-sh/uv", asset = {
      mac = { match = "uv\\-aarch64\\-apple\\-darwin\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "uv\\-x86_64\\-pc\\-windows\\-msvc\\.zip", archive = "zip" },
    },
    version = "0.12.9", -- snapshot pin
  },
  {
    id = "v-analyzer", display = "v-analyzer", detail = "The @vlang language server, for all your editing needs like go-to-definition, code completion, type hints, and more.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"V"},
    bin = {"v-analyzer"},
    repo = "vlang/v-analyzer", asset = {
      linux = { match = "v\\-analyzer\\-linux\\-x86_64\\.zip", archive = "zip" },
      mac = { match = "v\\-analyzer\\-darwin\\-arm64\\.zip", archive = "zip" },
      win = { match = "v\\-analyzer\\-windows\\-x86_64\\.zip", archive = "zip" },
    },
    version = "0.0.6", -- snapshot pin
  },
  {
    id = "vacuum", display = "vacuum", detail = "vacuum is the worlds fastest OpenAPI 3, OpenAPI 2 / Swagger linter and quality analysis tool. Built in go, it tears t...", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"OpenAPI"},
    bin = {"vacuum"},
    repo = "daveshanley/vacuum", asset = {
      mac = { match = "vacuum_.*_darwin_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "vacuum_.*_windows_x86_64\\.tar\\.gz", archive = "tar.gz" },
    },
    version = "v0.30.3", -- snapshot pin
  },
  {
    id = "vale", display = "vale", detail = "A syntax-aware linter for prose built with speed and extensibility in mind.", manager = "github", pkg = "",
    categories = {"Linter"},
    languages = {"Text", "Markdown", "LaTeX"},
    bin = {"vale"},
    repo = "errata-ai/vale", asset = {
      mac = { match = "vale_.*_macOS_arm64\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "vale_.*_Windows_64\\-bit\\.zip", archive = "zip" },
    },
    version = "v3.20.0", -- snapshot pin
  },
  {
    id = "vale-ls", display = "vale-ls", detail = "An implementation of the Language Server Protocol (LSP) for the Vale command-line tool.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Text", "Markdown"},
    bin = {"vale-ls"},
    repo = "errata-ai/vale-ls", asset = {
      linux = { match = "vale\\-ls\\-x86_64\\-unknown\\-linux\\-gnu\\.zip", archive = "zip" },
      mac = { match = "vale\\-ls\\-aarch64\\-apple\\-darwin\\.zip", archive = "zip" },
      win = { match = "vale\\-ls\\-x86_64\\-pc\\-windows\\-gnu\\.zip", archive = "zip" },
    },
    version = "v0.5.1", -- snapshot pin
  },
  {
    id = "vectorcode", display = "vectorcode", detail = "A code repository indexing tool to supercharge your LLM experience.", manager = "pypi", pkg = "VectorCode",
    categories = {"LSP"},
    languages = {"Python"},
    bin = {"vectorcode", "vectorcode-server", "vectorcode-mcp-server"},
    extras = {["extra"] = "lsp,mcp"},
    version = "0.7.20", -- snapshot pin
  },
  {
    id = "verible", display = "verible", detail = "Verible is a suite of SystemVerilog developer tools, including a parser, style-linter, and formatter.", manager = "github", pkg = "",
    categories = {"LSP", "Linter", "Formatter"},
    languages = {"SystemVerilog"},
    bin = {"verible-verilog-diff", "verible-verilog-format", "verible-verilog-kythe-extractor", "verible-verilog-lint", "verible-verilog-ls", "verible-verilog-obfuscate", "verible-verilog-preprocessor", "verible-verilog-project", "verible-verilog-syntax"},
    repo = "chipsalliance/verible", asset = {
      mac = { match = "verible\\-.*\\-macOS\\.tar\\.gz", archive = "tar.gz" },
      win = { match = "verible\\-.*\\-win64\\.zip", archive = "zip" },
    },
    version = "v0.0-4163-g6cce8f19", -- snapshot pin
  },
  {
    id = "veryl-ls", display = "veryl-ls", detail = "Veryl language server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Veryl"},
    bin = {"veryl-ls"},
    repo = "veryl-lang/veryl", asset = {
      mac = { match = "veryl\\-aarch64\\-mac\\.zip", archive = "zip" },
      win = { match = "veryl\\-x86_64\\-windows\\.zip", archive = "zip" },
    },
    version = "v0.21.0", -- snapshot pin
  },
  {
    id = "vespa-language-server", display = "vespa-language-server", detail = "Language support for the Vespa Schema language.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"sd", "yql"},
    bin = {"vespa-language-server"},
    repo = "vespa-engine/vespa", asset = {
      linux = { match = "vespa\\-language\\-server_.*\\.jar", archive = "none" },
      mac = { match = "vespa\\-language\\-server_.*\\.jar", archive = "none" },
      win = { match = "vespa\\-language\\-server_.*\\.jar", archive = "none" },
    },
    version = "lsp-v2.5.0", -- snapshot pin
    runs = {
      ["vespa-language-server"] = { kind = "jar", hint = "vespa-language-server_{{ version | strip_prefix \"lsp-v\" }}.jar" },
    },
  },
  {
    id = "vetur-vls", display = "vetur-vls", detail = "VLS (Vue Language Server) is a language server implementation compatible with Language Server Protocol.", manager = "npm", pkg = "vls",
    categories = {"LSP"},
    languages = {"Vue"},
    bin = {"vls"},
    version = "0.8.5", -- snapshot pin
  },
  {
    id = "vhdl-style-guide", display = "vhdl-style-guide", detail = "Style guide enforcement for VHDL", manager = "pypi", pkg = "vsg",
    categories = {"Formatter"},
    languages = {"VHDL"},
    bin = {"vsg"},
    version = "3.35.0", -- snapshot pin
  },
  {
    id = "vim-language-server", display = "vim-language-server", detail = "VimScript language server.", manager = "npm", pkg = "vim-language-server",
    categories = {"LSP"},
    languages = {"VimScript"},
    bin = {"vim-language-server"},
    version = "2.3.1", -- snapshot pin
  },
  {
    id = "vint", display = "vint", detail = "Fast and Highly Extensible Vim script Language Lint implemented in Python.", manager = "pypi", pkg = "vim-vint",
    categories = {"Linter"},
    languages = {"VimScript"},
    bin = {"vint"},
    version = "0.3.21", -- snapshot pin
  },
  {
    id = "visualforce-language-server", display = "visualforce-language-server", detail = "Visualforce language server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"Visualforce"},
    bin = {"visualforce-language-server"},
    repo = "forcedotcom/salesforcedx-vscode", asset = {
      linux = { match = "salesforcedx\\-vscode\\-visualforce\\-.*\\.vsix", archive = "none" },
      mac = { match = "salesforcedx\\-vscode\\-visualforce\\-.*\\.vsix", archive = "none" },
      win = { match = "salesforcedx\\-vscode\\-visualforce\\-.*\\.vsix", archive = "none" },
    },
    version = "v67.17.2", -- snapshot pin
    runs = {
      ["visualforce-language-server"] = { kind = "node", hint = "extension/dist/visualforceServer.js" },
    },
  },
  {
    id = "vls", display = "vls", detail = "V language server.", manager = "github", pkg = "",
    categories = {"LSP"},
    languages = {"V"},
    bin = {"vls"},
    repo = "vlang/vls", asset = {
      mac = { match = "vls_macos_x64", archive = "none" },
      win = { match = "vls_windows_x64\\.exe", archive = "none" },
    },
    version = "latest", -- snapshot pin
  },
  {
    id = "vscode-home-assistant", display = "vscode-home-assistant", detail = "Language Server Protocol implementation for Home Assistant.", manager = "openvsx", pkg = "",
    categories = {"LSP"},
    languages = {"YAML"},
    bin = {"vscode-home-assistant"},
    openvsx = { ns = "keesschollaart", ext = "vscode-home-assistant", version = "2.2.0", file = "keesschollaart.vscode-home-assistant-{{version}}.vsix" },
    version = "2.2.0", -- snapshot pin
    runs = {
      ["vscode-home-assistant"] = { kind = "node", hint = "extension/out/server/server.js" },
    },
  },
  {
    id = "vscode-java-dependency", display = "vscode-java-dependency", detail = "Visual Studio Code extension for managing Java projects, dependencies, and exporting JAR files", manager = "openvsx", pkg = "",
    categories = {"Runtime"},
    languages = {"Java"},
    bin = {},
    openvsx = { ns = "vscjava", ext = "vscode-java-dependency", version = "0.27.5", file = "vscjava.vscode-java-dependency-{{version}}.vsix" },
    version = "0.27.5", -- snapshot pin
  },
  {
    id = "vscode-solidity-server", display = "vscode-solidity-server", detail = "Solidity language server.", manager = "npm", pkg = "vscode-solidity-server",
    categories = {"LSP"},
    languages = {"Solidity"},
    bin = {"vscode-solidity-server"},
    version = "0.0.187", -- snapshot pin
  },
  {
    id = "vscode-spring-boot-tools", display = "vscode-spring-boot-tools", detail = "VS Code Language Server for Spring Boot VSCode extension and Language Server providing support for working with Sprin...", manager = "openvsx", pkg = "",
    categories = {"LSP"},
    languages = {"Java"},
    bin = {},
    openvsx = { ns = "VMware", ext = "vscode-spring-boot", version = "2.2.0", file = "VMware.vscode-spring-boot-{{version}}.vsix" },
    version = "2.2.0", -- snapshot pin
  },
  {
    id = "vsg", display = "vsg", detail = "VHDL Style Guide (VSG), Coding style enforcement for VHDL.", manager = "pypi", pkg = "vsg",
    categories = {"Formatter", "Linter"},
    languages = {"VHDL"},
    bin = {"vsg"},
    version = "3.35.0", -- snapshot pin
  },
  {
    id = "vtsls", display = "vtsls", detail = "LSP wrapper around the TypeScript extension bundled with VSCode.", manager = "npm", pkg = "%40vtsls/language-server",
    categories = {"LSP"},
    languages = {"JavaScript", "TypeScript"},
    bin = {"vtsls"},
    version = "0.3.0", -- snapshot pin
  },
  {
    id = "vue", display = "vue-language-server", detail = "⚡ Explore high-performance tooling for Vue.", manager = "npm", pkg = "%40vue/language-server",
    categories = {"LSP"},
    languages = {"Vue"},
    aliases = {"vue-language-server", "vue-language-server"},
    bin = {"vue-language-server"},
    version = "3.3.11", -- snapshot pin
  },
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

-- Resolves a user-supplied id or alias to an entry.
function M.resolve(name)
  if not name then
    return nil
  end
  local n = tostring(name):lower():gsub('%s', '')
  for _, e in ipairs(M.entries) do
    if e.id == n then
      return e
    end
    for _, a in ipairs(e.aliases or {}) do
      if a == n then
        return e
      end
    end
  end
  return nil
end

return M
