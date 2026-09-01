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

namespace {
struct LspServerSpec {
  const char *id;
  const char *label;
  const char *detail;
  bool managed;
};

const std::vector<LspServerSpec> &lsp_server_specs() {
  static const std::vector<LspServerSpec> specs = {
      {"python", "Python", "pylsp", true},
      {"typescript", "TypeScript", "JS, JSX, TS, TSX", true},
      {"cpp", "C / C++", "clangd - package manager", false},
      {"rust", "Rust", "rust-analyzer - package manager", false},
      {"go", "Go", "gopls - package manager", false},
      {"lua", "Lua", "lua-language-server - package manager", false},
      {"bash", "Bash", "bash-language-server", true},
      {"html", "HTML", "vscode-html-language-server", true},
  };
  return specs;
}

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

bool ends_with(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string detect_lsp_language(const std::string &filepath) {
  std::string lower = filepath;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".py")
    return "python";
  if (ends_with(lower, ".ts") || ends_with(lower, ".tsx") ||
      ends_with(lower, ".mts") || ends_with(lower, ".cts") ||
      ends_with(lower, ".js") || ends_with(lower, ".jsx") ||
      ends_with(lower, ".mjs") || ends_with(lower, ".cjs"))
    return "typescript";
  if (lower.size() >= 2 &&
      (lower.substr(lower.size() - 2) == ".c" || lower.substr(lower.size() - 2) == ".h"))
    return "cpp";
  if (lower.size() >= 4 &&
      (lower.substr(lower.size() - 4) == ".cpp" ||
       lower.substr(lower.size() - 4) == ".hpp"))
    return "cpp";
  if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".rs")
    return "rust";
  if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".go")
    return "go";
  if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".lua")
    return "lua";
  if ((lower.size() >= 3 && lower.substr(lower.size() - 3) == ".sh") ||
      (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".bash") ||
      (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".zsh"))
    return "bash";
  if (ends_with(lower, ".html") || ends_with(lower, ".htm"))
    return "html";
  return "";
}

std::vector<std::string> workspace_markers_for(const std::string &language) {
  if (language == "python") {
    return {"pyproject.toml", "setup.py", "setup.cfg", "requirements.txt",
            ".git"};
  }
  if (language == "typescript") {
    return {"package.json", "tsconfig.json", "jsconfig.json", ".git"};
  }
  if (language == "cpp") {
    return {"compile_commands.json", "compile_flags.txt", "CMakeLists.txt",
            ".clangd", ".git"};
  }
  if (language == "rust") {
    return {"Cargo.toml", "rust-project.json", ".git"};
  }
  if (language == "go") {
    return {"go.mod", "go.work", ".git"};
  }
  if (language == "lua") {
    return {".luarc.json", ".git"};
  }
  if (language == "bash") {
    return {".git"};
  }
  if (language == "html") {
    return {"package.json", ".git"};
  }
  return {".git"};
}

std::string find_workspace_root(const std::string &filepath,
                                const std::string &language) {
  std::error_code ec;
  fs::path current = fs::absolute(fs::path(filepath).parent_path(), ec);
  if (ec) {
    current = fs::current_path(ec);
  }

  const std::vector<std::string> markers = workspace_markers_for(language);
  fs::path last;
  while (!current.empty() && current != last) {
    for (const auto &marker : markers) {
      if (fs::exists(current / marker, ec)) {
        return current.string();
      }
    }
    last = current;
    current = current.parent_path();
  }

  current = fs::current_path(ec);
  return ec ? "." : current.string();
}

std::vector<std::string> command_for_language(const std::string &language) {
  if (language == "python") {
#ifdef _WIN32
    const char *app_data = getenv("APPDATA");
    if (app_data) {
      fs::path venv = fs::path(app_data) / "jot" / "venv" / "Scripts" / "pylsp.exe";
      if (fs::exists(venv)) {
        return {venv.string()};
      }
    }
#else
    const char *home = getenv("HOME");
    if (home) {
      fs::path venv = fs::path(home) / ".config" / "jot" / "venv" / "bin" / "pylsp";
      if (fs::exists(venv)) {
        return {venv.string()};
      }
    }
#endif
    return {"pylsp"};
  }
  if (language == "typescript") {
    return {"typescript-language-server", "--stdio"};
  }
  if (language == "cpp") {
    return {"clangd"};
  }
  if (language == "rust") {
    return {"rust-analyzer"};
  }
  if (language == "go") {
    return {"gopls"};
  }
  if (language == "lua") {
    return {"lua-language-server"};
  }
  if (language == "bash") {
    return {"bash-language-server", "start"};
  }
  if (language == "html") {
    return {"vscode-html-language-server", "--stdio"};
  }
  return {};
}

std::string language_id_for(const std::string &language, const std::string &filepath) {
  if (language == "typescript") {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (ends_with(lower, ".jsx"))
      return "javascriptreact";
    if (ends_with(lower, ".tsx"))
      return "typescriptreact";
    if (ends_with(lower, ".js") || ends_with(lower, ".mjs") ||
        ends_with(lower, ".cjs"))
      return "javascript";
    return "typescript";
  }
  if (language == "cpp") {
    return "cpp";
  }
  if (language == "rust") {
    return "rust";
  }
  if (language == "go") {
    return "go";
  }
  if (language == "lua") {
    return "lua";
  }
  if (language == "bash") {
    return "shellscript";
  }
  if (language == "html") {
    return "html";
  }
  return language;
}

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
}

constexpr int kLspMouseHoverDelayMs = 450;

bool is_identifier_char(char c) {
  unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

Cursor current_completion_start(const FileBuffer &buf) {
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count()) {
    return {0, 0};
  }
  const std::string &line = buf.line(buf.cursor.y);
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int start = cursor;
  while (start > 0 && is_identifier_char(line[start - 1])) {
    start--;
  }
  return {start, buf.cursor.y};
}

std::string completion_prefix_from(const FileBuffer &buf, const Cursor &start) {
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.line_count() ||
      start.y != buf.cursor.y) {
    return "";
  }
  const std::string &line = buf.line(buf.cursor.y);
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int prefix_start = std::clamp(start.x, 0, cursor);
  return line.substr((size_t)prefix_start, (size_t)(cursor - prefix_start));
}

bool is_subsequence_case_insensitive(const std::string &needle,
                                     const std::string &haystack) {
  if (needle.empty()) {
    return true;
  }
  size_t j = 0;
  for (size_t i = 0; i < haystack.size() && j < needle.size(); i++) {
    if (std::tolower((unsigned char)haystack[i]) ==
        std::tolower((unsigned char)needle[j])) {
      j++;
    }
  }
  return j == needle.size();
}

