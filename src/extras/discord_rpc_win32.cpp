// Windows transport: Discord's IPC endpoint is a named pipe, "\\.\pipe\discord-ipc-N".
//
// Reads stay non-blocking by peeking first: PeekNamedPipe reports whether any
// bytes are queued, so the poll loop never blocks the editor on a silent pipe.
// The handle itself carries no extra state, which keeps it interchangeable
// with the POSIX implementation.
#include "discord_rpc_transport.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace discord_ipc
{
  namespace
  {
    const char *kPipePrefix = "\\\\.\\pipe\\discord-ipc-";
  } // namespace

  std::vector<std::string> candidate_endpoints()
  {
    std::vector<std::string> endpoints;
    for (int index = 0; index <= 9; index++)
    {
      endpoints.push_back(std::string(kPipePrefix) + std::to_string(index));
    }
    return endpoints;
  }

  bool probe_exists(const std::string &endpoint)
  {
    // WaitNamedPipe with a zero timeout reports whether an *instance* is
    // currently available: enough to tell a missing Discord apart from a busy
    // one for diagnostics.
    return WaitNamedPipeA(endpoint.c_str(), 0) != 0;
  }

  Handle connect_endpoint(const std::string &endpoint)
  {
    // A busy pipe instance is normal (Discord serves one client per instance
    // and creates more on demand): wait briefly, then retry, exactly like the
    // official clients do.
    for (int attempt = 0; attempt < 3; attempt++)
    {
      HANDLE pipe = CreateFileA(
          endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (pipe != INVALID_HANDLE_VALUE)
      {
        // Byte mode, no buffering: the RPC frames are length-prefixed already.
        DWORD mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
        return reinterpret_cast<Handle>(pipe);
      }
      if (GetLastError() != ERROR_PIPE_BUSY)
      {
        return kInvalidHandle;
      }
      if (!WaitNamedPipeA(endpoint.c_str(), 200))
      {
        return kInvalidHandle;
      }
    }
    return kInvalidHandle;
  }

  void close_handle(Handle handle)
  {
    if (handle != kInvalidHandle)
    {
      CloseHandle(reinterpret_cast<HANDLE>(handle));
    }
  }

  long read_bytes(Handle handle, uint8_t *buffer, size_t length)
  {
    if (handle == kInvalidHandle)
    {
      return -2;
    }
    HANDLE pipe = reinterpret_cast<HANDLE>(handle);
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr))
    {
      return -2;
    }
    if (available == 0)
    {
      return -1; // nothing queued yet
    }
    DWORD to_read = static_cast<DWORD>(available < length ? available : length);
    DWORD read = 0;
    if (!ReadFile(pipe, buffer, to_read, &read, nullptr))
    {
      const DWORD error = GetLastError();
      if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
      {
        return 0; // peer closed
      }
      return -2;
    }
    return static_cast<long>(read);
  }

  long write_bytes(Handle handle, const uint8_t *data, size_t length)
  {
    if (handle == kInvalidHandle)
    {
      return -2;
    }
    HANDLE pipe = reinterpret_cast<HANDLE>(handle);
    DWORD written = 0;
    if (!WriteFile(pipe, data, static_cast<DWORD>(length), &written, nullptr))
    {
      const DWORD error = GetLastError();
      if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED)
      {
        return -2;
      }
      // A full pipe buffer is transient: report "try again" like EAGAIN.
      return -1;
    }
    return static_cast<long>(written);
  }
} // namespace discord_ipc
