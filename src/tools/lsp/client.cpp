// LSPClient transport and lifecycle: child-process spawning, pipe
// plumbing, framed writes (send_message / flush_pending_writes),
// start / stop / restart, and the poll loop. Message parsing lives in
// messages.cpp, protocol conversions in protocol.cpp, and outbound
// request builders in requests.cpp.
#include "tools/lsp/internal.h"
#include "tools/lsp/client.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string.h>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
using ssize_t = intptr_t;
#define read _read
#define write _write
#define close _close
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

using namespace lsp_detail;

namespace
{
  bool set_non_blocking(int fd)
  {
#ifdef _WIN32
    (void)fd;
    return true;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
      return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
  }

#ifdef _WIN32
  std::string windows_error_message(DWORD code)
  {
    char *text = nullptr;
    DWORD len = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                                   | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr,
                               code,
                               MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPSTR>(&text),
                               0,
                               nullptr);
    std::string message =
        len && text ? std::string(text, len) : "Windows error " + std::to_string(code);
    if (text)
    {
      LocalFree(text);
    }
    while (!message.empty()
           && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
    {
      message.pop_back();
    }
    return message;
  }

  std::string quote_windows_arg(const std::string &arg)
  {
    if (arg.empty())
    {
      return "\"\"";
    }
    bool needs_quotes = false;
    for (char c : arg)
    {
      if (std::isspace((unsigned char)c) || c == '"')
      {
        needs_quotes = true;
        break;
      }
    }
    if (!needs_quotes)
    {
      return arg;
    }
    std::string out = "\"";
    int backslashes = 0;
    for (char c : arg)
    {
      if (c == '\\')
      {
        backslashes++;
        continue;
      }
      if (c == '"')
      {
        out.append((size_t)(backslashes * 2 + 1), '\\');
        out.push_back(c);
        backslashes = 0;
        continue;
      }
      out.append((size_t)backslashes, '\\');
      backslashes = 0;
      out.push_back(c);
    }
    out.append((size_t)(backslashes * 2), '\\');
    out.push_back('"');
    return out;
  }

  std::string windows_command_line(const std::vector<std::string> &argv)
  {
    std::string out;
    for (size_t i = 0; i < argv.size(); i++)
    {
      if (i > 0)
      {
        out.push_back(' ');
      }
      out += quote_windows_arg(argv[i]);
    }
    return out;
  }

  bool fd_has_data(int fd)
  {
    intptr_t os_handle = _get_osfhandle(fd);
    if (os_handle == -1)
    {
      return false;
    }
    DWORD available = 0;
    if (!PeekNamedPipe(
            reinterpret_cast<HANDLE>(os_handle), nullptr, 0, nullptr, &available, nullptr))
    {
      return false;
    }
    return available > 0;
  }

  unsigned long current_process_id()
  {
    return GetCurrentProcessId();
  }
#else
  bool fd_has_data(int)
  {
    return true;
  }
  long current_process_id()
  {
    return getpid();
  }
#endif

  std::string get_lsp_log_path(const std::string &language)
  {
    const char *override_home = getenv("JOT_CONFIG_HOME");
    if (override_home && *override_home)
    {
      fs::path base = fs::path(override_home) / "logs";
      std::error_code ec;
      fs::create_directories(base, ec);
      return (base / ("lsp_" + language + ".log")).string();
    }
#ifdef _WIN32
    const char *app_data = getenv("APPDATA");
    fs::path base = app_data && *app_data ? fs::path(app_data) / "jot" / "logs"
                                          : fs::temp_directory_path() / "jot-logs";
#else
    const char *home = getenv("HOME");
    fs::path base =
        home ? fs::path(home) / ".config" / "jot" / "logs" : fs::temp_directory_path() / "jot-logs";
#endif
    std::error_code ec;
    fs::create_directories(base, ec);
    return (base / ("lsp_" + language + ".log")).string();
  }

} // namespace

