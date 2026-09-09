-- LSP / language-tooling package catalog shard: buf .. csskit.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
