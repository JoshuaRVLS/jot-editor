#include "core/app/process_job.h"
#include "editor.h"
#include "lsp/client.h"
#include "lsp/install.h"
#include "lua_bridge/api.h"
#include "ui/text.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace
{
  std::string to_lower_copy(std::string s)
  {
    std::transform(
        s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
  }

  bool ends_with(const std::string &s, const std::string &suffix)
  {
    return s.size() >= suffix.size()
           && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  std::string detect_lsp_language(const std::string &filepath)
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
    return "";
  }

  std::vector<std::string> workspace_markers_for(const std::string &language)
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

  std::string find_workspace_root(const std::string &filepath, const std::string &language)
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

  // Defined later in this namespace (near the managed-bin helpers).
  std::string resolve_lsp_bin(const std::string &bin);
  std::string lsp_server_usage_hint(LuaAPI *api);

  std::vector<std::string> command_for_language(const std::string &language)
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
    return {};
  }

  std::string language_id_for(const std::string &language, const std::string &filepath)
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
    return language;
  }

  long long now_ms()
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  constexpr int kLspMouseHoverDelayMs = 450;

  bool is_identifier_char(char c)
  {
    unsigned char uc = (unsigned char)c;
    return std::isalnum(uc) || c == '_';
  }

  Cursor current_completion_start(const FileBuffer &buf)
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count())
    {
      return {0, 0};
    }
    const std::string &line = buf.line(buf.cursor.y);
    int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
    int start = cursor;
    while (start > 0 && is_identifier_char(line[start - 1]))
    {
      start--;
    }
    return {start, buf.cursor.y};
  }

  std::string completion_prefix_from(const FileBuffer &buf, const Cursor &start)
  {
    if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count() || start.y != buf.cursor.y)
    {
      return "";
    }
    const std::string &line = buf.line(buf.cursor.y);
    int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
    int prefix_start = std::clamp(start.x, 0, cursor);
    return line.substr((size_t)prefix_start, (size_t)(cursor - prefix_start));
  }

  bool is_subsequence_case_insensitive(const std::string &needle, const std::string &haystack)
  {
    if (needle.empty())
    {
      return true;
    }
    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); i++)
    {
      if (std::tolower((unsigned char)haystack[i]) == std::tolower((unsigned char)needle[j]))
      {
        j++;
      }
    }
    return j == needle.size();
  }

  int completion_match_score(const std::string &query, const LSPCompletionItem &item)
  {
    int score = query.empty() ? 50 : 0;

    const std::string q = to_lower_copy(query);
    const std::string label = to_lower_copy(item.label);
    const std::string filter =
        to_lower_copy(item.filter_text.empty() ? item.label : item.filter_text);
    const std::string insert = to_lower_copy(item.insert_text);

    if (!query.empty())
    {
      if (label == q || filter == q || insert == q)
      {
        score = 10000;
      }
      else if (label.rfind(q, 0) == 0 || filter.rfind(q, 0) == 0 || insert.rfind(q, 0) == 0)
      {
        score = 7000 - (int)label.size();
      }
      else
      {
        size_t label_pos = label.find(q);
        size_t filter_pos = filter.find(q);
        size_t insert_pos = insert.find(q);
        size_t best_pos = std::min(label_pos, std::min(filter_pos, insert_pos));
        if (best_pos != std::string::npos)
        {
          score = 4000 - (int)best_pos;
        }
        else if (is_subsequence_case_insensitive(q, label)
                 || is_subsequence_case_insensitive(q, filter))
        {
          score = 1500;
        }
      }
    }
    if (score <= 0)
    {
      return 0;
    }
    if (item.preselect)
    {
      score += 250;
    }
    if (item.deprecated)
    {
      score -= 200;
    }
    switch (item.kind)
    {
    case 2:
    case 3:
    case 5:
    case 6:
    case 10:
      score += 40;
      break;
    case 14:
      score -= 20;
      break;
    default:
      break;
    }
    return score;
  }

  struct SnippetExpansion
  {
    std::string text;
    int cursor_offset = -1;
  };

  SnippetExpansion expand_lsp_snippet(const std::string &snippet)
  {
    SnippetExpansion expansion;
    std::string &out = expansion.text;
    out.reserve(snippet.size());

    for (size_t i = 0; i < snippet.size(); i++)
    {
      char c = snippet[i];
      if (c == '\\')
      {
        if (i + 1 < snippet.size())
        {
          out.push_back(snippet[i + 1]);
          i++;
        }
        continue;
      }
      if (c != '$')
      {
        out.push_back(c);
        continue;
      }

      if (i + 1 >= snippet.size())
      {
        out.push_back(c);
        continue;
      }
      if (std::isdigit((unsigned char)snippet[i + 1]))
      {
        size_t start = i + 1;
        while (i + 1 < snippet.size() && std::isdigit((unsigned char)snippet[i + 1]))
        {
          i++;
        }
        int tabstop = std::atoi(snippet.substr(start, i - start + 1).c_str());
        if (tabstop == 0 || expansion.cursor_offset < 0)
        {
          expansion.cursor_offset = (int)out.size();
        }
        continue;
      }
      if (snippet[i + 1] == '{')
      {
        size_t j = i + 2;
        std::string inner;
        while (j < snippet.size() && snippet[j] != '}')
        {
          inner.push_back(snippet[j]);
          j++;
        }
        if (j < snippet.size() && snippet[j] == '}')
        {
          i = j;
        }
        else
        {
          i = snippet.size();
        }

        size_t pos = 0;
        while (pos < inner.size() && std::isdigit((unsigned char)inner[pos]))
        {
          pos++;
        }
        int tabstop = pos > 0 ? std::atoi(inner.substr(0, pos).c_str()) : -1;
        std::string value;
        if (pos < inner.size() && inner[pos] == ':')
        {
          value = inner.substr(pos + 1);
        }
        else if (pos < inner.size() && inner[pos] == '|')
        {
          size_t end = inner.find('|', pos + 1);
          std::string choices = end == std::string::npos ? inner.substr(pos + 1)
                                                         : inner.substr(pos + 1, end - pos - 1);
          size_t comma = choices.find(',');
          value = choices.substr(0, comma);
        }
        else if (pos == 0)
        {
          size_t colon = inner.find(':');
          value = colon == std::string::npos ? "" : inner.substr(colon + 1);
        }

        if (tabstop == 0 || (tabstop > 0 && expansion.cursor_offset < 0))
        {
          expansion.cursor_offset = (int)out.size();
        }
        out.append(value);
        continue;
      }
      out.push_back(c);
    }

    if (expansion.cursor_offset < 0)
    {
      expansion.cursor_offset = (int)out.size();
    }
    return expansion;
  }

  bool same_path(const std::string &a, const std::string &b)
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

  const char *diag_severity_hover_label(int severity)
  {
    switch (severity)
    {
      case 1:
        return "Error";
      case 2:
        return "Warning";
      case 3:
        return "Info";
      case 4:
        return "Hint";
      default:
        return "Diagnostic";
    }
  }

  // VSCode-style: the diagnostics whose range covers (line, col) lead the
  // hover popup, so hovering a squiggle shows the error message even when
  // the LSP server has no hover content at that spot. Renders up to 4
  // diagnostics, then a "… and N more" tail.
  std::string diagnostics_at_position_text(const std::vector<FileBuffer> &buffers,
                                           const std::string &filepath,
                                           int line,
                                           int col)
  {
    if (filepath.empty())
    {
      return {};
    }
    const FileBuffer *buf = nullptr;
    for (const auto &b : buffers)
    {
      if (same_path(b.filepath, filepath))
      {
        buf = &b;
        break;
      }
    }
    if (!buf)
    {
      return {};
    }
    std::string out;
    int shown = 0;
    int total = 0;
    for (const auto &d : buf->diagnostics)
    {
      bool covers = false;
      if (line >= d.line && line <= d.end_line)
      {
        if (line == d.line && line == d.end_line)
        {
          covers = col >= d.col && col <= d.end_col;
        }
        else if (line == d.line)
        {
          covers = col >= d.col;
        }
        else if (line == d.end_line)
        {
          covers = col <= d.end_col;
        }
        else
        {
          covers = true;
        }
      }
      if (!covers)
      {
        continue;
      }
      total++;
      if (shown < 4)
      {
        if (shown > 0)
        {
          out += "\n";
        }
        out += std::string(diag_severity_hover_label(d.severity)) + ": " + d.message;
        shown++;
      }
    }
    if (total > shown)
    {
      if (shown > 0)
      {
        out += "\n";
      }
      out += "… and " + std::to_string(total - shown) + " more";
    }
    return out;
  }

  std::string compact_lsp_popup_text(const std::string &text, int max_lines, int max_cols)
  {
    std::string out;
    std::string line;
    std::istringstream stream(text);
    int lines = 0;
    while (lines < max_lines && std::getline(stream, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      if ((int)line.size() > max_cols)
      {
        line = line.substr(0, (size_t)std::max(0, max_cols - 1)) + "...";
      }
      if (!out.empty())
      {
        out.push_back('\n');
      }
      out += line;
      lines++;
    }
    if (std::getline(stream, line))
    {
      out += "\n...";
    }
    return out;
  }

  bool lsp_popup_markdown_fence(const std::string &line)
  {
    size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
      start++;
    }
    return line.compare(start, 3, "```") == 0;
  }

  std::pair<int, int> lsp_popup_size(const std::string &text)
  {
    int max_w = 0;
    int lines = 0;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
      if (lsp_popup_markdown_fence(line))
      {
        continue;
      }
      max_w = std::max(max_w, ui_cell_count(line));
      lines++;
    }
    return {max_w + 2, std::max(1, lines) + 2};
  }

  std::pair<int, int> place_lsp_popup(int anchor_x,
                                      int anchor_y,
                                      int popup_w,
                                      int popup_h,
                                      int render_w,
                                      int screen_h,
                                      int status_h)
  {
    constexpr int top_chrome_h = 1;
    int usable_w = std::max(1, render_w);
    int bottom_exclusive = std::max(top_chrome_h + 1, screen_h - std::max(0, status_h));

    int x = std::clamp(anchor_x, 0, std::max(0, usable_w - popup_w));
    int y = anchor_y + 1;
    if (y + popup_h > bottom_exclusive)
    {
      y = anchor_y - popup_h - 1;
    }
    y = std::clamp(y, top_chrome_h, std::max(top_chrome_h, bottom_exclusive - popup_h));
    return {x, y};
  }

  // "id1|id2|..." hint for the unknown-server message, from the Lua registry.
  std::string lsp_server_usage_hint(LuaAPI *api)
  {
    std::vector<LspServerSpec> servers;
    if (api)
    {
      api->lsp_install_list(&servers);
    }
    std::string out;
    for (const auto &spec : servers)
    {
      if (!out.empty())
      {
        out += "|";
      }
      out += spec.id;
    }
    return out.empty() ? "see :help lspinstall" : out;
  }

  // Full path to a binary the installer manages (installed under
  // <data>/lsp/bin), or the bare name so PATH is consulted.
  std::string resolve_lsp_bin(const std::string &bin)
  {
    const std::string managed = LspInstall::resolve_managed_bin(bin);
    return managed.empty() ? bin : managed;
  }

  bool is_html_filepath(const std::string &filepath)
  {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".html")
           || (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".htm");
  }

  bool is_script_lsp_filepath(const std::string &filepath)
  {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return ends_with(lower, ".js") || ends_with(lower, ".jsx") || ends_with(lower, ".mjs")
           || ends_with(lower, ".cjs") || ends_with(lower, ".ts") || ends_with(lower, ".tsx")
           || ends_with(lower, ".mts") || ends_with(lower, ".cts");
  }

  void append_html_builtin_completions(std::vector<LSPCompletionItem> &items)
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
} // namespace

