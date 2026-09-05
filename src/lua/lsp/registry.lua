-- LSP package registry: pure data, no logic. Adding a server is adding one
-- row here. The `id` doubles as the language id the native LSP client uses
-- (see Editor::command_for_language in lsp.cpp), and the `bin` names must
-- match the binaries that client spawns.
--
-- Managers (src/lua/lsp/managers/*.lua) turn a row into install shell
-- steps: npm/pypi/golang install into an isolated package dir under the
-- install root and symlink the binaries into <root>/bin, exactly like
-- mason.nvim's per-manager installers. `win_cmd` is the fallback for
-- Windows terminals (isolated POSIX scripts are not available there yet).

local M = {}

M.entries = {
  {
    id = "python",
    display = "Python",
    detail = "python-lsp-server (pylsp)",
    aliases = { "py", "pylsp" },
    manager = "pypi",
    pkg = "python-lsp-server",
    bin = { "pylsp" },
    win_cmd = "python -m pip install --user -U python-lsp-server",
    win_remove_cmd = "python -m pip uninstall -y python-lsp-server",
  },
  {
    id = "pyright",
    display = "Python (Pyright)",
    detail = "pyright (pyright-langserver)",
    manager = "pypi",
    pkg = "pyright",
    bin = { "pyright-langserver", "pyright" },
    win_cmd = "python -m pip install --user -U pyright",
    win_remove_cmd = "python -m pip uninstall -y pyright",
  },
  {
    id = "typescript",
    display = "TypeScript",
    detail = "typescript-language-server",
    aliases = { "javascript", "js", "jsx", "ts", "tsx", "mts", "cts", "ts-server",
      "typescript-language-server" },
    manager = "npm",
    pkg = "typescript-language-server",
    bin = { "typescript-language-server" },
    win_cmd = "npm install -g typescript-language-server",
    win_remove_cmd = "npm uninstall -g typescript-language-server",
  },
  {
    id = "bash",
    display = "Bash",
    detail = "bash-language-server",
    aliases = { "sh", "shell", "bashls", "bash-language-server" },
    manager = "npm",
    pkg = "bash-language-server",
    bin = { "bash-language-server" },
    win_cmd = "npm install -g bash-language-server",
    win_remove_cmd = "npm uninstall -g bash-language-server",
  },
  {
    id = "html",
    display = "HTML",
    detail = "vscode-html-language-server",
    aliases = { "htm", "vscode-html-language-server", "html-language-server" },
    manager = "npm",
    pkg = "vscode-langservers-extracted",
    bin = { "vscode-html-language-server" },
    win_cmd = "npm install -g vscode-langservers-extracted",
    win_remove_cmd = "npm uninstall -g vscode-langservers-extracted",
  },
  {
    id = "json",
    display = "JSON",
    detail = "vscode-json-language-server",
    manager = "npm",
    pkg = "vscode-langservers-extracted",
    bin = { "vscode-json-language-server" },
    win_cmd = "npm install -g vscode-langservers-extracted",
    win_remove_cmd = "npm uninstall -g vscode-langservers-extracted",
  },
  {
    id = "css",
    display = "CSS",
    detail = "vscode-css-language-server",
    manager = "npm",
    pkg = "vscode-langservers-extracted",
    bin = { "vscode-css-language-server" },
    win_cmd = "npm install -g vscode-langservers-extracted",
    win_remove_cmd = "npm uninstall -g vscode-langservers-extracted",
  },
  {
    id = "yaml",
    display = "YAML",
    detail = "yaml-language-server",
    manager = "npm",
    pkg = "yaml-language-server",
    bin = { "yaml-language-server" },
    win_cmd = "npm install -g yaml-language-server",
    win_remove_cmd = "npm uninstall -g yaml-language-server",
  },
  {
    id = "dockerfile",
    display = "Dockerfile",
    detail = "dockerfile-language-server-nodejs",
    manager = "npm",
    pkg = "dockerfile-language-server-nodejs",
    bin = { "docker-langserver" },
    win_cmd = "npm install -g dockerfile-language-server-nodejs",
    win_remove_cmd = "npm uninstall -g dockerfile-language-server-nodejs",
  },
  {
    id = "vue",
    display = "Vue",
    detail = "@vue/language-server",
    manager = "npm",
    pkg = "@vue/language-server",
    bin = { "vue-language-server" },
    win_cmd = "npm install -g @vue/language-server",
    win_remove_cmd = "npm uninstall -g @vue/language-server",
  },
  {
    id = "markdown",
    display = "Markdown",
    detail = "markdown-language-features",
    manager = "npm",
    pkg = "markdown-language-features",
    bin = { "markdown-language-server" },
    win_cmd = "npm install -g markdown-language-features",
    win_remove_cmd = "npm uninstall -g markdown-language-features",
  },
  {
    id = "sql",
    display = "SQL",
    detail = "sql-language-server",
    manager = "npm",
    pkg = "sql-language-server",
    bin = { "sql-language-server" },
    win_cmd = "npm install -g sql-language-server",
    win_remove_cmd = "npm uninstall -g sql-language-server",
  },
  {
    id = "php",
    display = "PHP",
    detail = "intelephense",
    manager = "npm",
    pkg = "intelephense",
    bin = { "intelephense" },
    win_cmd = "npm install -g intelephense",
    win_remove_cmd = "npm uninstall -g intelephense",
  },
  {
    id = "lua",
    display = "Lua",
    detail = "lua-language-server",
    manager = "github",
    aliases = { "lua_ls", "lua-language-server", "luals" },
    repo = "LuaLS/lua-language-server",
    -- The release asset embeds the version tag: <name>-<tag>-<platform>.tgz
    asset = {
      linux = { match = "lua-language-server-{tag}-linux-x64.tar.gz", archive = "tar.gz", bin = "bin/lua-language-server" },
      mac = { match = "lua-language-server-{tag}-darwin-arm64.tar.gz", archive = "tar.gz", bin = "bin/lua-language-server" },
      win = { match = "lua-language-server-{tag}-win32-x64.zip", archive = "zip", bin = "bin/lua-language-server.exe" },
    },
    bin = { "lua-language-server" },
  },
  {
    id = "cpp",
    display = "C / C++",
    detail = "clangd",
    manager = "github",
    aliases = { "c", "c++", "clangd" },
    repo = "clangd/clangd",
    asset = {
      linux = { match = "clangd-linux-x86_64.zip", archive = "zip", bin = "bin/clangd" },
      mac = { match = "clangd-mac-arm64.zip", archive = "zip", bin = "bin/clangd" },
      win = { match = "clangd-win32-x64.zip", archive = "zip", bin = "bin/clangd.exe" },
    },
    bin = { "clangd" },
  },
  {
    id = "rust",
    display = "Rust",
    detail = "rust-analyzer",
    manager = "github",
    aliases = { "rs", "rust-analyzer" },
    repo = "rust-lang/rust-analyzer",
    asset = {
      linux = { match = "rust-analyzer-x86_64-unknown-linux-gnu.gz", archive = "gz", bin = "rust-analyzer" },
      mac = { match = "rust-analyzer-aarch64-apple-darwin.gz", archive = "gz", bin = "rust-analyzer" },
      win = { match = "rust-analyzer-x86_64-pc-windows-msvc.zip", archive = "zip", bin = "rust-analyzer.exe" },
    },
    bin = { "rust-analyzer" },
  },
  {
    id = "go",
    display = "Go",
    detail = "gopls (go install)",
    aliases = { "golang", "gopls" },
    manager = "golang",
    pkg = "golang.org/x/tools/gopls",
    bin = { "gopls" },
    win_cmd = "go install golang.org/x/tools/gopls@latest",
  },
}

-- Normalizes a user-supplied name (id or alias) to an entry, or nil.
function M.resolve(name)
  if not name then
    return nil
  end
  local n = tostring(name):lower():gsub("%s", "")
  for _, entry in ipairs(M.entries) do
    if entry.id == n then
      return entry
    end
    for _, alias in ipairs(entry.aliases or {}) do
      if alias == n then
        return entry
      end
    end
  end
  return nil
end

return M