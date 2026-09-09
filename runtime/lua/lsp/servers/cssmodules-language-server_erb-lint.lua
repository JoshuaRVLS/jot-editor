-- LSP / language-tooling package catalog shard: cssmodules-language-server .. erb-lint.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