void Editor::poll_lsp_clients()
{
  const long long current_time = now_ms();
  std::vector<std::string> ready_changes;
  ready_changes.reserve(lsp_pending_changes.size());
  for (const auto &entry : lsp_pending_changes)
  {
    if (entry.second <= current_time)
    {
      ready_changes.push_back(entry.first);
    }
  }

  for (const auto &filepath : ready_changes)
  {
    lsp_pending_changes.erase(filepath);
    LSPClient *client = ensure_lsp_for_file(filepath);
    if (!client)
    {
      continue;
    }

    for (const auto &buf : buffers)
    {
      if (buf.filepath == filepath)
      {
        client->did_change(filepath, get_buffer_text(buf));
        break;
      }
    }
  }

  for (auto &client : lsp_clients)
  {
    const int stdout_fd = client ? client->get_stdout_fd() : -1;
    const int stderr_fd = client ? client->get_stderr_fd() : -1;
    if (client && client->poll())
    {
      needs_redraw = true;
    }
    if (client && !client->is_running())
    {
      if (stdout_fd >= 0)
        event_loop_.unwatch_fd(stdout_fd);
      if (stderr_fd >= 0)
        event_loop_.unwatch_fd(stderr_fd);
    }
    if (!client)
    {
      continue;
    }
    auto published = client->consume_published_diagnostics();
    for (auto &entry : published)
    {
      set_diagnostics(entry.first, entry.second);
    }

    auto completions = client->consume_completion_items();
    for (auto &entry : completions)
    {
      if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
      {
        continue;
      }

      auto &buf = get_buffer();
      if (!same_path(entry.first, buf.filepath))
      {
        continue;
      }

      // A Lua one-shot sink (jot.lsp.request_completion) consumes the items
      // first; the native completion popup only shows when Lua did not take
      // them.
      if (lua_api && lua_api->try_deliver_lsp_completion(entry.first, entry.second))
      {
        lsp_completion_manual_request = false;
        continue;
      }

      if (!lsp_completion_manual_request
          && (buf.cursor.y != lsp_completion_anchor.y
              || std::abs(buf.cursor.x - lsp_completion_anchor.x) > 4))
      {
        continue;
      }

      if (lua_api && lua_api->has_event_subscribers("lsp.completion"))
      {
        lua_api->emit_lsp_completion(entry.first, entry.second);
      }

      lsp_completion_all_items = std::move(entry.second);

      if (is_html_filepath(entry.first))
      {
        append_html_builtin_completions(lsp_completion_all_items);
      }

      lsp_completion_filepath = entry.first;
      bool visible = refresh_lsp_completion_filter();
      if (lsp_completion_manual_request && !visible)
      {
        set_message("No suggestions");
      }
      lsp_completion_manual_request = false;
      needs_redraw = true;
    }
    auto hovers = client->consume_hover_results();
    for (const auto &hover : hovers)
    {
      if (lua_api)
        lua_api->emit_lsp_hover(hover);
      handle_lsp_hover_result(hover);
    }

    auto definitions = client->consume_definition_results();
    for (const auto &definition : definitions)
    {
      if (lua_api)
        lua_api->emit_lsp_definition(definition);
      handle_lsp_definition_result(definition);
    }

    auto document_symbols = client->consume_document_symbol_results();
    for (const auto &symbols : document_symbols)
    {
      if (lua_api)
        lua_api->emit_lsp_symbols(symbols);
      handle_document_symbols_result(symbols);
    }
  }
}

