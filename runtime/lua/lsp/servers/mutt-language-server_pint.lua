-- LSP / language-tooling package catalog shard: mutt-language-server .. pint.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