LSPClient::LSPClient(const std::string &language_name,
                     const std::string &workspace_root,
                     const std::vector<std::string> &argv,
                     const std::vector<std::string> &library_dirs_arg,
                     const std::string &initialization_options_arg)
    : language(language_name), root_path(workspace_root), command(argv),
      library_dirs(library_dirs_arg), initialization_options(initialization_options_arg),
      stdin_fd(-1), stdout_fd(-1), stderr_fd(-1),
      child_pid(-1), running(false), initialized(false), uses_utf8_positions(false),
      shutdown_complete(false), next_request_id(1), initialize_request_id(0),
      shutdown_request_id(0)
{
}

LSPClient::~LSPClient()
{
  stop();
}

void LSPClient::append_log_line(const std::string &prefix, const std::string &line)
{
  constexpr std::uintmax_t kMaxLspLogBytes = 1024 * 1024;
  constexpr size_t kMaxLspLogLineBytes = 16 * 1024;
  const std::string path = get_lsp_log_path(language);
  std::error_code ec;
  if (fs::file_size(path, ec) > kMaxLspLogBytes)
  {
    std::ofstream truncated_log(path, std::ios::trunc);
  }
  std::ofstream log(path, std::ios::app);
  if (!log.is_open())
  {
    return;
  }
  log << prefix << line.substr(0, kMaxLspLogLineBytes) << "\n";
}

bool LSPClient::send_message(const std::string &json, bool allow_during_initialization)
{
  constexpr size_t kMaxLspMessageBytes = 16 * 1024 * 1024;
  constexpr size_t kMaxDeferredMessages = 256;
  constexpr size_t kMaxOutboundBytes = 32 * 1024 * 1024;
  if (!running || stdin_fd < 0)
  {
    return false;
  }
  if (json.size() > kMaxLspMessageBytes)
  {
    last_error = "LSP outbound message exceeds size limit";
    return false;
  }
  if (!initialized && !allow_during_initialization)
  {
    if (deferred_messages.size() >= kMaxDeferredMessages)
    {
      last_error = "LSP initialization queue is full";
      return false;
    }
    deferred_messages.push_back(json);
    return true;
  }

  std::ostringstream payload;
  payload << "Content-Length: " << json.size() << "\r\n\r\n" << json;
  if (outbound_buffer.size() >= kMaxOutboundBytes
      || (size_t)payload.tellp() > kMaxOutboundBytes - outbound_buffer.size())
  {
    last_error = "LSP outbound queue is full";
    return false;
  }
  outbound_buffer += payload.str();

  append_log_line("SEND ", json);
  return flush_pending_writes();
}

bool LSPClient::flush_pending_writes()
{
  if (!running || stdin_fd < 0)
  {
    return false;
  }

  while (!outbound_buffer.empty())
  {
    ssize_t written = write(stdin_fd, outbound_buffer.data(), outbound_buffer.size());
    if (written > 0)
    {
      outbound_buffer.erase(0, (size_t)written);
      continue;
    }

    if (written < 0 && errno == EINTR)
    {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      return true;
    }

    last_error = written < 0 ? strerror(errno) : "LSP stdin closed";
    append_log_line("SEND-ERR ", last_error);
    running = false;
    initialized = false;
    return false;
  }

  return true;
}

// The completion-related client capabilities, as one string so the initialize
// request and the test that asserts them cannot drift apart. labelDetails is
// opt-in per the spec: a server only sends it (and the popup's richer rows are
// built from it) when the client asks for it here.
std::string LSPClient::completion_client_capabilities()
{
  return "\"completion\":{"
         "\"dynamicRegistration\":false,"
         "\"contextSupport\":true,"
         "\"completionItem\":{"
         "\"snippetSupport\":true,"
         "\"deprecatedSupport\":true,"
         "\"preselectSupport\":true,"
         "\"commitCharactersSupport\":true,"
         "\"documentationFormat\":[\"markdown\",\"plaintext\"],"
         "\"labelDetailsSupport\":true,"
         "\"tagSupport\":{\"valueSet\":[1]},"
         "\"insertReplaceSupport\":false,"
         "\"resolveSupport\":{\"properties\":[\"documentation\",\"detail\"]}"
         "}"
         "}";
}

std::string LSPClient::window_client_capabilities()
{
  // Servers gate $/progress on this: without it they check once and never send
  // work-done progress (helix advertises the same flag).
  return "\"window\":{\"workDoneProgress\":true}";
}