void Editor::poll_lsp_installs()
{
  bool changed = false;
  for (auto &job : lsp_install_jobs)
  {
    if (!job.running)
    {
      continue;
    }

    // Gather new output lines from the active transport: the silent job's log
    // file when a background process is running, otherwise the fallback
    // integrated terminal.
    std::vector<std::string> lines;
    bool transport_dead = false;
    if (job.pid >= 0)
    {
      std::string text;
      job.output_offset = process_job::read_appended(job.output_path, job.output_offset, text);
      if (!text.empty())
      {
        // Only complete rows are parsed; a trailing unterminated line is not
        // yielded by getline and arrives on a later poll.
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line))
        {
          lines.push_back(line);
        }
      }
      if (process_job::reap_child(job.pid) >= 0)
      {
        transport_dead = true;
      }
    }
    else
    {
      IntegratedTerminal *term = get_integrated_terminal(job.terminal_index);
      if (!term)
      {
        job.running = false;
        job.failed = true;
        job.progress = "terminal closed";
        changed = true;
        continue;
      }
      lines = term->get_recent_lines(80);
      if (!term->is_active())
      {
        transport_dead = true;
      }
    }

    bool resolved = false;
    for (const auto &line : lines)
    {
      LspInstall::Marker marker;
      if (!LspInstall::parse_marker(line, marker) || marker.server != job.server)
      {
        continue;
      }
      if (marker.phase == "start")
      {
        const std::string next = job.removing ? "removing" : "downloading";
        if (job.progress != next)
        {
          job.progress = next;
          set_message("LSP " + std::string(job.removing ? "remove started: " : "install started: ")
                      + job.server);
        }
      }
      else if (marker.phase == "success" && marker.exit_code == 0)
      {
        job.progress = job.removing ? "removed" : "installed";
        job.running = false;
        job.succeeded = true;
        job.failed = false;
        resolved = true;
        set_message("LSP " + std::string(job.removing ? "remove OK: " : "install OK: ")
                    + job.server);
      }
      else if (marker.phase == "failed")
      {
        job.progress = marker.exit_code >= 0
                           ? "failed (exit " + std::to_string(marker.exit_code) + ")"
                           : "failed";
        job.running = false;
        job.succeeded = false;
        job.failed = true;
        resolved = true;
        set_message("LSP " + std::string(job.removing ? "remove failed: " : "install failed: ")
                    + job.server);
      }
      changed = true;
    }

    // The transport ended without the script reporting start/success/failure:
    // treat the job as failed (a successful script always prints a marker).
    if (job.running && transport_dead && !resolved)
    {
      job.running = false;
      job.succeeded = false;
      job.failed = true;
      job.progress = "process exited";
      set_message("LSP " + std::string(job.removing ? "remove failed: " : "install failed: ")
                  + job.server);
      changed = true;
    }
  }
  if (changed)
  {
    needs_redraw = true;
  }
}

void Editor::watch_lsp_client_fds(LSPClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else

  auto watch_read = [this](int fd)
  {
    if (fd < 0 || event_loop_.is_watching_fd(fd))
    {
      return;
    }
    event_loop_.watch_fd(fd,
                         true,
                         false,
                         [this, fd]
                         {
                           bool found = false;
                           for (auto &client : lsp_clients)
                           {
                             if (!client)
                             {
                               continue;
                             }
                             if (client->get_stdout_fd() == fd || client->get_stderr_fd() == fd)
                             {
                               found = true;
                               break;
                             }
                           }
                           if (!found)
                           {
                             event_loop_.unwatch_fd(fd);
                             return;
                           }
                           poll_lsp_clients();
                         });
  };

  watch_read(client->get_stdout_fd());
  watch_read(client->get_stderr_fd());
#endif
}

