-- LSP / language-tooling package catalog shard: pkl-lsp .. rescript-language-server.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