bool LSPClient::start()
{
  if (running)
  {
    return true;
  }
  if (command.empty())
  {
    last_error = "empty command";
    return false;
  }

#ifndef _WIN32
  static const bool sigpipe_ignored = []
  {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)sigpipe_ignored;
#endif

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;

  auto close_handle = [](HANDLE &handle)
  {
    if (handle)
    {
      CloseHandle(handle);
      handle = nullptr;
    }
  };

  if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)
      || !CreatePipe(&stdout_read, &stdout_write, &sa, 0)
      || !CreatePipe(&stderr_read, &stderr_write, &sa, 0))
  {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_read);
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stdout_write);
    close_handle(stderr_read);
    close_handle(stderr_write);
    return false;
  }

  SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = stdin_read;
  startup.hStdOutput = stdout_write;
  startup.hStdError = stderr_write;

  PROCESS_INFORMATION process{};
  std::string command_line = windows_command_line(command);
  std::string cwd = root_path.empty() ? std::string() : root_path;
  BOOL created = CreateProcessA(nullptr,
                                command_line.data(),
                                nullptr,
                                nullptr,
                                TRUE,
                                CREATE_NO_WINDOW,
                                nullptr,
                                cwd.empty() ? nullptr : cwd.c_str(),
                                &startup,
                                &process);

  close_handle(stdin_read);
  close_handle(stdout_write);
  close_handle(stderr_write);

  if (!created)
  {
    last_error = windows_error_message(GetLastError());
    close_handle(stdin_write);
    close_handle(stdout_read);
    close_handle(stderr_read);
    return false;
  }

  CloseHandle(process.hThread);
  child_process_handle = process.hProcess;
  child_pid = (int)process.dwProcessId;

  stdin_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdin_write), _O_WRONLY | _O_BINARY);
  stdout_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stdout_read), _O_RDONLY | _O_BINARY);
  stderr_fd = _open_osfhandle(reinterpret_cast<intptr_t>(stderr_read), _O_RDONLY | _O_BINARY);
  if (stdin_fd < 0 || stdout_fd < 0 || stderr_fd < 0)
  {
    last_error = "failed to convert process pipes to file descriptors";
    stop();
    return false;
  }
#else
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  auto close_pipe = [](int pipe_fds[2])
  {
    if (pipe_fds[0] >= 0)
      close(pipe_fds[0]);
    if (pipe_fds[1] >= 0)
      close(pipe_fds[1]);
    pipe_fds[0] = -1;
    pipe_fds[1] = -1;
  };
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
  {
    last_error = strerror(errno);
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    last_error = strerror(errno);
    close_pipe(stdin_pipe);
    close_pipe(stdout_pipe);
    close_pipe(stderr_pipe);
    return false;
  }

  if (pid == 0)
  {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    dup2(stderr_pipe[1], STDERR_FILENO);

    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);

    std::vector<char *> argv;
    argv.reserve(command.size() + 1);
    for (const auto &arg : command)
    {
      argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);

    if (chdir(root_path.c_str()) != 0)
    {
      _exit(127);
    }
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  close(stderr_pipe[1]);

  stdin_fd = stdin_pipe[1];
  stdout_fd = stdout_pipe[0];
  stderr_fd = stderr_pipe[0];
  child_pid = pid;
#endif
  running = true;
  initialized = false;
  uses_utf8_positions = false;
  shutdown_complete = false;
  next_request_id = 1;
  initialize_request_id = 0;
  shutdown_request_id = 0;
  file_versions.clear();
  document_texts.clear();
  pending_completion_requests.clear();
  pending_hover_requests.clear();
  pending_signature_requests.clear();
  pending_definition_requests.clear();
  pending_switch_source_header_requests.clear();
  pending_document_symbol_requests.clear();
  pending_format_requests.clear();
  pending_completions.clear();
  pending_hovers.clear();
  pending_signatures.clear();
  pending_definitions.clear();
  pending_switch_source_headers.clear();
  pending_document_symbols.clear();
  pending_formats.clear();
  stdout_buffer.clear();
  stderr_buffer.clear();
  outbound_buffer.clear();
  deferred_messages.clear();
  last_error.clear();

  set_non_blocking(stdin_fd);
  set_non_blocking(stdout_fd);
  set_non_blocking(stderr_fd);

  std::ostringstream init;
  initialize_request_id = next_request_id++;
  init << "{"
       << "\"jsonrpc\":\"2.0\","
       << "\"id\":" << initialize_request_id << ","
       << "\"method\":\"initialize\","
       << "\"params\":" << initialize_params_json()
       << "}";
  if (!send_message(init.str(), true))
  {
    stop();
    return false;
  }

  append_log_line("INFO ", "Initializing " + describe());
  return true;
}