void Editor::unwatch_lsp_client_fds(LSPClient *client)
{
  if (!client)
  {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else
  if (client->get_stdout_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stdout_fd());
  }
  if (client->get_stderr_fd() >= 0)
  {
    event_loop_.unwatch_fd(client->get_stderr_fd());
  }
#endif
}

LSPClient *Editor::find_lsp_client(const std::string &language, const std::string &root_path)
{
  for (auto &client : lsp_clients)
  {
    if (client && client->get_language() == language && client->get_root_path() == root_path)
    {
      return client.get();
    }
  }
  return nullptr;
}

LSPClient *Editor::ensure_lsp_for_file(const std::string &filepath)
{
  if (filepath.empty())
  {
    return nullptr;
  }

  std::string language = detect_lsp_language(filepath);
  if (language.empty())
  {
    return nullptr;
  }
  if (lsp_disabled_servers.count(language))
  {
    return nullptr;
  }

  std::string root = find_workspace_root(filepath, language);
  if (LSPClient *existing = find_lsp_client(language, root))
  {
    if (!existing->is_running())
    {
      unwatch_lsp_client_fds(existing);
      existing->restart();
      watch_lsp_client_fds(existing);
    }
    return existing;
  }

  std::vector<std::string> command = command_for_language(language);
  if (command.empty())
  {
    return nullptr;
  }

  auto client = std::make_unique<LSPClient>(language, root, command);
  if (!client->start())
  {
    set_message("LSP start failed for " + language + ": " + client->get_last_error());
    return nullptr;
  }

  lsp_clients.push_back(std::move(client));
  watch_lsp_client_fds(lsp_clients.back().get());
  return lsp_clients.back().get();
}

std::string Editor::get_buffer_text(const FileBuffer &buf) const
{
  if (buf.is_lazy())
  {
    return "";
  }
  size_t total_size = buf.lines.empty() ? 0 : buf.lines.size() - 1;
  for (const auto &line : buf.lines)
  {
    total_size += line.size();
  }

  std::string text;
  text.reserve(total_size);
  for (size_t i = 0; i < buf.line_count(); i++)
  {
    if (i > 0)
    {
      text.push_back('\n');
    }
    text.append(buf.line(i));
  }
  return text;
}

void Editor::notify_lsp_open(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  set_diagnostics(filepath, {});
  LSPClient *client = ensure_lsp_for_file(filepath);
  if (!client)
  {
    return;
  }

  for (const auto &buf : buffers)
  {
    if (buf.filepath == filepath)
    {
      if (buf.is_lazy())
        return;
      client->did_open(
          filepath, language_id_for(client->get_language(), filepath), get_buffer_text(buf));
      break;
    }
  }
}

void Editor::notify_lsp_change(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes[filepath] = now_ms() + lsp_change_debounce_ms;
}

void Editor::notify_lsp_save(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes.erase(filepath);
  LSPClient *client = ensure_lsp_for_file(filepath);
  if (!client)
  {
    return;
  }

  for (const auto &buf : buffers)
  {
    if (buf.filepath == filepath)
    {
      client->did_save(filepath, get_buffer_text(buf));
      break;
    }
  }
}

void Editor::notify_lsp_close(const std::string &filepath)
{
  if (filepath.empty())
  {
    return;
  }
  lsp_pending_changes.erase(filepath);
  const std::string language = detect_lsp_language(filepath);
  if (language.empty())
  {
    return;
  }
  const std::string root = find_workspace_root(filepath, language);
  if (LSPClient *client = find_lsp_client(language, root))
  {
    client->did_close(filepath);
  }
}

