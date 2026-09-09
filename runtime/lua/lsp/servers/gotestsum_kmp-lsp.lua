-- LSP / language-tooling package catalog shard: gotestsum .. kmp-lsp.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
