#ifndef LSP_CLIENT_H
#define LSP_CLIENT_H

#include "editor_features.h"
#include <map>
#include <utility>
#include <string>
#include <vector>

class LSPClient {
private:
  std::string language;
  std::string root_path;
  std::vector<std::string> command;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
  int child_pid;
  bool running;
  bool initialized;
  int next_request_id;
  std::map<std::string, int> file_versions;
  std::string stdout_buffer;
  std::string stderr_buffer;
  std::string last_error;
  std::vector<std::pair<std::string, std::vector<Diagnostic>>>
      pending_diagnostics;

  bool send_message(const std::string &json);
  std::string json_escape(const std::string &value) const;
  void append_log_line(const std::string &prefix, const std::string &line);
  void handle_stdout_data(const std::string &data);
  void handle_stderr_data(const std::string &data);

public:
  LSPClient(const std::string &language_name, const std::string &workspace_root,
            const std::vector<std::string> &argv);
  ~LSPClient();

  bool start();
  void stop();
  bool restart();
  bool poll();

  bool did_open(const std::string &filepath, const std::string &language_id,
                const std::string &text);
  bool did_change(const std::string &filepath, const std::string &text);
  bool did_save(const std::string &filepath, const std::string &text);
  std::vector<std::pair<std::string, std::vector<Diagnostic>>>
  consume_published_diagnostics();

  bool is_running() const { return running; }
  bool is_initialized() const { return initialized; }
  const std::string &get_language() const { return language; }
  const std::string &get_root_path() const { return root_path; }
  const std::string &get_last_error() const { return last_error; }
  std::string describe() const;
};

#endif