void Editor::stop_all_lsp_clients()
{
  int stopped = 0;
  lsp_pending_changes.clear();
  for (auto &buf : buffers)
  {
    if (!buf.filepath.empty())
    {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  for (auto &client : lsp_clients)
  {
    if (client)
    {
      unwatch_lsp_client_fds(client.get());
    }
    if (client && client->is_running())
    {
      client->stop();
      stopped++;
    }
  }
  lsp_clients.clear();
  set_message("LSP stopped: " + std::to_string(stopped) + " client(s)");
}

void Editor::restart_all_lsp_clients()
{
  lsp_pending_changes.clear();
  for (auto &buf : buffers)
  {
    if (!buf.filepath.empty())
    {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  int restarted = 0;
  for (auto &client : lsp_clients)
  {
    if (!client)
    {
      continue;
    }
    unwatch_lsp_client_fds(client.get());
    if (client->restart())
    {
      watch_lsp_client_fds(client.get());
      restarted++;
    }
  }
  for (const auto &buf : buffers)
  {
    if (!buf.filepath.empty() && !buf.is_lazy())
    {
      notify_lsp_open(buf.filepath);
    }
  }
  set_message("LSP restarted: " + std::to_string(restarted) + " client(s)");
}

void Editor::set_lsp_server_enabled(const std::string &server, bool enabled)
{
  if (enabled)
  {
    lsp_disabled_servers.erase(server);
    if (!buffers.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size()
        && detect_lsp_language(get_buffer().filepath) == server)
    {
      notify_lsp_open(get_buffer().filepath);
    }
  }
  else
  {
    lsp_disabled_servers.insert(server);
    for (auto &client : lsp_clients)
    {
      if (client && client->get_language() == server)
      {
        unwatch_lsp_client_fds(client.get());
        client->stop();
      }
    }
    for (auto &buf : buffers)
    {
      if (detect_lsp_language(buf.filepath) == server)
      {
        buf.diagnostics.clear();
        lsp_pending_changes.erase(buf.filepath);
      }
    }
    invalidate_sidebar_diagnostics_cache();
  }
  save_workspace_session();
  needs_redraw = true;
}

std::string Editor::lsp_install_usage_hint() const
{
  return lsp_server_usage_hint(lua_api);
}

bool Editor::install_lsp_server(const std::string &name)
{
  // The Lua installer registry owns server resolution (ids + aliases) and
  // the per-manager install scripts; the native side only reports progress
  // through the background-job poll loop.
  std::string server, script, message;
  const bool known = lua_api && lua_api->lsp_install_plan(name, &server, &script, &message);
  if (!known || server.empty())
  {
    set_message("Unknown LSP server: " + name + " (use " + lsp_server_usage_hint(lua_api) + ")");
    return false;
  }
  if (script.empty())
  {
    set_message(message);
    needs_redraw = true;
    return false;
  }

  auto active_install =
      std::find_if(lsp_install_jobs.begin(),
                   lsp_install_jobs.end(),
                   [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (active_install != lsp_install_jobs.end())
  {
    set_message("LSP install/remove already running: " + server);
    needs_redraw = true;
    return true;
  }

  lsp_install_jobs.erase(std::remove_if(lsp_install_jobs.begin(),
                                        lsp_install_jobs.end(),
                                        [&](const LspInstallJob &job)
                                        { return job.server == server && !job.running; }),
                         lsp_install_jobs.end());

  LspInstallJob job;
  job.server = server;
  job.removing = false;
  job.progress = "starting";

  // Preferred path: a silent background job - no terminal panel opens, the
  // output streams into a log file and the poll loop reports progress.
  job.output_path = process_job::make_install_log_path("lsp-" + server);
#ifndef _WIN32
  job.pid = process_job::spawn_background_shell(LspInstall::wrap_script(server, script),
                                                job.output_path);
#endif
  if (job.pid >= 0)
  {
    lsp_install_jobs.push_back(std::move(job));
    set_message(message);
    needs_redraw = true;
    return true;
  }

  // Fallback (background spawn unavailable): run in an integrated terminal so
  // installs still work even where a detached process cannot be started.
  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspinstall:" + server);
  if (integrated_terminals.size() == terminal_count)
  {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active())
  {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  job.terminal_index = terminal_index;
  lsp_install_jobs.push_back(std::move(job));
  term->send_text(LspInstall::wrap_script(server, script) + "\r");
  set_message(message + " (terminal " + std::to_string(terminal_index + 1) + ")");
  needs_redraw = true;
  return true;
}

bool Editor::remove_lsp_server(const std::string &name)
{
  std::string server, script, message;
  const bool known = lua_api && lua_api->lsp_remove_plan(name, &server, &script, &message);
  if (!known || server.empty())
  {
    set_message("Unknown LSP server: " + name + " (use " + lsp_server_usage_hint(lua_api) + ")");
    return false;
  }
  if (script.empty())
  {
    set_message(message);
    needs_redraw = true;
    return false;
  }

  auto active_remove =
      std::find_if(lsp_install_jobs.begin(),
                   lsp_install_jobs.end(),
                   [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (active_remove != lsp_install_jobs.end())
  {
    set_message("LSP install/remove already running: " + server);
    needs_redraw = true;
    return true;
  }

  lsp_install_jobs.erase(std::remove_if(lsp_install_jobs.begin(),
                                        lsp_install_jobs.end(),
                                        [&](const LspInstallJob &job)
                                        { return job.server == server && !job.running; }),
                         lsp_install_jobs.end());

  set_lsp_server_enabled(server, false);

  LspInstallJob job;
  job.server = server;
  job.removing = true;
  job.progress = "starting";

  // Preferred path: a silent background job - no terminal panel opens.
  job.output_path = process_job::make_install_log_path("lsp-" + server);
#ifndef _WIN32
  job.pid = process_job::spawn_background_shell(LspInstall::wrap_script(server, script),
                                                job.output_path);
#endif
  if (job.pid >= 0)
  {
    lsp_install_jobs.push_back(std::move(job));
    set_message(message);
    needs_redraw = true;
    return true;
  }

  // Fallback (background spawn unavailable): run in an integrated terminal.
  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspremove:" + server);
  if (integrated_terminals.size() == terminal_count)
  {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active())
  {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  job.terminal_index = terminal_index;
  lsp_install_jobs.push_back(std::move(job));
  term->send_text(LspInstall::wrap_script(server, script) + "\r");
  set_message(message + " (terminal " + std::to_string(terminal_index + 1) + ")");
  needs_redraw = true;
  return true;
}

void Editor::hide_lsp_completion()
{
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_replace_start = {0, 0};
  lsp_completion_items.clear();
  lsp_completion_all_items.clear();
  lsp_completion_filepath.clear();
  lsp_completion_prefix.clear();
}

bool Editor::refresh_lsp_completion_filter()
{
  if (lsp_completion_all_items.empty() || buffers.empty() || panes.empty())
  {
    lsp_completion_visible = false;
    lsp_completion_items.clear();
    return false;
  }

  auto &buf = get_buffer();
  if (!lsp_completion_filepath.empty() && !same_path(lsp_completion_filepath, buf.filepath))
  {
    hide_lsp_completion();
    return false;
  }
  if (buf.cursor.y != lsp_completion_replace_start.y
      || buf.cursor.x < lsp_completion_replace_start.x)
  {
    hide_lsp_completion();
    return false;
  }

  std::string query = completion_prefix_from(buf, lsp_completion_replace_start);
  std::string selected_label;
  if (lsp_completion_selected >= 0 && lsp_completion_selected < (int)lsp_completion_items.size())
  {
    selected_label = lsp_completion_items[lsp_completion_selected].label;
  }

  std::vector<std::pair<int, LSPCompletionItem>> ranked;
  ranked.reserve(lsp_completion_all_items.size());
  for (const auto &item : lsp_completion_all_items)
  {
    int score = completion_match_score(query, item);
    if (query.empty() || score > 0)
    {
      ranked.push_back({score, item});
    }
  }

  std::stable_sort(ranked.begin(),
                   ranked.end(),
                   [](const auto &a, const auto &b)
                   {
                     if (a.first != b.first)
                     {
                       return a.first > b.first;
                     }
                     const std::string &as =
                         a.second.sort_text.empty() ? a.second.label : a.second.sort_text;
                     const std::string &bs =
                         b.second.sort_text.empty() ? b.second.label : b.second.sort_text;
                     return as < bs;
                   });

  lsp_completion_items.clear();
  const int max_items = 200;
  for (int i = 0; i < (int)ranked.size() && i < max_items; i++)
  {
    lsp_completion_items.push_back(std::move(ranked[i].second));
  }

  lsp_completion_prefix = query;
  lsp_completion_selected = 0;
  for (int i = 0; i < (int)lsp_completion_items.size(); i++)
  {
    if (!selected_label.empty() && lsp_completion_items[i].label == selected_label)
    {
      lsp_completion_selected = i;
      break;
    }
    if (selected_label.empty() && lsp_completion_items[i].preselect)
    {
      lsp_completion_selected = i;
      break;
    }
  }
  lsp_completion_visible = !lsp_completion_items.empty();
  return lsp_completion_visible;
}

void Editor::request_lsp_completion(bool manual, char trigger_character)
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    if (manual)
    {
      set_message("Save file first to use LSP completion");
    }
    return;
  }

  if (!manual)
  {
    if (!(std::isalnum((unsigned char)trigger_character) || trigger_character == '_'
          || trigger_character == '.' || trigger_character == ':' || trigger_character == '>'
          || trigger_character == '<' || trigger_character == '/'))
    {
      return;
    }

    int prefix_len = 0;
    int i = std::min(buf.cursor.x, (int)buf.line(buf.cursor.y).size());
    while (i > 0 && is_identifier_char(buf.line(buf.cursor.y)[i - 1]))
    {
      prefix_len++;
      i--;
    }
    bool html_file = is_html_filepath(buf.filepath);
    bool script_file = is_script_lsp_filepath(buf.filepath);
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':'
                               || trigger_character == '>' || trigger_character == '<'
                               || trigger_character == '/';
    int min_prefix = (html_file || script_file) ? 1 : 2;
    if (!punctuation_trigger && prefix_len < min_prefix)
    {
      return;
    }
  }

  Cursor replace_start = current_completion_start(buf);
  bool has_builtin_html = false;

  if (is_html_filepath(buf.filepath))
  {
    lsp_completion_all_items.clear();
    append_html_builtin_completions(lsp_completion_all_items);
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
    has_builtin_html = refresh_lsp_completion_filter();
    if (has_builtin_html)
    {
      needs_redraw = true;
    }
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    if (manual)
    {
      set_message("No LSP server for this file");
    }
    return;
  }

  // Completion must use current text state, not debounced change state.
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));

  char trigger = '\0';
  if (trigger_character == '.' || trigger_character == ':' || trigger_character == '>'
      || trigger_character == '<' || trigger_character == '/')
  {
    trigger = trigger_character;
  }

  if (!client->request_completion(buf.filepath, buf.cursor.y, buf.cursor.x, trigger))
  {
    if (manual)
    {
      set_message("LSP completion request failed");
    }
    return;
  }

  if (!has_builtin_html)
  {
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
  }
}

void Editor::request_lsp_hover()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, buf.cursor.y, buf.cursor.x))
  {
    set_message("LSP hover request failed");
    return;
  }
  set_message("LSP hover requested");
}

void Editor::request_lsp_hover_at(int pane_index,
                                  int buffer_id,
                                  const Cursor &pos,
                                  int token_start,
                                  int token_end,
                                  int screen_x,
                                  int screen_y)
{
  if (buffer_id < 0 || buffer_id >= (int)buffers.size())
  {
    cancel_lsp_mouse_hover();
    return;
  }
  const auto &buf = buffers[buffer_id];
  if (buf.is_lazy() || buf.filepath.empty())
  {
    cancel_lsp_mouse_hover();
    return;
  }
  if (pos.y < 0 || pos.y >= (int)buf.line_count() || token_start < 0 || token_end <= token_start)
  {
    cancel_lsp_mouse_hover();
    return;
  }

  if (lsp_mouse_hover_pending && lsp_mouse_hover_buffer == buffer_id
      && lsp_mouse_hover_line == pos.y && lsp_mouse_hover_token_start == token_start
      && lsp_mouse_hover_token_end == token_end)
  {
    lsp_mouse_hover_screen_x = screen_x;
    lsp_mouse_hover_screen_y = screen_y;
    return;
  }
  if (lsp_mouse_hover_visible && lsp_mouse_hover_buffer == buffer_id
      && lsp_mouse_hover_line == pos.y && lsp_mouse_hover_token_start == token_start
      && lsp_mouse_hover_token_end == token_end)
  {
    return;
  }

  if (lsp_mouse_hover_visible)
  {
    hide_popup();
  }
  lsp_mouse_hover_visible = false;
  lsp_mouse_hover_pending = true;
  lsp_mouse_hover_deadline_ms = now_ms() + kLspMouseHoverDelayMs;
  lsp_mouse_hover_pane = pane_index;
  lsp_mouse_hover_buffer = buffer_id;
  lsp_mouse_hover_line = pos.y;
  lsp_mouse_hover_col = pos.x;
  lsp_mouse_hover_token_start = token_start;
  lsp_mouse_hover_token_end = token_end;
  lsp_mouse_hover_screen_x = screen_x;
  lsp_mouse_hover_screen_y = screen_y;
  lsp_mouse_hover_filepath = buf.filepath;
}

void Editor::close_lua_hover_ui()
{
  if (lua_api)
  {
    lua_api->notify_lsp_hover_closed();
  }
}

void Editor::cancel_lsp_mouse_hover(bool hide_popup_now)
{
  // Any key, click, drag, paste or hover replacement dismisses the current
  // hover; when Lua renders the hover UI, mirror that by closing its float.
  close_lua_hover_ui();
  lsp_mouse_hover_pending = false;
  lsp_mouse_hover_deadline_ms = 0;
  lsp_mouse_hover_pane = -1;
  lsp_mouse_hover_buffer = -1;
  lsp_mouse_hover_line = -1;
  lsp_mouse_hover_col = -1;
  lsp_mouse_hover_token_start = -1;
  lsp_mouse_hover_token_end = -1;
  lsp_mouse_hover_screen_x = -1;
  lsp_mouse_hover_screen_y = -1;
  lsp_mouse_hover_filepath.clear();
  if (hide_popup_now && lsp_mouse_hover_visible)
  {
    hide_popup();
    lsp_mouse_hover_visible = false;
  }
}

void Editor::maybe_fire_lsp_mouse_hover()
{
  if (!lsp_mouse_hover_pending || now_ms() < lsp_mouse_hover_deadline_ms)
  {
    return;
  }
  if (show_context_menu || show_command_palette || show_search || telescope.is_active()
      || mouse_selecting || mouse_drag_started)
  {
    return;
  }
  if (lsp_mouse_hover_buffer < 0 || lsp_mouse_hover_buffer >= (int)buffers.size())
  {
    cancel_lsp_mouse_hover();
    return;
  }

  auto &buf = buffers[lsp_mouse_hover_buffer];
  if (buf.is_lazy() || !same_path(buf.filepath, lsp_mouse_hover_filepath))
  {
    cancel_lsp_mouse_hover();
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    cancel_lsp_mouse_hover(false);
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, lsp_mouse_hover_line, lsp_mouse_hover_col))
  {
    cancel_lsp_mouse_hover(false);
    return;
  }
  lsp_mouse_hover_pending = false;
}

void Editor::request_lsp_definition()
{
  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    return;
  }
  if (buf.filepath.empty())
  {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client)
  {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_definition(buf.filepath, buf.cursor.y, buf.cursor.x))
  {
    set_message("LSP definition request failed");
    return;
  }
  set_message("LSP definition requested");
}

void Editor::handle_lsp_hover_result(const LSPHoverResult &hover)
{
  // A Lua one-shot sink (jot.lsp.request_hover) consumes this result first;
  // the native popup only shows when Lua did not take it.
  if (lua_api && lua_api->try_deliver_lsp_hover(hover))
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  // VSCode-style: diagnostics covering the hover position lead the popup, so
  // hovering the squiggle shows the error message even when the server has
  // no hover content. The merged text flows to the Lua hover UI and the
  // native popup alike.
  const std::string diag_text = diagnostics_at_position_text(
      buffers, hover.origin_filepath, hover.origin_line, hover.origin_character);
  std::string contents = hover.contents;
  if (!diag_text.empty())
  {
    contents = diag_text;
    if (!hover.contents.empty())
    {
      contents += "\n\n" + hover.contents;
    }
  }
  if (lsp_mouse_hover_buffer >= 0 && same_path(hover.origin_filepath, lsp_mouse_hover_filepath)
      && hover.origin_line == lsp_mouse_hover_line && hover.origin_character == lsp_mouse_hover_col)
  {
    if (contents.empty())
    {
      lsp_mouse_hover_visible = false;
      close_lua_hover_ui();
      needs_redraw = true;
      return;
    }
    int popup_x = lsp_mouse_hover_screen_x + 2;
    int popup_y = lsp_mouse_hover_screen_y;
    // Lua hover UI (jot.lsp.hover_ui) renders the popup when registered; the
    // native popup only shows when no handler consumed the result. The Lua
    // float clamps itself to the screen, so raw anchor coordinates are used.
    if (lua_api && lua_api->has_lsp_hover_ui()
        && lua_api->present_lsp_hover(contents,
                                      hover.origin_filepath,
                                      hover.origin_line,
                                      hover.origin_character,
                                      "mouse",
                                      popup_x,
                                      popup_y))
    {
      lsp_mouse_hover_visible = true;
      needs_redraw = true;
      return;
    }
    std::string text = compact_lsp_popup_text(contents, 14, 96);
    if (ui)
    {
      auto [popup_w, popup_h] = lsp_popup_size(text);
      auto [placed_x, placed_y] = place_lsp_popup(popup_x,
                                                  popup_y,
                                                  popup_w,
                                                  popup_h,
                                                  ui->get_render_width(),
                                                  ui->get_height(),
                                                  status_height);
      popup_x = placed_x;
      popup_y = placed_y;
    }
    else
    {
      popup_y += 1;
    }
    show_hover_popup(text, popup_x, popup_y);
    lsp_mouse_hover_visible = true;
    return;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, hover.origin_filepath) || buf.cursor.y != hover.origin_line
      || buf.cursor.x != hover.origin_character)
  {
    return;
  }
  if (contents.empty())
  {
    set_message("No hover information");
    return;
  }

  const SplitPane &pane = get_pane();
  constexpr int line_num_width = 8;
  int row = hover.origin_line - buf.scroll_offset;
  int max_row = std::max(0, pane.h - tab_height - 1);
  int anchor_y = pane.y + tab_height + std::clamp(row, 0, max_row);
  int popup_x =
      pane.x + 1 + line_num_width + std::max(0, hover.origin_character - buf.scroll_x) + 2;
  // Lua hover UI: same hook as the mouse path, anchored at the cursor line.
  if (lua_api && lua_api->has_lsp_hover_ui()
      && lua_api->present_lsp_hover(contents,
                                    hover.origin_filepath,
                                    hover.origin_line,
                                    hover.origin_character,
                                    "cursor",
                                    popup_x,
                                    anchor_y))
  {
    needs_redraw = true;
    return;
  }
  std::string text = compact_lsp_popup_text(contents, 14, 96);
  if (ui)
  {
    auto [popup_w, popup_h] = lsp_popup_size(text);
    auto [placed_x, placed_y] = place_lsp_popup(popup_x,
                                                anchor_y,
                                                popup_w,
                                                popup_h,
                                                ui->get_render_width(),
                                                ui->get_height(),
                                                status_height);
    popup_x = placed_x;
    anchor_y = placed_y;
  }
  else
  {
    anchor_y += 1;
  }
  show_hover_popup(text, popup_x, anchor_y);
}

