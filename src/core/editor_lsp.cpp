#include "editor.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
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
  return language;
}

long long now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
      .count();
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