// The params of the initialize request, on its own so a test can parse it. Two
// separate comma/brace slips in this assembly have made the whole request
// unparseable, and a server that cannot read `initialize` never starts at all --
// a failure that looks like "the language server does nothing", with the only
// evidence in the server's stderr.
std::string LSPClient::initialize_params_json() const
{
  std::ostringstream params;
  params << "{"
       << "\"processId\":" << current_process_id() << ","
       << "\"rootUri\":\"" << json_escape(to_file_uri(root_path)) << "\","
       << "\"rootPath\":\"" << json_escape(root_path) << "\","
       << "\"capabilities\":{"
       << "\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
       << "\"textDocument\":{"
       << completion_client_capabilities()
       // The helper returns a single member, so the separator lives here: drop
       // it and the whole initialize request becomes invalid JSON and the server
       // refuses to start (clangd answers with a JSON parse error).
       << ","
       << "\"hover\":{\"dynamicRegistration\":false,"
       << "\"contentFormat\":[\"markdown\",\"plaintext\"]},"
       << "\"definition\":{\"dynamicRegistration\":false,"
       << "\"linkSupport\":true},"
       // The rest of the location lookups clangd offers. Declaring them is what
       // makes clangd advertise the providers, and they share the definition
       // reply path (Location | Location[] | LocationLink[]).
       << "\"declaration\":{\"dynamicRegistration\":false,"
       << "\"linkSupport\":true},"
       << "\"typeDefinition\":{\"dynamicRegistration\":false,"
       << "\"linkSupport\":true},"
       << "\"implementation\":{\"dynamicRegistration\":false,"
       << "\"linkSupport\":true},"
       << "\"documentSymbol\":{\"dynamicRegistration\":false,"
       << "\"hierarchicalDocumentSymbolSupport\":true,"       << "\"symbolKind\":{\"valueSet\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
               "16,17,18,19,20,21,22,23,24,25,26]}},"
       << "\"inlayHint\":{\"dynamicRegistration\":false}"
       << "},"
       << "\"workspace\":{\"configuration\":true}"
       // Capabilities is still open here: the window block belongs to it, and
       // the `},` below closes it. Placing it after that brace would make it a
       // params member instead, which servers ignore -- and the extra brace made
       // the whole request unparseable, so clangd answered with a JSON error and
       // never reported progress at all.
       << ","
       << window_client_capabilities()
       << "},"
       << "\"workspaceFolders\":[{\"uri\":\"" << json_escape(to_file_uri(root_path))
       << "\",\"name\":\"" << json_escape(fs::path(root_path).filename().string()) << "\"}]"
       << (initialization_options.empty()
               ? ""
               : ",\"initializationOptions\":{" + initialization_options + "}")
       << "}";
  return params.str();
}

