#include "editor.h"
#include "python_api.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
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

bool is_identifier_char(char c) {
  unsigned char uc = (unsigned char)c;
  return std::isalnum(uc) || c == '_';
}

std::string current_completion_prefix(const FileBuffer &buf) {
  if (buf.cursor.y < 0 || buf.cursor.y >= (int)buf.lines.size()) {
    return "";
  }
  const std::string &line = buf.lines[buf.cursor.y];
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int start = cursor;
  while (start > 0 && is_identifier_char(line[start - 1])) {
    start--;
  }
  if (start >= cursor) {
    return "";
  }
  return line.substr((size_t)start, (size_t)(cursor - start));
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
  if (query.empty()) {
    return 1;
  }

  const std::string q = to_lower_copy(query);
  const std::string label = to_lower_copy(item.label);
  const std::string filter = to_lower_copy(item.filter_text.empty()
                                               ? item.label
                                               : item.filter_text);
  const std::string insert = to_lower_copy(item.insert_text);

  if (label == q || filter == q || insert == q) {
    return 10000;
  }
  if (label.rfind(q, 0) == 0 || filter.rfind(q, 0) == 0 || insert.rfind(q, 0) == 0) {
    return 7000 - (int)label.size();
  }
  if (label.find(q) != std::string::npos || filter.find(q) != std::string::npos ||
      insert.find(q) != std::string::npos) {
    return 4000 - (int)label.find(q);
  }
  if (is_subsequence_case_insensitive(q, label) ||
      is_subsequence_case_insensitive(q, filter)) {
    return 1500;
  }
  return 0;
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

      std::string query = current_completion_prefix(buf);
      std::vector<std::pair<int, LSPCompletionItem>> ranked;
      ranked.reserve(entry.second.size());
      for (auto &item : entry.second) {
        int score = completion_match_score(query, item);
        if (query.empty() || score > 0) {
          ranked.push_back({score, std::move(item)});
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
      lsp_completion_filepath = entry.first;
      lsp_completion_selected = 0;
      lsp_completion_visible = !lsp_completion_items.empty();
      if (lsp_completion_manual_request && lsp_completion_items.empty()) {
        set_message("No suggestions");
      }
      lsp_completion_manual_request = false;
      needs_redraw = true;
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
  size_t total_size = buf.lines.empty() ? 0 : buf.lines.size() - 1;
  for (const auto &line : buf.lines) {
    total_size += line.size();
  }

  std::string text;
  text.reserve(total_size);
  for (size_t i = 0; i < buf.lines.size(); i++) {
    if (i > 0) {
      text.push_back('\n');
    }
    text.append(buf.lines[i]);
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

  int rc = 1;
  if (server == "python") {
    rc = std::system("python3 -m pip install --user -U python-lsp-server");
  } else if (server == "typescript") {
    rc = std::system("npm install -g typescript typescript-language-server");
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
    rc = std::system("npm install -g bash-language-server");
  }

  if (rc == 0) {
    set_message("LSP install OK: " + server);
    return true;
  }
  set_message("LSP install failed: " + server);
  return false;
}

bool Editor::remove_lsp_server(const std::string &name) {
  const std::string server = normalize_lsp_server_name(name);
  if (server.empty()) {
    set_message("Unknown LSP server: " + name +
                " (use python|typescript|cpp|rust|go|lua|bash)");
    return false;
  }

  int rc = 1;
  if (server == "python") {
    rc = std::system("python3 -m pip uninstall -y python-lsp-server");
  } else if (server == "typescript") {
    rc = std::system("npm uninstall -g typescript typescript-language-server");
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
    rc = std::system("npm uninstall -g bash-language-server");
  }

  if (rc == 0) {
    set_message("LSP remove OK: " + server);
    return true;
  }
  set_message("LSP remove failed: " + server);
  return false;
}

void Editor::hide_lsp_completion() {
  lsp_completion_visible = false;
  lsp_completion_manual_request = false;
  lsp_completion_selected = 0;
  lsp_completion_items.clear();
  lsp_completion_filepath.clear();
}

void Editor::request_lsp_completion(bool manual, char trigger_character) {
  auto &buf = get_buffer();
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
    int i = std::min(buf.cursor.x, (int)buf.lines[buf.cursor.y].size());
    while (i > 0 && is_identifier_char(buf.lines[buf.cursor.y][i - 1])) {
      prefix_len++;
      i--;
    }
    bool punctuation_trigger = trigger_character == '.' || trigger_character == ':' ||
                               trigger_character == '>';
    if (!punctuation_trigger && prefix_len < 2) {
      return;
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
  lsp_completion_filepath = buf.filepath;
  lsp_completion_manual_request = manual;
}

bool Editor::apply_selected_lsp_completion() {
  if (!lsp_completion_visible || lsp_completion_items.empty()) {
    return false;
  }

  auto &buf = get_buffer();
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

  std::string &line = buf.lines[buf.cursor.y];
  int cursor = std::clamp(buf.cursor.x, 0, (int)line.size());
  int start = cursor;
  int end = cursor;

  if (item.has_text_edit_range && item.edit_start_line == buf.cursor.y &&
      item.edit_end_line == buf.cursor.y) {
    start = std::clamp(item.edit_start_char, 0, (int)line.size());
    end = std::clamp(item.edit_end_char, start, (int)line.size());
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
