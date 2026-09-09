-- LSP / language-tooling package catalog shard: sorbet .. textlsp.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
