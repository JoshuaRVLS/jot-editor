-- LSP / language-tooling package catalog shard: erg .. gotests.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