void Editor::handle_lsp_definition_result(const LSPDefinitionResult &definition)
{
  // Lua one-shot sink (jot.lsp.request_definition) takes precedence over the
  // native jump when it is waiting for this result.
  if (lua_api && lua_api->try_deliver_lsp_definition(definition))
  {
    return;
  }
  if (buffers.empty() || current_buffer < 0 || current_buffer >= (int)buffers.size())
  {
    return;
  }
  auto &buf = get_buffer();
  if (!same_path(buf.filepath, definition.origin_filepath) || buf.cursor.y != definition.origin_line
      || buf.cursor.x != definition.origin_character)
  {
    return;
  }
  if (definition.locations.empty())
  {
    set_message("No definition found");
    return;
  }

  LSPJumpLocation origin;
  origin.filepath = buf.filepath;
  origin.cursor = buf.cursor;
  origin.scroll_offset = buf.scroll_offset;
  origin.scroll_x = buf.scroll_x;
  origin.preview = buf.is_preview;
  lsp_jump_stack.push_back(origin);
  if (lsp_jump_stack.size() > 50)
  {
    lsp_jump_stack.erase(lsp_jump_stack.begin());
  }

  lsp_definition_pending_location = definition.locations.front();
  lsp_definition_jump_pending = true;
  const bool same_file = same_path(buf.filepath, lsp_definition_pending_location.filepath);
  open_file(lsp_definition_pending_location.filepath, !same_file);
  apply_pending_lsp_definition_jump();
}

