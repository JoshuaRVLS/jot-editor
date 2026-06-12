#include "editor.h"
#include "python_api.h"
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
struct LspCommandResult {
  int rc = 1;
};

std::string to_lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::string detect_lsp_language(const std::string &filepath) {
  std::string lower = filepath;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".py")
    return "python";
  if (lower.size() >= 3 &&
      (lower.substr(lower.size() - 3) == ".ts" ||
       lower.substr(lower.size() - 3) == ".js"))
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
  const char *home = getenv("HOME");
  if (language == "python") {
    if (home) {
      fs::path venv = fs::path(home) / ".config" / "jot" / "venv" / "bin" / "pylsp";
      if (fs::exists(venv)) {
        return {venv.string()};
      }
    }
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
  return {};
}

std::string language_id_for(const std::string &language, const std::string &filepath) {
  if (language == "typescript") {
    std::string lower = filepath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.size() >= 3 && lower.substr(lower.size() - 3) == ".js")
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

std::string snippet_to_plain_text(const std::string &snippet) {
  std::string out;
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
      continue;
    }
    if (std::isdigit((unsigned char)snippet[i + 1])) {
      while (i + 1 < snippet.size() &&
             std::isdigit((unsigned char)snippet[i + 1])) {
        i++;
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

      size_t colon = inner.find(':');
      if (colon != std::string::npos && colon + 1 < inner.size()) {
        out.append(inner.substr(colon + 1));
      }
      continue;
    }
  }

  return out;
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

std::pair<int, int> lsp_popup_size(const std::string &text) {
  int max_w = 0;
  int lines = 0;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    max_w = std::max(max_w, (int)line.length());
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
  std::string cmd = "command -v " + name + " >/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
}

std::string normalize_lsp_server_name(const std::string &raw) {
  std::string n = to_lower_copy(raw);
  if (n == "py" || n == "pylsp") {
    return "python";
  }
  if (n == "ts" || n == "js" || n == "javascript") {
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
  return "";
}

bool is_lsp_server_installed(const std::string &server) {
  if (server == "python") {
    const char *home = std::getenv("HOME");
    if (home) {
      fs::path local = fs::path(home) / ".config" / "jot" / "venv" / "bin" / "pylsp";
      if (fs::exists(local)) {
        return true;
      }
    }
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
  return false;
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
    if (client && client->poll()) {
      needs_redraw = true;
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
  }
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

  std::string root = find_workspace_root(filepath, language);
  if (LSPClient *existing = find_lsp_client(language, root)) {
    if (!existing->is_running()) {
      existing->restart();
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
    if (client && client->restart()) {
      restarted++;
    }
  }
  set_message("LSP restarted: " + std::to_string(restarted) + " client(s)");
}

void Editor::show_lsp_status() {
  if (lsp_clients.empty()) {
    set_message("LSP: no active clients");
    return;
  }

  std::string status = "LSP:";
  for (size_t i = 0; i < lsp_clients.size() && i < 3; i++) {
    status += " ";
    status += lsp_clients[i]->describe();
    if (!lsp_clients[i]->is_running()) {
      status += " [stopped]";
    }
    if (i + 1 < lsp_clients.size() && i < 2) {
      status += " |";
    }
  }
  set_message(status);
}

void Editor::show_lsp_manager() {
  std::vector<std::string> lines = {
      "LSP Manager (C++ builtin)",
      "",
      std::string("python      [") +
          (is_lsp_server_installed("python") ? "installed" : "missing") + "]",
      std::string("typescript  [") +
          (is_lsp_server_installed("typescript") ? "installed" : "missing") + "]",
      std::string("cpp         [") +
          (is_lsp_server_installed("cpp") ? "installed" : "missing") + "]",
      std::string("rust        [") +
          (is_lsp_server_installed("rust") ? "installed" : "missing") + "]",
      std::string("go          [") +
          (is_lsp_server_installed("go") ? "installed" : "missing") + "]",
      std::string("lua         [") +
          (is_lsp_server_installed("lua") ? "installed" : "missing") + "]",
      std::string("bash        [") +
          (is_lsp_server_installed("bash") ? "installed" : "missing") + "]",
      "",
      "Use:",
      ":lspinstall <python|typescript|cpp|rust|go|lua|bash>",
      ":lspremove <python|typescript|cpp|rust|go|lua|bash>",
      ":lspstart :lspstatus :lspstop :lsprestart"};
  show_popup([&lines]() {
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) {
      out += lines[i];
      if (i + 1 < lines.size()) {
        out.push_back('\n');
      }
    }
    return out;
  }(), 2, tab_height + 1);
}

bool Editor::install_lsp_server(const std::string &name) {
  const std::string server = normalize_lsp_server_name(name);
  if (server.empty()) {
    set_message("Unknown LSP server: " + name +
                " (use python|typescript|cpp|rust|go|lua|bash)");
    return false;
  }

  std::string command;
  if (server == "python") {
    command = "python3 -m pip install --user -U python-lsp-server";
  } else if (server == "typescript") {
    command = "npm install -g typescript typescript-language-server";
  } else if (server == "cpp") {
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
  } else if (server == "bash") {
    command = "npm install -g bash-language-server";
  }

  if (command.empty()) {
    set_message("LSP install failed: " + server);
    return false;
  }

  if (!task_queue_) {
    int rc = std::system(command.c_str());
    set_message(rc == 0 ? "LSP install OK: " + server
                        : "LSP install failed: " + server);
    return rc == 0;
  }

  set_message("LSP install started: " + server);
  needs_redraw = true;
  auto result = std::shared_ptr<LspCommandResult>(new LspCommandResult());
  task_queue_->submit(
      [command = std::move(command), result]() {
        result->rc = std::system(command.c_str());
      },
      [this, server, result]() {
        if (!running)
          return;
        set_message(result->rc == 0 ? "LSP install OK: " + server
                                    : "LSP install failed: " + server);
      });
  return true;
}

bool Editor::remove_lsp_server(const std::string &name) {
  const std::string server = normalize_lsp_server_name(name);
  if (server.empty()) {
    set_message("Unknown LSP server: " + name +
                " (use python|typescript|cpp|rust|go|lua|bash)");
    return false;
  }

  std::string command;
  if (server == "python") {
    command = "python3 -m pip uninstall -y python-lsp-server";
  } else if (server == "typescript") {
    command = "npm uninstall -g typescript typescript-language-server";
  } else if (server == "cpp") {
    set_message("Remove clangd using your OS package manager");
    return false;
  } else if (server == "rust") {
    set_message("Remove rust-analyzer using your OS package manager");
    return false;
  } else if (server == "go") {
    set_message("Remove gopls using your Go toolchain or package manager");
    return false;
  } else if (server == "lua") {
    set_message("Remove lua-language-server using your OS package manager");
    return false;
  } else if (server == "bash") {
    command = "npm uninstall -g bash-language-server";
  }

  if (command.empty()) {
    set_message("LSP remove failed: " + server);
    return false;
  }

  if (!task_queue_) {
    int rc = std::system(command.c_str());
    set_message(rc == 0 ? "LSP remove OK: " + server
                        : "LSP remove failed: " + server);
    return rc == 0;
  }

  set_message("LSP remove started: " + server);
  needs_redraw = true;
  auto result = std::shared_ptr<LspCommandResult>(new LspCommandResult());
  task_queue_->submit(
      [command = std::move(command), result]() {
        result->rc = std::system(command.c_str());
      },
      [this, server, result]() {
        if (!running)
          return;
        set_message(result->rc == 0 ? "LSP remove OK: " + server
                                    : "LSP remove failed: " + server);
      });
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
          trigger_character == ':' || trigger_character == '>')) {
      return;
    }

    int prefix_len = 0;
    int i = std::min(buf.cursor.x, (int)buf.line(buf.cursor.y).size());
    while (i > 0 && is_identifier_char(buf.line(buf.cursor.y)[i - 1])) {
      prefix_len++;
      i--;
    }
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':' ||
                               trigger_character == '>';
    if (!punctuation_trigger && prefix_len < 2) {
      return;
    }
  }

  Cursor replace_start = current_completion_start(buf);
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
      trigger_character == '>') {
    trigger = trigger_character;
  }

  if (!client->request_completion(buf.filepath, buf.cursor.y, buf.cursor.x,
                                  trigger)) {
    if (manual) {
      set_message("LSP completion request failed");
    }
    return;
  }

  lsp_completion_anchor = buf.cursor;
  lsp_completion_replace_start = replace_start;
  lsp_completion_filepath = buf.filepath;
  lsp_completion_prefix = completion_prefix_from(buf, replace_start);
  lsp_completion_manual_request = manual;
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
    show_popup(text, popup_x, popup_y);
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
  show_popup(text, popup_x, anchor_y);
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
  if (item.insert_text_format == 2) {
    text = snippet_to_plain_text(text);
  }
  size_t nl = text.find('\n');
  if (nl != std::string::npos) {
    text = text.substr(0, nl);
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
  line.insert(cursor, text);
  buf.cursor.x = cursor + (int)text.size();
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
