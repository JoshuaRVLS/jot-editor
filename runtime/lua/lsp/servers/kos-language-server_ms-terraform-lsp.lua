-- LSP / language-tooling package catalog shard: kos-language-server .. ms-terraform-lsp.
-- Generated from the mason registry. Regenerate: python3 tools/mason_import.py
-- <mason-registry-checkout>
return {
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
}
