// Internal helpers shared by the LSP integration modules
// (src/jot/integrations/lsp/*.cpp). Kept inline so every translation unit
// that needs them gets them without extra TUs; theme-specific helpers stay
// in the anonymous namespace of the module that owns them.
#pragma once

#include "jot/integrations/lsp_attach_data.h"
#include "lsp/client.h"
#include "lsp/install.h"
#include "tools/string_util.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class LuaAPI;

namespace lsp_internal
{
  namespace fs = std::filesystem;
  inline std::string to_lower_copy(std::string s)
  {
    return string_util::lower_copy(std::move(s));
  }

  inline bool ends_with(const std::string &s, const std::string &suffix)
  {
    return string_util::ends_with(s, suffix);
  }

  inline bool lower_ends_with_dot_ext(const std::string &lower, const std::string &ext)
  {
    if (ext.empty() || lower.size() <= ext.size())
      return false;
    const size_t pos = lower.size() - ext.size();
    return lower.compare(pos, ext.size(), ext) == 0 && lower[pos - 1] == '.';
  }

  inline long long now_ms()
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  inline bool same_path(const std::string &a, const std::string &b)
  {
    if (a == b)
    {
      return true;
    }
    if (a.empty() || b.empty())
    {
      return false;
    }
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec) && !ec)
    {
      return true;
    }
    return false;
  }

  // Extension table from tools/mason_import.py: (ext -> server). Applies only
  // when none of the hand-tuned canonical rules matched.
  inline std::string attach_server_for_file(const std::string &lower)
  {
    for (const auto &e : kLspAttachTable)
    {
      if (lower_ends_with_dot_ext(lower, e.ext))
        return e.server;
    }
    // Dotless filenames such as `Dockerfile` / `meson.build`-style names.
    for (const auto &e : kLspAttachTable)
    {
      if (lower == e.ext)
        return e.server;
    }
    return "";
  }

  inline std::string detect_lsp_language(const std::string &filepath)
  {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".py")
      return "python";
    if (ends_with(lower, ".ts") || ends_with(lower, ".tsx") || ends_with(lower, ".mts")
        || ends_with(lower, ".cts") || ends_with(lower, ".js") || ends_with(lower, ".jsx")
        || ends_with(lower, ".mjs") || ends_with(lower, ".cjs"))
      return "typescript";
    if (lower.size() >= 2
        && (lower.substr(lower.size() - 2) == ".c" || lower.substr(lower.size() - 2) == ".h"))
      return "cpp";
    if (lower.size() >= 4
        && (lower.substr(lower.size() - 4) == ".cpp" || lower.substr(lower.size() - 4) == ".hpp"))
      return "cpp";
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".rs")
      return "rust";
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".go")
      return "go";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".lua")
      return "lua";
    if ((lower.size() >= 3 && lower.substr(lower.size() - 3) == ".sh")
        || (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".bash")
        || (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".zsh"))
      return "bash";
    if (ends_with(lower, ".html") || ends_with(lower, ".htm"))
      return "html";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".json")
      return "json";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".css")
      return "css";
    if (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".yaml")
      return "yaml";
    if (ends_with(lower, ".yml"))
      return "yaml";
    if (lower.size() >= 11 && lower.substr(lower.size() - 11) == ".dockerfile")
      return "dockerfile";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".vue")
      return "vue";
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".md")
      return "markdown";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".sql")
      return "sql";
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".php")
      return "php";
    // Everything past the hand-tuned rules above comes from the generated
    // mason-registry catalog (see tools/mason_import.py).
    return attach_server_for_file(lower);
  }

  inline std::vector<std::string> workspace_markers_for(const std::string &language)
  {
    if (language == "python")
    {
      return {"pyproject.toml", "setup.py", "setup.cfg", "requirements.txt", ".git"};
    }
    if (language == "typescript")
    {
      return {"package.json", "tsconfig.json", "jsconfig.json", ".git"};
    }
    if (language == "cpp")
    {
      return {"compile_commands.json", "compile_flags.txt", "CMakeLists.txt", ".clangd", ".git"};
    }
    if (language == "rust")
    {
      return {"Cargo.toml", "rust-project.json", ".git"};
    }
    if (language == "go")
    {
      return {"go.mod", "go.work", ".git"};
    }
    if (language == "lua")
    {
      return {".luarc.json", ".git"};
    }
    if (language == "bash")
    {
      return {".git"};
    }
    if (language == "html")
    {
      return {"package.json", ".git"};
    }
    return {".git"};
  }

  inline std::string find_workspace_root(const std::string &filepath, const std::string &language)
  {
    std::error_code ec;
    fs::path current = fs::absolute(fs::path(filepath).parent_path(), ec);
    if (ec)
    {
      current = fs::current_path(ec);
    }

    const std::vector<std::string> markers = workspace_markers_for(language);
    fs::path last;
    while (!current.empty() && current != last)
    {
      for (const auto &marker : markers)
      {
        if (fs::exists(current / marker, ec))
        {
          return current.string();
        }
      }
      last = current;
      current = current.parent_path();
    }

    current = fs::current_path(ec);
    return ec ? "." : current.string();
  }

  // True when the server's managed bin exists, or the bare name is on PATH.
  inline bool lsp_bin_available(const std::string &bin)
  {
    if (bin.empty())
      return false;
    if (!LspInstall::resolve_managed_bin(bin).empty())
      return true;
    const char *env = std::getenv("PATH");
    if (!env || !*env)
      return false;
    std::error_code ec;
    std::istringstream paths(env);
    std::string dir;
    while (std::getline(paths, dir, ':'))
    {
      if (dir.empty())
        continue;
#ifdef _WIN32
      const std::filesystem::path cand = std::filesystem::path(dir) / (bin + ".exe");
#else
      const std::filesystem::path cand = std::filesystem::path(dir) / bin;
#endif
      if (std::filesystem::is_regular_file(cand, ec))
        return true;
    }
    return false;
  }

  inline std::string language_id_for_attach(const std::string &lower)
  {
    for (const auto &e : kLspLangIdTable)
    {
      if (lower_ends_with_dot_ext(lower, e.ext))
        return e.langid;
    }
    return "";
  }

  // Full path to a binary the installer manages (installed under
  // <data>/lsp/bin), or the bare name so PATH is consulted.
  inline std::string resolve_lsp_bin(const std::string &bin)
  {
    const std::string managed = LspInstall::resolve_managed_bin(bin);
    return managed.empty() ? bin : managed;
  }

  inline std::vector<std::string> command_for_language(const std::string &language)
  {
    if (language == "python")
    {
#ifdef _WIN32
      const char *app_data = getenv("APPDATA");
      if (app_data)
      {
        fs::path venv = fs::path(app_data) / "jot" / "venv" / "Scripts" / "pylsp.exe";
        if (fs::exists(venv))
        {
          return {venv.string()};
        }
      }
#else
      const char *home = getenv("HOME");
      if (home)
      {
        fs::path venv = fs::path(home) / ".config" / "jot" / "venv" / "bin" / "pylsp";
        if (fs::exists(venv))
        {
          return {venv.string()};
        }
      }
#endif
      return {resolve_lsp_bin("pylsp")};
    }
    if (language == "pyright")
    {
      return {resolve_lsp_bin("pyright-langserver"), "--stdio"};
    }
    if (language == "typescript")
    {
      return {resolve_lsp_bin("typescript-language-server"), "--stdio"};
    }
    if (language == "cpp")
    {
      return {resolve_lsp_bin("clangd")};
    }
    if (language == "rust")
    {
      return {resolve_lsp_bin("rust-analyzer")};
    }
    if (language == "go")
    {
      return {resolve_lsp_bin("gopls")};
    }
    if (language == "lua")
    {
      return {resolve_lsp_bin("lua-language-server")};
    }
    if (language == "bash")
    {
      return {resolve_lsp_bin("bash-language-server"), "start"};
    }
    if (language == "html" || language == "json" || language == "css")
    {
      const char *bin = language == "html"    ? "vscode-html-language-server"
                        : language == "json"  ? "vscode-json-language-server"
                                               : "vscode-css-language-server";
      return {resolve_lsp_bin(bin), "--stdio"};
    }
    if (language == "yaml")
    {
      return {resolve_lsp_bin("yaml-language-server"), "--stdio"};
    }
    if (language == "dockerfile")
    {
      return {resolve_lsp_bin("docker-langserver"), "--stdio"};
    }
    if (language == "vue")
    {
      return {resolve_lsp_bin("vue-language-server"), "--stdio"};
    }
    if (language == "markdown")
    {
      return {resolve_lsp_bin("markdown-language-server"), "--stdio"};
    }
    if (language == "sql")
    {
      return {resolve_lsp_bin("sql-language-server"), "up", "--method", "stream"};
    }
    if (language == "php")
    {
      return {resolve_lsp_bin("intelephense"), "--stdio"};
    }
    // Servers from the generated catalog: launch their primary bin when it is
    // actually installed; args come from the well-known stdio table when the
    // server's launch contract is recorded there (bare otherwise).
    for (const auto &entry : kLspBinTable)
    {
      if (entry.server == language)
      {
        if (!lsp_bin_available(entry.bin))
          return {};
        std::vector<std::string> command = {resolve_lsp_bin(entry.bin)};
        for (const auto &args : kLspKnownArgs)
        {
          if (args.server == language)
          {
            std::istringstream iss(args.args);
            std::string token;
            while (iss >> token)
            {
              command.push_back(token);
            }
            break;
          }
        }
        return command;
      }
    }
    return {};
  }

  inline std::string language_id_for(const std::string &language, const std::string &filepath)
  {
    if (language == "typescript")
    {
      std::string lower = filepath;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (ends_with(lower, ".jsx"))
        return "javascriptreact";
      if (ends_with(lower, ".tsx"))
        return "typescriptreact";
      if (ends_with(lower, ".js") || ends_with(lower, ".mjs") || ends_with(lower, ".cjs"))
        return "javascript";
      return "typescript";
    }
    if (language == "cpp")
    {
      return "cpp";
    }
    if (language == "rust")
    {
      return "rust";
    }
    if (language == "go")
    {
      return "go";
    }
    if (language == "lua")
    {
      return "lua";
    }
    if (language == "bash")
    {
      return "shellscript";
    }
    if (language == "html")
    {
      return "html";
    }
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    const std::string attach_id = language_id_for_attach(lower);
    if (!attach_id.empty())
    {
      return attach_id;
    }
    return language;
  }

  inline bool is_html_filepath(const std::string &filepath)
  {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".html")
           || (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".htm");
  }

  inline bool is_script_lsp_filepath(const std::string &filepath)
  {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return ends_with(lower, ".js") || ends_with(lower, ".jsx") || ends_with(lower, ".mjs")
           || ends_with(lower, ".cjs") || ends_with(lower, ".ts") || ends_with(lower, ".tsx")
           || ends_with(lower, ".mts") || ends_with(lower, ".cts");
  }

  inline void append_html_builtin_completions(std::vector<LSPCompletionItem> &items)
  {
    auto add = [&](const std::string &label,
                   const std::string &insert,
                   const std::string &detail,
                   int kind = 10)
    {
      LSPCompletionItem item;
      item.label = label;
      item.insert_text = insert;
      item.filter_text = label;
      item.detail = detail;
      item.kind = kind;
      item.insert_text_format = 1;
      items.push_back(std::move(item));
    };
    add("html", "<html>|</html>", "HTML tag");
    add("head", "<head>|</head>", "HTML tag");
    add("body", "<body>|</body>", "HTML tag");
    add("title", "<title>|</title>", "HTML tag");
    add("main", "<main>|</main>", "HTML tag");
    add("section", "<section>|</section>", "HTML tag");
    add("article", "<article>|</article>", "HTML tag");
    add("header", "<header>|</header>", "HTML tag");
    add("footer", "<footer>|</footer>", "HTML tag");
    add("nav", "<nav>|</nav>", "HTML tag");
    add("div", "<div>|</div>", "HTML tag");
    add("span", "<span>|</span>", "HTML tag");
    add("p", "<p>|</p>", "HTML tag");
    add("a", "<a href=\"\">|</a>", "HTML anchor");
    add("img", "<img src=\"\" alt=\"\">", "HTML void tag");
    add("input", "<input type=\"text\">", "HTML void tag");
    add("button", "<button>|</button>", "HTML tag");
    add("ul", "<ul>|</ul>", "HTML tag");
    add("ol", "<ol>|</ol>", "HTML tag");
    add("li", "<li>|</li>", "HTML tag");
    add("form", "<form>|</form>", "HTML tag");
    add("label", "<label>|</label>", "HTML tag");
    add("script", "<script>|</script>", "HTML tag");
    add("style", "<style>|</style>", "HTML tag");
  }

  // "id1|id2|..." hint for the unknown-server message, from the Lua registry.
  // Defined in install.cpp (needs the full LuaAPI definition).
  std::string lsp_server_usage_hint(LuaAPI *api);
} // namespace lsp_internal