void LSPClient::stop()
{
  if (!running && stdin_fd < 0 && stdout_fd < 0 && stderr_fd < 0 && child_pid <= 0)
  {
    return;
  }

  if (running && stdin_fd >= 0 && initialized)
  {
    shutdown_complete = false;
    shutdown_request_id = next_request_id++;
    std::ostringstream shutdown;
    shutdown << "{\"jsonrpc\":\"2.0\",\"id\":" << shutdown_request_id
             << ",\"method\":\"shutdown\",\"params\":null}";
    if (send_message(shutdown.str(), true))
    {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
      while (!shutdown_complete && std::chrono::steady_clock::now() < deadline)
      {
        poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }
  if (running && stdin_fd >= 0)
  {
    send_message("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":{}}", true);
    flush_pending_writes();
  }
  running = false;
  initialized = false;

  if (child_pid > 0)
  {
#ifdef _WIN32
    if (child_process_handle)
    {
      if (WaitForSingleObject(reinterpret_cast<HANDLE>(child_process_handle), 200) == WAIT_TIMEOUT)
      {
        TerminateProcess(reinterpret_cast<HANDLE>(child_process_handle), 1);
      }
      CloseHandle(reinterpret_cast<HANDLE>(child_process_handle));
      child_process_handle = nullptr;
    }
#else
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (waitpid(child_pid, &status, WNOHANG) == 0 && std::chrono::steady_clock::now() < deadline)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (waitpid(child_pid, &status, WNOHANG) == 0)
    {
      kill(child_pid, SIGTERM);
      const auto kill_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
      while (waitpid(child_pid, &status, WNOHANG) == 0
             && std::chrono::steady_clock::now() < kill_deadline)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (waitpid(child_pid, &status, WNOHANG) == 0)
      {
        kill(child_pid, SIGKILL);
        waitpid(child_pid, &status, 0);
      }
    }
#endif
  }

  close_transport();
  child_pid = -1;
#ifdef _WIN32
  child_process_handle = nullptr;
#endif
  running = false;
  initialized = false;
  uses_utf8_positions = false;
  shutdown_complete = false;
  initialize_request_id = 0;
  shutdown_request_id = 0;
  file_versions.clear();
  document_texts.clear();
  pending_completion_requests.clear();
  pending_hover_requests.clear();
  pending_signature_requests.clear();
  pending_definition_requests.clear();
  pending_switch_source_header_requests.clear();
  pending_document_symbol_requests.clear();
  pending_format_requests.clear();
  pending_completions.clear();
  pending_hovers.clear();
  pending_signatures.clear();
  pending_definitions.clear();
  pending_switch_source_headers.clear();
  pending_document_symbols.clear();
  pending_formats.clear();
  outbound_buffer.clear();
  deferred_messages.clear();
}

void LSPClient::close_transport()
{
  if (stdin_fd >= 0)
    close(stdin_fd);
  if (stdout_fd >= 0)
    close(stdout_fd);
  if (stderr_fd >= 0)
    close(stderr_fd);
  stdin_fd = -1;
  stdout_fd = -1;
  stderr_fd = -1;
}

bool LSPClient::restart()
{
  stop();
  return start();
}

bool LSPClient::poll()
{
  if (!running)
  {
    return false;
  }

  bool changed = false;
  if (!outbound_buffer.empty())
  {
    if (!flush_pending_writes())
    {
      return true;
    }
  }

  char buf[4096];
  size_t bytes_polled = 0;
  constexpr size_t kPollByteLimit = 256 * 1024;
  while (stdout_fd >= 0 && bytes_polled < kPollByteLimit)
  {
    if (!fd_has_data(stdout_fd))
    {
      break;
    }
    ssize_t n = read(stdout_fd, buf, sizeof(buf));
    if (n <= 0)
    {
      break;
    }
    handle_stdout_data(std::string(buf, buf + n));
    bytes_polled += (size_t)n;
    changed = true;
  }
  while (stderr_fd >= 0 && bytes_polled < kPollByteLimit)
  {
    if (!fd_has_data(stderr_fd))
    {
      break;
    }
    ssize_t n = read(stderr_fd, buf, sizeof(buf));
    if (n <= 0)
    {
      break;
    }
    handle_stderr_data(std::string(buf, buf + n));
    bytes_polled += (size_t)n;
  }

  int status = 0;
  if (child_pid > 0)
  {
#ifdef _WIN32
    HANDLE process = reinterpret_cast<HANDLE>(child_process_handle);
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
    {
      DWORD exit_code = 0;
      GetExitCodeProcess(process, &exit_code);
      running = false;
      initialized = false;
      last_error = "process exited with status " + std::to_string(exit_code);
      append_log_line("INFO ", last_error);
      CloseHandle(process);
      child_process_handle = nullptr;
      close_transport();
      child_pid = -1;
      changed = true;
    }
#else
    pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid)
    {
      running = false;
      initialized = false;
      if (WIFEXITED(status))
      {
        last_error = "process exited with status " + std::to_string(WEXITSTATUS(status));
      }
      else
      {
        last_error = "process exited unexpectedly";
      }
      append_log_line("INFO ", last_error);
      close_transport();
      child_pid = -1;
      changed = true;
    }
#endif
  }

  return changed;
}