int completion_match_score(const std::string &query, const LSPCompletionItem &item) {
  int score = query.empty() ? 50 : 0;

  const std::string q = to_lower_copy(query);
  const std::string label = to_lower_copy(item.label);
  const std::string filter = to_lower_copy(item.filter_text.empty()
                                               ? item.label
                                               : item.filter_text);
  const std::string insert = to_lower_copy(item.insert_text);

  if (!query.empty()) {
    if (label == q || filter == q || insert == q) {
      score = 10000;
    } else if (label.rfind(q, 0) == 0 || filter.rfind(q, 0) == 0 ||
               insert.rfind(q, 0) == 0) {
      score = 7000 - (int)label.size();
    } else {
      size_t label_pos = label.find(q);
      size_t filter_pos = filter.find(q);
      size_t insert_pos = insert.find(q);
      size_t best_pos = std::min(label_pos, std::min(filter_pos, insert_pos));
      if (best_pos != std::string::npos) {
        score = 4000 - (int)best_pos;
      } else if (is_subsequence_case_insensitive(q, label) ||
                 is_subsequence_case_insensitive(q, filter)) {
        score = 1500;
      }
    }
  }
  if (score <= 0) {
    return 0;
  }
  if (item.preselect) {
    score += 250;
  }
  if (item.deprecated) {
    score -= 200;
  }
  switch (item.kind) {
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

struct SnippetExpansion {
  std::string text;
  int cursor_offset = -1;
};

SnippetExpansion expand_lsp_snippet(const std::string &snippet) {
  SnippetExpansion expansion;
  std::string &out = expansion.text;
  out.reserve(snippet.size());

  for (size_t i = 0; i < snippet.size(); i++) {
    char c = snippet[i];
    if (c == '\\') {
      if (i + 1 < snippet.size()) {
        out.push_back(snippet[i + 1]);
        i++;
      }
      continue;
    }
    if (c != '$') {
      out.push_back(c);
      continue;
    }

    if (i + 1 >= snippet.size()) {
      out.push_back(c);
      continue;
    }
    if (std::isdigit((unsigned char)snippet[i + 1])) {
      size_t start = i + 1;
      while (i + 1 < snippet.size() &&
             std::isdigit((unsigned char)snippet[i + 1])) {
        i++;
      }
      int tabstop = std::atoi(snippet.substr(start, i - start + 1).c_str());
      if (tabstop == 0 || expansion.cursor_offset < 0) {
        expansion.cursor_offset = (int)out.size();
      }
      continue;
    }
    if (snippet[i + 1] == '{') {
      size_t j = i + 2;
      std::string inner;
      while (j < snippet.size() && snippet[j] != '}') {
        inner.push_back(snippet[j]);
        j++;
      }
      if (j < snippet.size() && snippet[j] == '}') {
        i = j;
      } else {
        i = snippet.size();
      }

      size_t pos = 0;
      while (pos < inner.size() &&
             std::isdigit((unsigned char)inner[pos])) {
        pos++;
      }
      int tabstop = pos > 0 ? std::atoi(inner.substr(0, pos).c_str()) : -1;
      std::string value;
      if (pos < inner.size() && inner[pos] == ':') {
        value = inner.substr(pos + 1);
      } else if (pos < inner.size() && inner[pos] == '|') {
        size_t end = inner.find('|', pos + 1);
        std::string choices =
            end == std::string::npos ? inner.substr(pos + 1)
                                     : inner.substr(pos + 1, end - pos - 1);
        size_t comma = choices.find(',');
        value = choices.substr(0, comma);
      } else if (pos == 0) {
        size_t colon = inner.find(':');
        value = colon == std::string::npos ? "" : inner.substr(colon + 1);
      }

      if (tabstop == 0 || (tabstop > 0 && expansion.cursor_offset < 0)) {
        expansion.cursor_offset = (int)out.size();
      }
      out.append(value);
      continue;
    }
    out.push_back(c);
  }

  if (expansion.cursor_offset < 0) {
    expansion.cursor_offset = (int)out.size();
  }
  return expansion;
}

bool same_path(const std::string &a, const std::string &b) {
  if (a == b) {
    return true;
  }
  if (a.empty() || b.empty()) {
    return false;
  }
  std::error_code ec;
  if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec) &&
      !ec) {
    return true;
  }
  return false;
}

std::string compact_lsp_popup_text(const std::string &text, int max_lines,
                                   int max_cols) {
  std::string out;
  std::string line;
  std::istringstream stream(text);
  int lines = 0;
  while (lines < max_lines && std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if ((int)line.size() > max_cols) {
      line = line.substr(0, (size_t)std::max(0, max_cols - 1)) + "...";
    }
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += line;
    lines++;
  }
  if (std::getline(stream, line)) {
    out += "\n...";
  }
  return out;
}

bool lsp_popup_markdown_fence(const std::string &line) {
  size_t start = 0;
  while (start < line.size() &&
         (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }
  return line.compare(start, 3, "```") == 0;
}

std::pair<int, int> lsp_popup_size(const std::string &text) {
  int max_w = 0;
  int lines = 0;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (lsp_popup_markdown_fence(line)) {
      continue;
    }
    max_w = std::max(max_w, ui_cell_count(line));
    lines++;
  }
  return {max_w + 2, std::max(1, lines) + 2};
}

std::pair<int, int> place_lsp_popup(int anchor_x, int anchor_y, int popup_w,
                                    int popup_h, int render_w, int screen_h,
                                    int status_h) {
  constexpr int top_chrome_h = 1;
  int usable_w = std::max(1, render_w);
  int bottom_exclusive =
      std::max(top_chrome_h + 1, screen_h - std::max(0, status_h));

  int x = std::clamp(anchor_x, 0, std::max(0, usable_w - popup_w));
  int y = anchor_y + 1;
  if (y + popup_h > bottom_exclusive) {
    y = anchor_y - popup_h - 1;
  }
  y = std::clamp(y, top_chrome_h,
                 std::max(top_chrome_h, bottom_exclusive - popup_h));
  return {x, y};
}

bool command_exists(const std::string &name) {
  if (name.empty()) {
    return false;
  }
#ifdef _WIN32
  std::string cmd = "where " + name + " >NUL 2>NUL";
#else
  std::string cmd = "command -v " + name + " >/dev/null 2>&1";
#endif
  return std::system(cmd.c_str()) == 0;
}

