-- LSP / language-tooling package catalog shard: actionlint .. bslint.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
