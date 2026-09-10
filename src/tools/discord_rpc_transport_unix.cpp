// POSIX transport: AF_UNIX stream sockets under every directory Discord is
// known to use.
//
// The single-base probe this replaces was why presence never connected on many
// Linux installs: Discord puts "discord-ipc-N" either in XDG_RUNTIME_DIR (the
// current default) or in a temp directory such as /tmp (older clients, Flatpak,
// pacman), and picking the wrong one fails silently.
#include "discord_rpc_transport.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace discord_ipc
{
  namespace
  {
    void add_unique(std::vector<std::string> &out, const std::string &value)
    {
      if (value.empty())
      {
        return;
      }
      for (const std::string &existing : out)
      {
        if (existing == value)
        {
          return;
        }
      }
      out.push_back(value);
    }

    // Directories to probe, most-likely first. The upstream Discord client
    // library builds the same list; "temp" bases are checked because
    // sandboxed and distro-packaged clients cannot always write into
    // XDG_RUNTIME_DIR and fall back to their own temp dir.
    std::vector<std::string> base_directories()
    {
      std::vector<std::string> bases;
#if defined(__APPLE__)
      const char *tmp = std::getenv("TMPDIR");
      add_unique(bases, tmp && *tmp ? std::string(tmp) : std::string("/tmp"));
#else
      const char *xdg = std::getenv("XDG_RUNTIME_DIR");
      if (xdg && *xdg)
      {
        add_unique(bases, xdg);
      }
      const char *snap = std::getenv("SNAP_USER_COMMON");
      if (snap && *snap)
      {
        add_unique(bases, snap);
      }
      for (const char *name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"})
      {
        const char *value = std::getenv(name);
        if (value && *value)
        {
          add_unique(bases, value);
        }
      }
      add_unique(bases, "/tmp");
#endif
      return bases;
    }
  } // namespace

  std::vector<std::string> candidate_endpoints()
  {
    std::vector<std::string> endpoints;
    for (const std::string &base : base_directories())
    {
      for (int index = 0; index <= 9; index++)
      {
        endpoints.push_back(base + "/discord-ipc-" + std::to_string(index));
      }
    }
    return endpoints;
  }

  bool probe_exists(const std::string &endpoint)
  {
    struct stat st{};
    return stat(endpoint.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
  }

  Handle connect_endpoint(const std::string &endpoint)
  {
    if (!probe_exists(endpoint))
    {
      return kInvalidHandle;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
      return kInvalidHandle;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, endpoint.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
      close(fd);
      return kInvalidHandle;
    }

    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1)
    {
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return static_cast<Handle>(fd);
  }

  void close_handle(Handle handle)
  {
    if (handle != kInvalidHandle)
    {
      close(static_cast<int>(handle));
    }
  }

  long read_bytes(Handle handle, uint8_t *buffer, size_t length)
  {
    if (handle == kInvalidHandle)
    {
      return -2;
    }
    const ssize_t n = read(static_cast<int>(handle), buffer, length);
    if (n == 0)
    {
      return 0; // peer closed
    }
    if (n < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      {
        return -1;
      }
      return -2;
    }
    return static_cast<long>(n);
  }

  long write_bytes(Handle handle, const uint8_t *data, size_t length)
  {
    if (handle == kInvalidHandle)
    {
      return -2;
    }
    const ssize_t n = write(static_cast<int>(handle), data, length);
    if (n < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      {
        return -1;
      }
      return -2;
    }
    return static_cast<long>(n);
  }
} // namespace discord_ipc
