#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "tools/terminal/win32_terminal_session.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
#ifndef HPCON
  using HPCON = void *;
#endif

  using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
  using ResizePseudoConsoleFn = HRESULT(WINAPI *)(HPCON, COORD);
  using ClosePseudoConsoleFn = void(WINAPI *)(HPCON);

  struct ConPtyApi
  {
    HMODULE module = nullptr;
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;

    bool load()
    {
      if (create && resize && close)
      {
        return true;
      }

      for (const char *name : {"conpty.dll", "kernel32.dll"})
      {
        HMODULE candidate = GetModuleHandleA(name);
        if (!candidate)
        {
          candidate = LoadLibraryA(name);
        }
        if (!candidate)
        {
          continue;
        }

        auto create_fn = reinterpret_cast<CreatePseudoConsoleFn>(
            GetProcAddress(candidate, "CreatePseudoConsole"));
        auto resize_fn = reinterpret_cast<ResizePseudoConsoleFn>(
            GetProcAddress(candidate, "ResizePseudoConsole"));
        auto close_fn = reinterpret_cast<ClosePseudoConsoleFn>(
            GetProcAddress(candidate, "ClosePseudoConsole"));
        if (create_fn && resize_fn && close_fn)
        {
          module = candidate;
          create = create_fn;
          resize = resize_fn;
          close = close_fn;
          return true;
        }
        if (candidate != GetModuleHandleA(name))
        {
          FreeLibrary(candidate);
        }
      }
      return false;
    }
  };

  ConPtyApi &conpty_api()
  {
    static ConPtyApi api;
    return api;
  }

  std::wstring utf8_to_wide(const std::string &value)
  {
    if (value.empty())
    {
      return {};
    }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
    {
      return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) <= 0)
    {
      return {};
    }
    return result;
  }

  std::wstring quote_command_argument(const std::wstring &argument)
  {
    if (argument.empty())
    {
      return L"\"\"";
    }
    bool needs_quotes = false;
    for (wchar_t ch : argument)
    {
      if (ch == L' ' || ch == L'\t' || ch == L'\"')
      {
        needs_quotes = true;
        break;
      }
    }
    if (!needs_quotes)
    {
      return argument;
    }

    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : argument)
    {
      if (ch == L'\\')
      {
        backslashes++;
        continue;
      }
      if (ch == L'\"')
      {
        result.append(backslashes * 2 + 1, L'\\');
        result.push_back(L'\"');
        backslashes = 0;
        continue;
      }
      result.append(backslashes, L'\\');
      backslashes = 0;
      result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
  }

  void close_handle(HANDLE &handle)
  {
    if (handle && handle != INVALID_HANDLE_VALUE)
    {
      CloseHandle(handle);
    }
    handle = nullptr;
  }

  COORD terminal_size(int rows, int cols)
  {
    return {static_cast<SHORT>(std::clamp(cols, 1, 32767)),
            static_cast<SHORT>(std::clamp(rows, 1, 32767))};
  }
} // namespace

Win32TerminalSession::Win32TerminalSession() = default;

Win32TerminalSession::~Win32TerminalSession()
{
  close();
}

bool Win32TerminalSession::open(const std::string &cwd, int rows, int cols)
{
  close();
  ConPtyApi &api = conpty_api();
  if (!api.load())
  {
    return false;
  }

  HANDLE input_read = nullptr;
  HANDLE output_write = nullptr;
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;

  if (!CreatePipe(&input_read, &input_write_, &security, 0)
      || !CreatePipe(&output_read_, &output_write, &security, 0))
  {
    close_handle(input_read);
    close_handle(output_write);
    close_handles();
    return false;
  }

  SetHandleInformation(input_write_, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output_read_, HANDLE_FLAG_INHERIT, 0);

  HPCON pseudo_console = nullptr;
  HRESULT result = api.create(terminal_size(rows, cols), input_read, output_write, 0,
                              &pseudo_console);
  close_handle(input_read);
  close_handle(output_write);
  if (FAILED(result) || !pseudo_console)
  {
    close_pseudo_console();
    close_handles();
    return false;
  }
  pseudo_console_ = pseudo_console;

  SIZE_T attribute_bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_bytes);
  if (attribute_bytes == 0)
  {
    close();
    return false;
  }
  std::vector<unsigned char> attributes(attribute_bytes);
  auto *attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_bytes)
      || !UpdateProcThreadAttribute(attribute_list, 0,
                                    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                    pseudo_console, sizeof(pseudo_console), nullptr, nullptr))
  {
    DeleteProcThreadAttributeList(attribute_list);
    close();
    return false;
  }

  const char *comspec = std::getenv("COMSPEC");
  std::string shell = comspec && *comspec ? comspec : "cmd.exe";
  std::wstring shell_wide = utf8_to_wide(shell);
  if (shell_wide.empty())
  {
    shell_wide = L"cmd.exe";
  }
  std::wstring command_line = quote_command_argument(shell_wide) + L" /d /q";
  std::wstring cwd_wide = utf8_to_wide(cwd);

  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList = attribute_list;
  PROCESS_INFORMATION process{};
  BOOL created = CreateProcessW(shell_wide.c_str(), command_line.data(), nullptr, nullptr,
                                FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                nullptr, cwd_wide.empty() ? nullptr : cwd_wide.c_str(),
                                &startup.StartupInfo, &process);
  DeleteProcThreadAttributeList(attribute_list);
  if (!created)
  {
    close();
    return false;
  }

  CloseHandle(process.hThread);
  process_handle_ = process.hProcess;
  process_id_ = static_cast<int>(process.dwProcessId);
  return true;
}

