#ifndef DISCORD_RPC_TRANSPORT_H
#define DISCORD_RPC_TRANSPORT_H

// Connection primitives for the Discord IPC channel, one implementation per
// platform (POSIX domain sockets, Windows named pipes). Only the byte pipe
// lives here: framing, the handshake state machine and presence content are
// shared in discord_rpc.cpp / discord_presence.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace discord_ipc
{
  // Platform handle: a file descriptor on POSIX, a pipe handle on Windows.
  using Handle = intptr_t;
  constexpr Handle kInvalidHandle = static_cast<Handle>(-1);

  // Every candidate endpoint for this platform, in the order they should be
  // tried. Discord listens on one of them, and *which* one varies by install
  // (XDG_RUNTIME_DIR vs /tmp vs a sandbox home), so probing all of them is what
  // makes the connection reliable -- a single-path probe silently fails on any
  // install that picked a different base.
  std::vector<std::string> candidate_endpoints();

  // Connects to one endpoint. Returns kInvalidHandle when it cannot be reached;
  // errors are expected here (most candidates do not exist) and are not
  // reported.
  Handle connect_endpoint(const std::string &endpoint);

  // Whether the endpoint exists at all (a socket or pipe left behind by a dead
  // client counts). Diagnostics only: connect_endpoint() is the real test.
  bool probe_exists(const std::string &endpoint);

  void close_handle(Handle handle);

  // Non-blocking reads/writes. read_bytes returns the byte count, 0 on
  // end-of-stream, -1 when nothing is available yet (EAGAIN / no data) and -2
  // on a real error.
  long read_bytes(Handle handle, uint8_t *buffer, size_t length);
  long write_bytes(Handle handle, const uint8_t *data, size_t length);
} // namespace discord_ipc

#endif // DISCORD_RPC_TRANSPORT_H
