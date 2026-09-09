-- LSP / language-tooling package catalog shard: tflint .. vue.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
