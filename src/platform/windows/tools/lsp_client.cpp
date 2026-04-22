#include "lsp_client.h"

LSPClient::LSPClient(const std::string &language_name,
                     const std::string &workspace_root,
                     const std::vector<std::string> &argv)
    : language(language_name), root_path(workspace_root), command(argv), stdin_fd(-1),
      stdout_fd(-1), stderr_fd(-1), child_pid(-1), running(false),
      initialized(false), next_request_id(1) {
  last_error = "LSP process backend is not available on Windows in this build";
}

LSPClient::~LSPClient() { stop(); }

bool LSPClient::send_message(const std::string &) { return false; }

std::string LSPClient::json_escape(const std::string &value) const { return value; }

void LSPClient::append_log_line(const std::string &, const std::string &) {}

void LSPClient::handle_stdout_data(const std::string &) {}

void LSPClient::handle_stderr_data(const std::string &) {}

bool LSPClient::start() {
  running = false;
  initialized = false;
  last_error = "LSP is not supported on Windows yet (planned)";
  return false;
}

void LSPClient::stop() {
  running = false;
  initialized = false;
}

bool LSPClient::restart() {
  stop();
  return start();
}

bool LSPClient::poll() { return false; }

bool LSPClient::did_open(const std::string &, const std::string &,
                         const std::string &) {
  return false;
}

bool LSPClient::did_change(const std::string &, const std::string &) {
  return false;
}

bool LSPClient::did_save(const std::string &, const std::string &) {
  return false;
}

bool LSPClient::request_completion(const std::string &, int, int, char) {
  return false;
}

std::vector<std::pair<std::string, std::vector<Diagnostic>>>
LSPClient::consume_published_diagnostics() {
  std::vector<std::pair<std::string, std::vector<Diagnostic>>> out;
  out.swap(pending_diagnostics);
  return out;
}

std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>>
LSPClient::consume_completion_items() {
  std::vector<std::pair<std::string, std::vector<LSPCompletionItem>>> out;
  out.swap(pending_completions);
  return out;
}

std::string LSPClient::describe() const {
  return language + " (Windows stub: unavailable)";
}