std::string normalize_lsp_server_name(const std::string &raw) {
  std::string n = to_lower_copy(raw);
  if (n == "py" || n == "pylsp") {
    return "python";
  }
  if (n == "ts" || n == "js" || n == "javascript" || n == "jsx" ||
      n == "tsx" || n == "typescript-language-server") {
    return "typescript";
  }
  if (n == "c" || n == "c++" || n == "clangd") {
    return "cpp";
  }
  if (n == "rs" || n == "rust" || n == "rust-analyzer") {
    return "rust";
  }
  if (n == "golang" || n == "go" || n == "gopls") {
    return "go";
  }
  if (n == "lua" || n == "lua_ls" || n == "lua-language-server") {
    return "lua";
  }
  if (n == "sh" || n == "shell" || n == "bash" || n == "bashls" ||
      n == "bash-language-server") {
    return "bash";
  }
  if (n == "python" || n == "typescript" || n == "cpp" || n == "rust" ||
      n == "go" || n == "lua" || n == "bash") {
    return n;
  }
  if (n == "html" || n == "htm" || n == "vscode-html-language-server") {
    return "html";
  }
  return "";
}

bool is_lsp_server_installed(const std::string &server) {
  if (server == "python") {
#ifdef _WIN32
    const char *app_data = std::getenv("APPDATA");
    if (app_data) {
      fs::path local = fs::path(app_data) / "jot" / "venv" / "Scripts" / "pylsp.exe";
      if (fs::exists(local)) {
        return true;
      }
    }
#else
    const char *home = std::getenv("HOME");
    if (home) {
      fs::path local = fs::path(home) / ".config" / "jot" / "venv" / "bin" / "pylsp";
      if (fs::exists(local)) {
        return true;
      }
    }
#endif
    return command_exists("pylsp");
  }
  if (server == "typescript") {
    return command_exists("typescript-language-server");
  }
  if (server == "cpp") {
    return command_exists("clangd");
  }
  if (server == "rust") {
    return command_exists("rust-analyzer");
  }
  if (server == "go") {
    return command_exists("gopls");
  }
  if (server == "lua") {
    return command_exists("lua-language-server");
  }
  if (server == "bash") {
    return command_exists("bash-language-server");
  }
  if (server == "html") {
    return command_exists("vscode-html-language-server");
  }
  return false;
}

bool is_html_filepath(const std::string &filepath) {
  std::string lower = filepath;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".html") ||
         (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".htm");
}

bool is_script_lsp_filepath(const std::string &filepath) {
  std::string lower = filepath;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return ends_with(lower, ".js") || ends_with(lower, ".jsx") ||
         ends_with(lower, ".mjs") || ends_with(lower, ".cjs") ||
         ends_with(lower, ".ts") || ends_with(lower, ".tsx") ||
         ends_with(lower, ".mts") || ends_with(lower, ".cts");
}