void Win32TerminalSession::close()
{
  if (process_handle_)
  {
    if (WaitForSingleObject(static_cast<HANDLE>(process_handle_), 0) == WAIT_TIMEOUT)
    {
      TerminateProcess(static_cast<HANDLE>(process_handle_), 1);
      WaitForSingleObject(static_cast<HANDLE>(process_handle_), 200);
    }
  }
  close_pseudo_console();
  close_handles();
  process_id_ = -1;
}

void Win32TerminalSession::close_after_exit()
{
  if (!process_exited())
  {
    return;
  }
  close_pseudo_console();
  close_handles();
  process_id_ = -1;
}

bool Win32TerminalSession::read_available(std::string &out, size_t max_bytes)
{
  out.clear();
  if (!active() || max_bytes == 0)
  {
    return false;
  }

  DWORD available = 0;
  if (!PeekNamedPipe(static_cast<HANDLE>(output_read_), nullptr, 0, nullptr, &available, nullptr))
  {
    return false;
  }
  if (available == 0)
  {
    return false;
  }

  DWORD requested = static_cast<DWORD>(std::min<size_t>(max_bytes, available));
  std::string buffer(requested, '\0');
  DWORD read_bytes = 0;
  if (!ReadFile(static_cast<HANDLE>(output_read_), buffer.data(), requested, &read_bytes, nullptr)
      || read_bytes == 0)
  {
    return false;
  }
  buffer.resize(read_bytes);
  out = std::move(buffer);
  return true;
}

size_t Win32TerminalSession::write(const char *data, size_t size)
{
  if (!active() || !data || size == 0)
  {
    return 0;
  }
  DWORD written = 0;
  if (!WriteFile(static_cast<HANDLE>(input_write_), data,
                 static_cast<DWORD>(std::min<size_t>(size, 0xFFFFFFFFu)), &written, nullptr))
  {
    return 0;
  }
  return written;
}

bool Win32TerminalSession::resize(int rows, int cols)
{
  if (!active() || !pseudo_console_)
  {
    return false;
  }
  ConPtyApi &api = conpty_api();
  return api.load() && SUCCEEDED(api.resize(static_cast<HPCON>(pseudo_console_),
                                             terminal_size(rows, cols)));
}

bool Win32TerminalSession::process_exited() const
{
  return process_handle_ && WaitForSingleObject(static_cast<HANDLE>(process_handle_), 0)
         == WAIT_OBJECT_0;
}

bool Win32TerminalSession::active() const
{
  return process_handle_ != nullptr;
}

int Win32TerminalSession::process_id() const
{
  return process_id_;
}

void Win32TerminalSession::close_handles()
{
  close_handle(reinterpret_cast<HANDLE &>(process_handle_));
  close_handle(reinterpret_cast<HANDLE &>(input_write_));
  close_handle(reinterpret_cast<HANDLE &>(output_read_));
}

void Win32TerminalSession::close_pseudo_console()
{
  if (!pseudo_console_)
  {
    return;
  }
  ConPtyApi &api = conpty_api();
  if (api.load())
  {
    api.close(static_cast<HPCON>(pseudo_console_));
  }
  pseudo_console_ = nullptr;
}