bool Editor::apply_pending_lsp_definition_jump()
{
  if (!lsp_definition_jump_pending || buffers.empty() || current_buffer < 0
      || current_buffer >= (int)buffers.size())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, lsp_definition_pending_location.filepath))
  {
    return false;
  }

  buf.cursor.y =
      std::clamp(lsp_definition_pending_location.line, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(lsp_definition_pending_location.character, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  ensure_cursor_visible();
  lsp_definition_jump_pending = false;
  set_message("Definition: " + get_filename(buf.filepath) + ":" + std::to_string(buf.cursor.y + 1));
  needs_redraw = true;
  return true;
}

bool Editor::apply_pending_lsp_back_jump()
{
  if (!lsp_back_jump_pending || buffers.empty() || current_buffer < 0
      || current_buffer >= (int)buffers.size())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, lsp_back_pending_location.filepath))
  {
    return false;
  }

  buf.cursor.y =
      std::clamp(lsp_back_pending_location.cursor.y, 0, std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(lsp_back_pending_location.cursor.x, 0, (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  buf.scroll_offset = std::max(0, lsp_back_pending_location.scroll_offset);
  buf.scroll_x = std::max(0, lsp_back_pending_location.scroll_x);
  clear_selection();
  ensure_cursor_visible();
  lsp_back_jump_pending = false;
  set_message("Returned: " + get_filename(buf.filepath) + ":" + std::to_string(buf.cursor.y + 1));
  needs_redraw = true;
  return true;
}

void Editor::return_from_lsp_definition()
{
  if (lsp_jump_stack.empty())
  {
    set_message("No LSP jump to return to");
    return;
  }

  lsp_back_pending_location = lsp_jump_stack.back();
  lsp_jump_stack.pop_back();
  lsp_back_jump_pending = true;
  open_file(lsp_back_pending_location.filepath, lsp_back_pending_location.preview);
  apply_pending_lsp_back_jump();
}

bool Editor::apply_selected_lsp_completion()
{
  if (!lsp_completion_visible || lsp_completion_items.empty())
  {
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy())
  {
    buf.materialize();
  }
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size())
  {
    hide_lsp_completion();
    return false;
  }

  int idx = std::clamp(lsp_completion_selected, 0, (int)lsp_completion_items.size() - 1);
  const auto &item = lsp_completion_items[idx];
  std::string text = item.insert_text.empty() ? item.label : item.insert_text;

  int cursor_offset = -1;
  size_t marker_pos = text.find('|');
  if (marker_pos != std::string::npos)
  {
    cursor_offset = (int)marker_pos;
    text.erase(marker_pos, 1);
  }

  if (item.insert_text_format == 2)
  {
    SnippetExpansion expansion = expand_lsp_snippet(text);
    text = std::move(expansion.text);
    cursor_offset = expansion.cursor_offset;
  }
  if (text.empty())
  {
    hide_lsp_completion();
    return false;
  }

  save_state();

  std::string &line = buf.line_mut(buf.cursor.y);
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int start = cursor;
  int end = cursor;

  if (item.has_text_edit_range && item.edit_start_line == buf.cursor.y
      && item.edit_end_line == buf.cursor.y)
  {
    start = std::clamp(item.edit_start_char, 0, (int)line.size());
    end = std::clamp(item.edit_end_char, start, (int)line.size());
  }
  else if (lsp_completion_replace_start.y == buf.cursor.y)
  {
    start = std::clamp(lsp_completion_replace_start.x, 0, cursor);
  }
  else
  {
    while (start > 0 && is_identifier_char(line[start - 1]))
    {
      start--;
    }
  }

  if (start < end)
  {
    line.erase(start, end - start);
    cursor = start;
  }
  else if (start < cursor)
  {
    line.erase(start, cursor - start);
    cursor = start;
  }
  std::string tail = line.substr(cursor);
  line.erase(cursor);

  size_t segment_start = 0;
  std::vector<std::string> inserted_lines;
  while (true)
  {
    size_t nl = text.find('\n', segment_start);
    if (nl == std::string::npos)
    {
      inserted_lines.push_back(text.substr(segment_start));
      break;
    }
    inserted_lines.push_back(text.substr(segment_start, nl - segment_start));
    segment_start = nl + 1;
  }
  if (inserted_lines.empty())
  {
    inserted_lines.push_back("");
  }

  line.insert(cursor, inserted_lines.front());
  int insert_line = buf.cursor.y;
  for (size_t i = 1; i < inserted_lines.size(); i++)
  {
    buf.lines.insert(buf.lines.begin() + insert_line + (int)i, inserted_lines[i]);
  }
  buf.line_mut(insert_line + (int)inserted_lines.size() - 1) += tail;

  int target_offset = cursor_offset >= 0 ? cursor_offset : (int)text.size();
  int target_line_delta = 0;
  int target_col = cursor;
  for (int i = 0; i < target_offset && i < (int)text.size(); i++)
  {
    if (text[i] == '\n')
    {
      target_line_delta++;
      target_col = 0;
    }
    else
    {
      target_col++;
    }
  }
  buf.cursor.y = insert_line + target_line_delta;
  buf.cursor.x = target_col;
  buf.preferred_x = buf.cursor.x;
  buf.modified = true;
  buf.selection.active = false;
  ensure_cursor_visible();
  needs_redraw = true;

  if (lua_api)
  {
    lua_api->on_buffer_change(buf.filepath, "");
  }
  notify_lsp_change(buf.filepath);

  hide_lsp_completion();
  return true;
}