void append_html_builtin_completions(std::vector<LSPCompletionItem> &items) {
  auto add = [&](const std::string &label, const std::string &insert, const std::string &detail, int kind = 10) {
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

void Editor::poll_lsp_clients() {
  const long long current_time = now_ms();
  std::vector<std::string> ready_changes;
  ready_changes.reserve(lsp_pending_changes.size());
  for (const auto &entry : lsp_pending_changes) {
    if (entry.second <= current_time) {
      ready_changes.push_back(entry.first);
    }
  }

  for (const auto &filepath : ready_changes) {
    lsp_pending_changes.erase(filepath);
    LSPClient *client = ensure_lsp_for_file(filepath);
    if (!client) {
      continue;
    }

    for (const auto &buf : buffers) {
      if (buf.filepath == filepath) {
        client->did_change(filepath, get_buffer_text(buf));
        break;
      }
    }
  }

  for (auto &client : lsp_clients) {
    const int stdout_fd = client ? client->get_stdout_fd() : -1;
    const int stderr_fd = client ? client->get_stderr_fd() : -1;
    if (client && client->poll()) {
      needs_redraw = true;
    }
    if (client && !client->is_running()) {
      if (stdout_fd >= 0) event_loop_.unwatch_fd(stdout_fd);
      if (stderr_fd >= 0) event_loop_.unwatch_fd(stderr_fd);
    }
    if (!client) {
      continue;
    }
    auto published = client->consume_published_diagnostics();
    for (auto &entry : published) {
      set_diagnostics(entry.first, entry.second);
    }

    auto completions = client->consume_completion_items();
    for (auto &entry : completions) {
      if (buffers.empty() || current_buffer < 0 ||
          current_buffer >= (int)buffers.size()) {
        continue;
      }

      auto &buf = get_buffer();
      if (!same_path(entry.first, buf.filepath)) {
        continue;
      }

      if (!lsp_completion_manual_request &&
          (buf.cursor.y != lsp_completion_anchor.y ||
           std::abs(buf.cursor.x - lsp_completion_anchor.x) > 4)) {
        continue;
      }

      lsp_completion_all_items = std::move(entry.second);

      if (is_html_filepath(entry.first)) {
        append_html_builtin_completions(lsp_completion_all_items);
      }

      lsp_completion_filepath = entry.first;
      bool visible = refresh_lsp_completion_filter();
      if (lsp_completion_manual_request && !visible) {
        set_message("No suggestions");
      }
      lsp_completion_manual_request = false;
      needs_redraw = true;
    }

    auto hovers = client->consume_hover_results();
    for (const auto &hover : hovers) {
      handle_lsp_hover_result(hover);
    }

    auto definitions = client->consume_definition_results();
    for (const auto &definition : definitions) {
      handle_lsp_definition_result(definition);
    }

    auto document_symbols = client->consume_document_symbol_results();
    for (const auto &symbols : document_symbols) {
      handle_document_symbols_result(symbols);
    }
  }
}

void Editor::poll_lsp_installs() {
  bool changed = false;
  for (auto &job : lsp_install_jobs) {
    if (!job.running) {
      continue;
    }
    IntegratedTerminal *term = get_integrated_terminal(job.terminal_index);
    if (!term) {
      job.running = false;
      job.failed = true;
      job.progress = "terminal closed";
      changed = true;
      continue;
    }

    for (const auto &line : term->get_recent_lines(80)) {
      LspInstall::Marker marker;
      if (!LspInstall::parse_marker(line, marker) || marker.server != job.server) {
        continue;
      }
      if (marker.phase == "start") {
        job.progress = job.removing ? "removing" : "downloading";
      } else if (marker.phase == "success" && marker.exit_code == 0) {
        job.progress = job.removing ? "removed" : "installed";
        job.running = false;
        job.succeeded = true;
        job.failed = false;
        set_message("LSP " + std::string(job.removing ? "remove OK: " : "install OK: ") +
                    job.server);
      } else if (marker.phase == "failed") {
        job.progress = marker.exit_code >= 0
                           ? "failed (exit " + std::to_string(marker.exit_code) + ")"
                           : "failed";
        job.running = false;
        job.succeeded = false;
        job.failed = true;
        set_message("LSP " + std::string(job.removing ? "remove failed: " : "install failed: ") +
                    job.server);
      }
      changed = true;
    }

    if (job.running && !term->is_active()) {
      job.running = false;
      job.succeeded = false;
      job.failed = true;
      job.progress = "terminal exited";
      changed = true;
    }
  }
  if (changed || show_lsp_manager_modal) {
    if (show_lsp_manager_modal) refresh_lsp_manager();
    needs_redraw = true;
  }
}

void Editor::watch_lsp_client_fds(LSPClient *client) {
  if (!client) {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else

  auto watch_read = [this](int fd) {
    if (fd < 0 || event_loop_.is_watching_fd(fd)) {
      return;
    }
    event_loop_.watch_fd(fd, true, false, [this, fd] {
      bool found = false;
      for (auto &client : lsp_clients) {
        if (!client) {
          continue;
        }
        if (client->get_stdout_fd() == fd || client->get_stderr_fd() == fd) {
          found = true;
          break;
        }
      }
      if (!found) {
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

void Editor::unwatch_lsp_client_fds(LSPClient *client) {
  if (!client) {
    return;
  }
#ifdef _WIN32
  (void)client;
  return;
#else
  if (client->get_stdout_fd() >= 0) {
    event_loop_.unwatch_fd(client->get_stdout_fd());
  }
  if (client->get_stderr_fd() >= 0) {
    event_loop_.unwatch_fd(client->get_stderr_fd());
  }
#endif
}

LSPClient *Editor::find_lsp_client(const std::string &language,
                                   const std::string &root_path) {
  for (auto &client : lsp_clients) {
    if (client && client->get_language() == language &&
        client->get_root_path() == root_path) {
      return client.get();
    }
  }
  return nullptr;
}

LSPClient *Editor::ensure_lsp_for_file(const std::string &filepath) {
  if (filepath.empty()) {
    return nullptr;
  }

  std::string language = detect_lsp_language(filepath);
  if (language.empty()) {
    return nullptr;
  }
  if (lsp_disabled_servers.count(language)) {
    return nullptr;
  }

  std::string root = find_workspace_root(filepath, language);
  if (LSPClient *existing = find_lsp_client(language, root)) {
    if (!existing->is_running()) {
      unwatch_lsp_client_fds(existing);
      existing->restart();
      watch_lsp_client_fds(existing);
    }
    return existing;
  }

  std::vector<std::string> command = command_for_language(language);
  if (command.empty()) {
    return nullptr;
  }

  auto client = std::make_unique<LSPClient>(language, root, command);
  if (!client->start()) {
    set_message("LSP start failed for " + language + ": " + client->get_last_error());
    return nullptr;
  }

  lsp_clients.push_back(std::move(client));
  watch_lsp_client_fds(lsp_clients.back().get());
  return lsp_clients.back().get();
}

std::string Editor::get_buffer_text(const FileBuffer &buf) const {
  if (buf.is_lazy()) {
    return "";
  }
  size_t total_size = buf.lines.empty() ? 0 : buf.lines.size() - 1;
  for (const auto &line : buf.lines) {
    total_size += line.size();
  }

  std::string text;
  text.reserve(total_size);
  for (size_t i = 0; i < buf.line_count(); i++) {
    if (i > 0) {
      text.push_back('\n');
    }
    text.append(buf.line(i));
  }
  return text;
}

void Editor::notify_lsp_open(const std::string &filepath) {
  if (filepath.empty()) {
    return;
  }
  set_diagnostics(filepath, {});
  LSPClient *client = ensure_lsp_for_file(filepath);
  if (!client) {
    return;
  }

  for (const auto &buf : buffers) {
    if (buf.filepath == filepath) {
      if (buf.is_lazy())
        return;
      client->did_open(filepath, language_id_for(client->get_language(), filepath),
                       get_buffer_text(buf));
      break;
    }
  }
}

void Editor::notify_lsp_change(const std::string &filepath) {
  if (filepath.empty()) {
    return;
  }
  lsp_pending_changes[filepath] = now_ms() + lsp_change_debounce_ms;
}

void Editor::notify_lsp_save(const std::string &filepath) {
  if (filepath.empty()) {
    return;
  }
  lsp_pending_changes.erase(filepath);
  LSPClient *client = ensure_lsp_for_file(filepath);
  if (!client) {
    return;
  }

  for (const auto &buf : buffers) {
    if (buf.filepath == filepath) {
      client->did_save(filepath, get_buffer_text(buf));
      break;
    }
  }
}

void Editor::notify_lsp_close(const std::string &filepath) {
  if (filepath.empty()) {
    return;
  }
  lsp_pending_changes.erase(filepath);
  const std::string language = detect_lsp_language(filepath);
  if (language.empty()) {
    return;
  }
  const std::string root = find_workspace_root(filepath, language);
  if (LSPClient *client = find_lsp_client(language, root)) {
    client->did_close(filepath);
  }
}

void Editor::stop_all_lsp_clients() {
  int stopped = 0;
  lsp_pending_changes.clear();
  for (auto &buf : buffers) {
    if (!buf.filepath.empty()) {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  for (auto &client : lsp_clients) {
    if (client) {
      unwatch_lsp_client_fds(client.get());
    }
    if (client && client->is_running()) {
      client->stop();
      stopped++;
    }
  }
  lsp_clients.clear();
  set_message("LSP stopped: " + std::to_string(stopped) + " client(s)");
}

void Editor::restart_all_lsp_clients() {
  lsp_pending_changes.clear();
  for (auto &buf : buffers) {
    if (!buf.filepath.empty()) {
      buf.diagnostics.clear();
    }
  }
  invalidate_sidebar_diagnostics_cache();
  int restarted = 0;
  for (auto &client : lsp_clients) {
    if (!client) {
      continue;
    }
    unwatch_lsp_client_fds(client.get());
    if (client->restart()) {
      watch_lsp_client_fds(client.get());
      restarted++;
    }
  }
  for (const auto &buf : buffers) {
    if (!buf.filepath.empty() && !buf.is_lazy()) {
      notify_lsp_open(buf.filepath);
    }
  }
  set_message("LSP restarted: " + std::to_string(restarted) + " client(s)");
}

void Editor::show_lsp_manager() {
  close_context_menu();
  hide_popup();
  show_lsp_manager_modal = true;
  lsp_manager_selected = 0;
  lsp_manager_scroll = 0;
  lsp_manager_action_selected = 0;
  refresh_lsp_manager();
  needs_redraw = true;
}

void Editor::refresh_lsp_manager() {
  lsp_manager_rows.clear();
  for (const auto &spec : lsp_server_specs()) {
    LspManagerRow row;
    row.server = spec.id;
    row.label = spec.label;
    row.detail = spec.detail;
    row.installed = is_lsp_server_installed(row.server);
    row.enabled = !lsp_disabled_servers.count(row.server);
    row.managed = spec.managed;
    for (const auto &job : lsp_install_jobs) {
      if (job.server == row.server && job.running) {
        row.busy = true;
        row.detail = job.progress;
      }
    }
    int active = 0;
    for (const auto &client : lsp_clients) {
      if (client && client->get_language() == row.server && client->is_running()) {
        active++;
      }
    }
    if (active > 0 && !row.busy) {
      row.detail += " - " + std::to_string(active) + " active";
    }
    lsp_manager_rows.push_back(std::move(row));
  }
  lsp_manager_selected = std::clamp(
      lsp_manager_selected, 0, std::max(0, (int)lsp_manager_rows.size() - 1));
}

void Editor::set_lsp_server_enabled(const std::string &server, bool enabled) {
  if (enabled) {
    lsp_disabled_servers.erase(server);
    if (!buffers.empty() && current_buffer >= 0 &&
        current_buffer < (int)buffers.size() &&
        detect_lsp_language(get_buffer().filepath) == server) {
      notify_lsp_open(get_buffer().filepath);
    }
  } else {
    lsp_disabled_servers.insert(server);
    for (auto &client : lsp_clients) {
      if (client && client->get_language() == server) {
        unwatch_lsp_client_fds(client.get());
        client->stop();
      }
    }
    for (auto &buf : buffers) {
      if (detect_lsp_language(buf.filepath) == server) {
        buf.diagnostics.clear();
        lsp_pending_changes.erase(buf.filepath);
      }
    }
    invalidate_sidebar_diagnostics_cache();
  }
  refresh_lsp_manager();
  save_workspace_session();
  needs_redraw = true;
}

void Editor::activate_lsp_manager_action(const std::string &server,
                                         const std::string &action) {
  if (action == "enable") {
    set_lsp_server_enabled(server, true);
  } else if (action == "disable") {
    set_lsp_server_enabled(server, false);
  } else if (action == "install" || action == "update") {
    install_lsp_server(server);
  } else if (action == "remove") {
    remove_lsp_server(server);
  } else if (action == "terminal") {
    for (const auto &job : lsp_install_jobs) {
      if (job.server == server && job.running && job.terminal_index >= 0) {
        show_integrated_terminal = true;
        activate_integrated_terminal(job.terminal_index, false);
        break;
      }
    }
  }
  refresh_lsp_manager();
  needs_redraw = true;
}

bool Editor::handle_lsp_manager_input(int ch) {
  if (!show_lsp_manager_modal) return false;
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    show_lsp_manager_modal = false;
    needs_redraw = true;
    return true;
  }
  if (ch == 1008 || ch == 'k' || ch == 'K') {
    lsp_manager_selected = std::max(0, lsp_manager_selected - 1);
    lsp_manager_action_selected = 0;
  } else if (ch == 1009 || ch == 'j' || ch == 'J') {
    lsp_manager_selected = std::min(
        std::max(0, (int)lsp_manager_rows.size() - 1), lsp_manager_selected + 1);
    lsp_manager_action_selected = 0;
  } else if (ch == '\t') {
    lsp_manager_action_selected = (lsp_manager_action_selected + 1) % 3;
  } else if (lsp_manager_rows.empty()) {
    return true;
  } else {
    const auto &row = lsp_manager_rows[lsp_manager_selected];
    if (ch == 'e' || ch == 'E') {
      activate_lsp_manager_action(row.server, row.enabled ? "disable" : "enable");
      return true;
    }
    if (ch == '\n' || ch == 13) {
      if (row.busy) {
        activate_lsp_manager_action(row.server, "terminal");
      } else if (!row.enabled) {
        activate_lsp_manager_action(row.server, "enable");
      } else if (lsp_manager_action_selected == 1 && row.installed && row.managed) {
        activate_lsp_manager_action(row.server, "remove");
      } else if (lsp_manager_action_selected == 2 && row.installed) {
        activate_lsp_manager_action(row.server, "disable");
      } else if (row.managed) {
        activate_lsp_manager_action(row.server, row.installed ? "update" : "install");
      } else {
        set_message(row.label + " requires your package manager");
      }
      return true;
    }
  }
  needs_redraw = true;
  return true;
}

bool Editor::handle_lsp_manager_mouse(int x, int y, bool is_click,
                                      bool is_scroll_up, bool is_scroll_down) {
  if (!show_lsp_manager_modal) return false;
  if (is_scroll_up || is_scroll_down) {
    lsp_manager_scroll = std::max(0, lsp_manager_scroll +
                                      (is_scroll_down ? 3 : -3));
    needs_redraw = true;
    return true;
  }
  if (!is_click) return true;

  const bool inside = x >= lsp_manager_x && x < lsp_manager_x + lsp_manager_w &&
                      y >= lsp_manager_y && y < lsp_manager_y + lsp_manager_h;
  if (!inside) {
    show_lsp_manager_modal = false;
    needs_redraw = true;
    return true;
  }
  const int index = lsp_manager_scroll + (y - lsp_manager_y - 1);
  if (index < 0 || index >= (int)lsp_manager_rows.size()) return true;
  lsp_manager_selected = index;
  const auto &row = lsp_manager_rows[index];
  for (const auto &button : lsp_manager_buttons) {
    if (button.server != row.server) continue;
    const bool hit = x >= button.x && x < button.x + button.w &&
                     y >= button.y && y < button.y + button.h;
    if (hit) {
      activate_lsp_manager_action(button.server, button.action);
      return true;
    }
  }
  needs_redraw = true;
  return true;
}

bool Editor::install_lsp_server(const std::string &name) {
  const std::string server = normalize_lsp_server_name(name);
  if (server.empty()) {
    set_message("Unknown LSP server: " + name +
                " (use python|typescript|javascript|jsx|tsx|cpp|rust|go|lua|bash|html)");
    return false;
  }

  auto active_install = std::find_if(
      lsp_install_jobs.begin(), lsp_install_jobs.end(),
      [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (active_install != lsp_install_jobs.end()) {
    if (active_install->terminal_index >= 0) {
      activate_integrated_terminal(active_install->terminal_index, false);
      show_integrated_terminal = true;
    }
    set_message("LSP install already running: " + server);
    needs_redraw = true;
    return true;
  }

  lsp_install_jobs.erase(
      std::remove_if(lsp_install_jobs.begin(), lsp_install_jobs.end(),
                     [&](const LspInstallJob &job) {
                       return job.server == server && !job.running;
                     }),
      lsp_install_jobs.end());

  if (server == "cpp") {
    set_message("Install clangd using your OS package manager");
    return false;
  } else if (server == "rust") {
    set_message("Install rust-analyzer using install.sh or your OS package manager");
    return false;
  } else if (server == "go") {
    set_message("Install gopls using install.sh or `go install ...`");
    return false;
  } else if (server == "lua") {
    set_message("Install lua-language-server using install.sh or your OS package manager");
    return false;
  }

  LspInstall::Command install = LspInstall::command_for_server(server);
  if (!install.supported) {
    set_message("LSP install failed: " + server);
    return false;
  }

  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspinstall:" + server);
  if (integrated_terminals.size() == terminal_count) {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active()) {
    set_message("Failed to open LSP install terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);

  auto existing = std::find_if(
      lsp_install_jobs.begin(), lsp_install_jobs.end(),
      [&](const LspInstallJob &job) { return job.server == server && job.running; });
  if (existing != lsp_install_jobs.end()) {
    existing->terminal_index = terminal_index;
    existing->progress = "starting";
    existing->failed = false;
    existing->succeeded = false;
  } else {
    LspInstallJob job;
    job.server = server;
    job.removing = false;
    job.terminal_index = terminal_index;
    job.progress = "starting";
    lsp_install_jobs.push_back(std::move(job));
  }

  term->send_text(LspInstall::terminal_command(install) + "\r");
  set_message(install.message + " (terminal " +
              std::to_string(terminal_index + 1) + ")");
  needs_redraw = true;
  return true;
}

bool Editor::remove_lsp_server(const std::string &name) {
  const std::string server = normalize_lsp_server_name(name);
  if (server.empty()) {
    set_message("Unknown LSP server: " + name +
                " (use python|typescript|javascript|jsx|tsx|cpp|rust|go|lua|bash|html)");
    return false;
  }

  LspInstall::Command remove = LspInstall::remove_command_for_server(server);
  if (!remove.supported) {
    set_message("Remove " + server + " with your package manager");
    return false;
  }

  set_lsp_server_enabled(server, false);
  const size_t terminal_count = integrated_terminals.size();
  create_integrated_terminal("lspremove:" + server);
  if (integrated_terminals.size() == terminal_count) {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  const int terminal_index = current_integrated_terminal;
  IntegratedTerminal *term = get_integrated_terminal(terminal_index);
  if (!term || !term->is_active()) {
    set_message("Failed to open LSP remove terminal");
    return false;
  }
  activate_integrated_terminal(terminal_index, false);
  LspInstallJob job;
  job.server = server;
  job.removing = true;
  job.terminal_index = terminal_index;
  job.progress = "starting";
  lsp_install_jobs.push_back(std::move(job));
  term->send_text(LspInstall::terminal_command(remove) + "\r");
  set_message(remove.message + " (terminal " + std::to_string(terminal_index + 1) + ")");
  refresh_lsp_manager();
  needs_redraw = true;
  return true;
}

void Editor::hide_lsp_completion() {
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_replace_start = {0, 0};
  lsp_completion_items.clear();
  lsp_completion_all_items.clear();
  lsp_completion_filepath.clear();
  lsp_completion_prefix.clear();
}

bool Editor::refresh_lsp_completion_filter() {
  if (lsp_completion_all_items.empty() || buffers.empty() || panes.empty()) {
    lsp_completion_visible = false;
    lsp_completion_items.clear();
    return false;
  }

  auto &buf = get_buffer();
  if (!lsp_completion_filepath.empty() &&
      !same_path(lsp_completion_filepath, buf.filepath)) {
    hide_lsp_completion();
    return false;
  }
  if (buf.cursor.y != lsp_completion_replace_start.y ||
      buf.cursor.x < lsp_completion_replace_start.x) {
    hide_lsp_completion();
    return false;
  }

  std::string query = completion_prefix_from(buf, lsp_completion_replace_start);
  std::string selected_label;
  if (lsp_completion_selected >= 0 &&
      lsp_completion_selected < (int)lsp_completion_items.size()) {
    selected_label = lsp_completion_items[lsp_completion_selected].label;
  }

  std::vector<std::pair<int, LSPCompletionItem>> ranked;
  ranked.reserve(lsp_completion_all_items.size());
  for (const auto &item : lsp_completion_all_items) {
    int score = completion_match_score(query, item);
    if (query.empty() || score > 0) {
      ranked.push_back({score, item});
    }
  }

  std::stable_sort(ranked.begin(), ranked.end(),
                   [](const auto &a, const auto &b) {
                     if (a.first != b.first) {
                       return a.first > b.first;
                     }
                     const std::string &as =
                         a.second.sort_text.empty() ? a.second.label
                                                    : a.second.sort_text;
                     const std::string &bs =
                         b.second.sort_text.empty() ? b.second.label
                                                    : b.second.sort_text;
                     return as < bs;
                   });

  lsp_completion_items.clear();
  const int max_items = 200;
  for (int i = 0; i < (int)ranked.size() && i < max_items; i++) {
    lsp_completion_items.push_back(std::move(ranked[i].second));
  }

  lsp_completion_prefix = query;
  lsp_completion_selected = 0;
  for (int i = 0; i < (int)lsp_completion_items.size(); i++) {
    if (!selected_label.empty() && lsp_completion_items[i].label == selected_label) {
      lsp_completion_selected = i;
      break;
    }
    if (selected_label.empty() && lsp_completion_items[i].preselect) {
      lsp_completion_selected = i;
      break;
    }
  }
  lsp_completion_visible = !lsp_completion_items.empty();
  return lsp_completion_visible;
}

void Editor::request_lsp_completion(bool manual, char trigger_character) {
  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    return;
  }
  if (buf.filepath.empty()) {
    if (manual) {
      set_message("Save file first to use LSP completion");
    }
    return;
  }

  if (!manual) {
    if (!(std::isalnum((unsigned char)trigger_character) ||
          trigger_character == '_' || trigger_character == '.' ||
          trigger_character == ':' || trigger_character == '>' ||
          trigger_character == '<' || trigger_character == '/')) {
      return;
    }

    int prefix_len = 0;
    int i = std::min(buf.cursor.x, (int)buf.line(buf.cursor.y).size());
    while (i > 0 && is_identifier_char(buf.line(buf.cursor.y)[i - 1])) {
      prefix_len++;
      i--;
    }
    bool html_file = is_html_filepath(buf.filepath);
    bool script_file = is_script_lsp_filepath(buf.filepath);
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':' ||
                               trigger_character == '>' || trigger_character == '<' ||
                               trigger_character == '/';
    int min_prefix = (html_file || script_file) ? 1 : 2;
    if (!punctuation_trigger && prefix_len < min_prefix) {
      return;
    }
  }

  Cursor replace_start = current_completion_start(buf);
  bool has_builtin_html = false;

  if (is_html_filepath(buf.filepath)) {
    lsp_completion_all_items.clear();
    append_html_builtin_completions(lsp_completion_all_items);
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
    has_builtin_html = refresh_lsp_completion_filter();
    if (has_builtin_html) {
      needs_redraw = true;
    }
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client) {
    if (manual) {
      set_message("No LSP server for this file");
    }
    return;
  }

  // Completion must use current text state, not debounced change state.
  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));

  char trigger = '\0';
  if (trigger_character == '.' || trigger_character == ':' ||
      trigger_character == '>' || trigger_character == '<' ||
      trigger_character == '/') {
    trigger = trigger_character;
  }

  if (!client->request_completion(buf.filepath, buf.cursor.y, buf.cursor.x,
                                  trigger)) {
    if (manual) {
      set_message("LSP completion request failed");
    }
    return;
  }

  if (!has_builtin_html) {
    lsp_completion_anchor = buf.cursor;
    lsp_completion_replace_start = replace_start;
    lsp_completion_filepath = buf.filepath;
    lsp_completion_prefix = completion_prefix_from(buf, replace_start);
    lsp_completion_manual_request = manual;
  }
}

void Editor::request_lsp_hover() {
  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    return;
  }
  if (buf.filepath.empty()) {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client) {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, buf.cursor.y, buf.cursor.x)) {
    set_message("LSP hover request failed");
    return;
  }
  set_message("LSP hover requested");
}

void Editor::request_lsp_hover_at(int pane_index, int buffer_id,
                                  const Cursor &pos, int token_start,
                                  int token_end, int screen_x, int screen_y) {
  if (buffer_id < 0 || buffer_id >= (int)buffers.size()) {
    cancel_lsp_mouse_hover();
    return;
  }
  const auto &buf = buffers[buffer_id];
  if (buf.is_lazy() || buf.filepath.empty()) {
    cancel_lsp_mouse_hover();
    return;
  }
  if (pos.y < 0 || pos.y >= (int)buf.line_count() || token_start < 0 ||
      token_end <= token_start) {
    cancel_lsp_mouse_hover();
    return;
  }

  if (lsp_mouse_hover_pending && lsp_mouse_hover_buffer == buffer_id &&
      lsp_mouse_hover_line == pos.y &&
      lsp_mouse_hover_token_start == token_start &&
      lsp_mouse_hover_token_end == token_end) {
    lsp_mouse_hover_screen_x = screen_x;
    lsp_mouse_hover_screen_y = screen_y;
    return;
  }
  if (lsp_mouse_hover_visible && lsp_mouse_hover_buffer == buffer_id &&
      lsp_mouse_hover_line == pos.y &&
      lsp_mouse_hover_token_start == token_start &&
      lsp_mouse_hover_token_end == token_end) {
    return;
  }

  if (lsp_mouse_hover_visible) {
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

void Editor::cancel_lsp_mouse_hover(bool hide_popup_now) {
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
  if (hide_popup_now && lsp_mouse_hover_visible) {
    hide_popup();
    lsp_mouse_hover_visible = false;
  }
}

void Editor::maybe_fire_lsp_mouse_hover() {
  if (!lsp_mouse_hover_pending || now_ms() < lsp_mouse_hover_deadline_ms) {
    return;
  }
  if (show_context_menu || show_command_palette || show_search ||
      telescope.is_active() || mouse_selecting || mouse_drag_started) {
    return;
  }
  if (lsp_mouse_hover_buffer < 0 ||
      lsp_mouse_hover_buffer >= (int)buffers.size()) {
    cancel_lsp_mouse_hover();
    return;
  }

  auto &buf = buffers[lsp_mouse_hover_buffer];
  if (buf.is_lazy() || !same_path(buf.filepath, lsp_mouse_hover_filepath)) {
    cancel_lsp_mouse_hover();
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client) {
    cancel_lsp_mouse_hover(false);
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_hover(buf.filepath, lsp_mouse_hover_line,
                             lsp_mouse_hover_col)) {
    cancel_lsp_mouse_hover(false);
    return;
  }
  lsp_mouse_hover_pending = false;
}

void Editor::request_lsp_definition() {
  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    return;
  }
  if (buf.filepath.empty()) {
    set_message("Save file first to use LSP");
    return;
  }

  LSPClient *client = ensure_lsp_for_file(buf.filepath);
  if (!client) {
    set_message("No LSP server for this file");
    return;
  }

  lsp_pending_changes.erase(buf.filepath);
  client->did_change(buf.filepath, get_buffer_text(buf));
  if (!client->request_definition(buf.filepath, buf.cursor.y, buf.cursor.x)) {
    set_message("LSP definition request failed");
    return;
  }
  set_message("LSP definition requested");
}

void Editor::handle_lsp_hover_result(const LSPHoverResult &hover) {
  if (buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return;
  }
  if (lsp_mouse_hover_buffer >= 0 &&
      same_path(hover.origin_filepath, lsp_mouse_hover_filepath) &&
      hover.origin_line == lsp_mouse_hover_line &&
      hover.origin_character == lsp_mouse_hover_col) {
    if (hover.contents.empty()) {
      lsp_mouse_hover_visible = false;
      return;
    }
    std::string text = compact_lsp_popup_text(hover.contents, 14, 96);
    int popup_x = lsp_mouse_hover_screen_x + 2;
    int popup_y = lsp_mouse_hover_screen_y;
    if (ui) {
      auto [popup_w, popup_h] = lsp_popup_size(text);
      auto [placed_x, placed_y] =
          place_lsp_popup(popup_x, popup_y, popup_w, popup_h,
                          ui->get_render_width(), ui->get_height(),
                          status_height);
      popup_x = placed_x;
      popup_y = placed_y;
    } else {
      popup_y += 1;
    }
    show_hover_popup(text, popup_x, popup_y);
    lsp_mouse_hover_visible = true;
    return;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, hover.origin_filepath) ||
      buf.cursor.y != hover.origin_line ||
      buf.cursor.x != hover.origin_character) {
    return;
  }
  if (hover.contents.empty()) {
    set_message("No hover information");
    return;
  }

  const SplitPane &pane = get_pane();
  constexpr int line_num_width = 8;
  int row = hover.origin_line - buf.scroll_offset;
  int max_row = std::max(0, pane.h - tab_height - 1);
  int anchor_y = pane.y + tab_height + std::clamp(row, 0, max_row);
  int popup_x = pane.x + 1 + line_num_width +
                std::max(0, hover.origin_character - buf.scroll_x) + 2;
  std::string text = compact_lsp_popup_text(hover.contents, 14, 96);
  if (ui) {
    auto [popup_w, popup_h] = lsp_popup_size(text);
    auto [placed_x, placed_y] =
        place_lsp_popup(popup_x, anchor_y, popup_w, popup_h,
                        ui->get_render_width(), ui->get_height(),
                        status_height);
    popup_x = placed_x;
    anchor_y = placed_y;
  } else {
    anchor_y += 1;
  }
  show_hover_popup(text, popup_x, anchor_y);
}

void Editor::handle_lsp_definition_result(
    const LSPDefinitionResult &definition) {
  if (buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return;
  }
  auto &buf = get_buffer();
  if (!same_path(buf.filepath, definition.origin_filepath) ||
      buf.cursor.y != definition.origin_line ||
      buf.cursor.x != definition.origin_character) {
    return;
  }
  if (definition.locations.empty()) {
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
  if (lsp_jump_stack.size() > 50) {
    lsp_jump_stack.erase(lsp_jump_stack.begin());
  }

  lsp_definition_pending_location = definition.locations.front();
  lsp_definition_jump_pending = true;
  const bool same_file =
      same_path(buf.filepath, lsp_definition_pending_location.filepath);
  open_file(lsp_definition_pending_location.filepath, !same_file);
  apply_pending_lsp_definition_jump();
}

bool Editor::apply_pending_lsp_definition_jump() {
  if (!lsp_definition_jump_pending || buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return false;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, lsp_definition_pending_location.filepath)) {
    return false;
  }

  buf.cursor.y =
      std::clamp(lsp_definition_pending_location.line, 0,
                 std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(lsp_definition_pending_location.character, 0,
                 (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  clear_selection();
  ensure_cursor_visible();
  lsp_definition_jump_pending = false;
  set_message("Definition: " + get_filename(buf.filepath) + ":" +
              std::to_string(buf.cursor.y + 1));
  needs_redraw = true;
  return true;
}

bool Editor::apply_pending_lsp_back_jump() {
  if (!lsp_back_jump_pending || buffers.empty() || current_buffer < 0 ||
      current_buffer >= (int)buffers.size()) {
    return false;
  }

  auto &buf = get_buffer();
  if (!same_path(buf.filepath, lsp_back_pending_location.filepath)) {
    return false;
  }

  buf.cursor.y =
      std::clamp(lsp_back_pending_location.cursor.y, 0,
                 std::max(0, (int)buf.line_count() - 1));
  buf.cursor.x =
      std::clamp(lsp_back_pending_location.cursor.x, 0,
                 (int)buf.line(buf.cursor.y).size());
  buf.preferred_x = buf.cursor.x;
  buf.scroll_offset = std::max(0, lsp_back_pending_location.scroll_offset);
  buf.scroll_x = std::max(0, lsp_back_pending_location.scroll_x);
  clear_selection();
  ensure_cursor_visible();
  lsp_back_jump_pending = false;
  set_message("Returned: " + get_filename(buf.filepath) + ":" +
              std::to_string(buf.cursor.y + 1));
  needs_redraw = true;
  return true;
}

void Editor::return_from_lsp_definition() {
  if (lsp_jump_stack.empty()) {
    set_message("No LSP jump to return to");
    return;
  }

  lsp_back_pending_location = lsp_jump_stack.back();
  lsp_jump_stack.pop_back();
  lsp_back_jump_pending = true;
  open_file(lsp_back_pending_location.filepath, lsp_back_pending_location.preview);
  apply_pending_lsp_back_jump();
}

bool Editor::apply_selected_lsp_completion() {
  if (!lsp_completion_visible || lsp_completion_items.empty()) {
    return false;
  }

  auto &buf = get_buffer();
  if (buf.is_lazy()) {
    buf.materialize();
  }
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size()) {
    hide_lsp_completion();
    return false;
  }

  int idx = std::clamp(lsp_completion_selected, 0,
                       (int)lsp_completion_items.size() - 1);
  const auto &item = lsp_completion_items[idx];
  std::string text = item.insert_text.empty() ? item.label : item.insert_text;

  int cursor_offset = -1;
  size_t marker_pos = text.find('|');
  if (marker_pos != std::string::npos) {
    cursor_offset = (int)marker_pos;
    text.erase(marker_pos, 1);
  }

  if (item.insert_text_format == 2) {
    SnippetExpansion expansion = expand_lsp_snippet(text);
    text = std::move(expansion.text);
    cursor_offset = expansion.cursor_offset;
  }
  if (text.empty()) {
    hide_lsp_completion();
    return false;
  }

  save_state();

  std::string &line = buf.line_mut(buf.cursor.y);
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int start = cursor;
  int end = cursor;

  if (item.has_text_edit_range && item.edit_start_line == buf.cursor.y &&
      item.edit_end_line == buf.cursor.y) {
    start = std::clamp(item.edit_start_char, 0, (int)line.size());
    end = std::clamp(item.edit_end_char, start, (int)line.size());
  } else if (lsp_completion_replace_start.y == buf.cursor.y) {
    start = std::clamp(lsp_completion_replace_start.x, 0, cursor);
  } else {
    while (start > 0 && is_identifier_char(line[start - 1])) {
      start--;
    }
  }

  if (start < end) {
    line.erase(start, end - start);
    cursor = start;
  } else if (start < cursor) {
    line.erase(start, cursor - start);
    cursor = start;
  }
  std::string tail = line.substr(cursor);
  line.erase(cursor);

  size_t segment_start = 0;
  std::vector<std::string> inserted_lines;
  while (true) {
    size_t nl = text.find('\n', segment_start);
    if (nl == std::string::npos) {
      inserted_lines.push_back(text.substr(segment_start));
      break;
    }
    inserted_lines.push_back(text.substr(segment_start, nl - segment_start));
    segment_start = nl + 1;
  }
  if (inserted_lines.empty()) {
    inserted_lines.push_back("");
  }

  line.insert(cursor, inserted_lines.front());
  int insert_line = buf.cursor.y;
  for (size_t i = 1; i < inserted_lines.size(); i++) {
    buf.lines.insert(buf.lines.begin() + insert_line + (int)i,
                     inserted_lines[i]);
  }
  buf.line_mut(insert_line + (int)inserted_lines.size() - 1) += tail;

  int target_offset = cursor_offset >= 0 ? cursor_offset : (int)text.size();
  int target_line_delta = 0;
  int target_col = cursor;
  for (int i = 0; i < target_offset && i < (int)text.size(); i++) {
    if (text[i] == '\n') {
      target_line_delta++;
      target_col = 0;
    } else {
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

  if (python_api) {
    python_api->on_buffer_change(buf.filepath, "");
  }
  notify_lsp_change(buf.filepath);

  hide_lsp_completion();
  return true;
}
