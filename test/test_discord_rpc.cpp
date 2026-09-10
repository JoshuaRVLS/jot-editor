// Discord IPC client (src/tools/discord_rpc.cpp) against a fake Discord server.
//
// The bug this pins down: the client used to probe a single socket directory, so
// it silently never connected on installs whose socket lives somewhere else. The
// fake server therefore listens on discord-ipc-2 inside a scratch
// XDG_RUNTIME_DIR -- not index 0 -- and the client must find it anyway.
#include "tools/discord_rpc.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
  int make_listener(const std::string &path)
  {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
      return -1;
    }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    unlink(path.c_str());
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
    {
      close(fd);
      return -1;
    }
    if (listen(fd, 4) < 0)
    {
      close(fd);
      return -1;
    }
    return fd;
  }

  std::vector<uint8_t> frame_bytes(int opcode, const std::string &json)
  {
    std::vector<uint8_t> out;
    const uint32_t op = static_cast<uint32_t>(opcode);
    const uint32_t len = static_cast<uint32_t>(json.size());
    for (int i = 0; i < 4; i++)
    {
      out.push_back((op >> (i * 8)) & 0xFF);
    }
    for (int i = 0; i < 4; i++)
    {
      out.push_back((len >> (i * 8)) & 0xFF);
    }
    out.insert(out.end(), json.begin(), json.end());
    return out;
  }

  bool send_all(int fd, const std::vector<uint8_t> &bytes)
  {
    size_t sent = 0;
    while (sent < bytes.size())
    {
      const ssize_t n = write(fd, bytes.data() + sent, bytes.size() - sent);
      if (n <= 0)
      {
        return false;
      }
      sent += static_cast<size_t>(n);
    }
    return true;
  }

  // Reads one framed message; returns false on timeout/EOF.
  bool read_frame(int fd, int &opcode, std::string &json)
  {
    uint8_t header[8];
    size_t got = 0;
    while (got < sizeof(header))
    {
      const ssize_t n = read(fd, header + got, sizeof(header) - got);
      if (n <= 0)
      {
        return false;
      }
      got += static_cast<size_t>(n);
    }
    const uint32_t op = static_cast<uint32_t>(header[0]) | (static_cast<uint32_t>(header[1]) << 8)
                        | (static_cast<uint32_t>(header[2]) << 16)
                        | (static_cast<uint32_t>(header[3]) << 24);
    const uint32_t len = static_cast<uint32_t>(header[4]) | (static_cast<uint32_t>(header[5]) << 8)
                         | (static_cast<uint32_t>(header[6]) << 16)
                         | (static_cast<uint32_t>(header[7]) << 24);
    json.assign(len, '\0');
    size_t body = 0;
    while (body < len)
    {
      const ssize_t n = read(fd, json.data() + body, len - body);
      if (n <= 0)
      {
        return false;
      }
      body += static_cast<size_t>(n);
    }
    opcode = static_cast<int>(op);
    return true;
  }

  // A scratch XDG_RUNTIME_DIR pointing at `dir`, restored on destruction so the
  // rest of the suite sees the real environment.
  struct ScopedRuntimeDir
  {
    std::string previous;
    bool had_previous = false;

    explicit ScopedRuntimeDir(const std::string &dir)
    {
      const char *value = std::getenv("XDG_RUNTIME_DIR");
      had_previous = value != nullptr;
      previous = value ? value : "";
      setenv("XDG_RUNTIME_DIR", dir.c_str(), 1);
    }
    ~ScopedRuntimeDir()
    {
      if (had_previous)
      {
        setenv("XDG_RUNTIME_DIR", previous.c_str(), 1);
      }
      else
      {
        unsetenv("XDG_RUNTIME_DIR");
      }
    }
  };

  jot_discord::Activity sample_activity()
  {
    jot_discord::Activity activity;
    activity.details = "Editing main.rs";
    activity.state = "Workspace: jot";
    activity.has_timestamp = true;
    activity.start_timestamp = 1700000000;
    activity.large_image_key = "rust";
    activity.large_image_text = "Editing a RUST file";
    activity.small_image_key = "jot";
    activity.small_image_text = "jot";
    return activity;
  }
} // namespace

TEST_CASE("Discord IPC finds the socket in a non-default directory", "[jot]")
{
  char tmpl[] = "/tmp/jot_discord_rpc_XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string dir = tmpl;
  // Deliberately index 2: index 0 is what a single-path probe looked for.
  const std::string endpoint = dir + "/discord-ipc-2";

  const int listener = make_listener(endpoint);
  REQUIRE(listener >= 0);
  ScopedRuntimeDir runtime(dir);

  DiscordRPC rpc;
  rpc.set_app_id("1513610110256021524");

  const long long now = 1000000;
  rpc.poll(now);
  // The client connected: the listening socket now has a queued peer.
  const int peer = accept(listener, nullptr, nullptr);
  REQUIRE(peer >= 0);

  int opcode = -1;
  std::string json;
  REQUIRE(read_frame(peer, opcode, json));
  REQUIRE(opcode == 0); // HANDSHAKE
  REQUIRE(json.find("\"client_id\":\"1513610110256021524\"") != std::string::npos);
  REQUIRE_FALSE(rpc.is_connected()); // still HANDSHAKING

  // Discord: READY with a heartbeat interval.
  REQUIRE(send_all(peer,
                   frame_bytes(1,
                               "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{"
                               "\"v\":1,\"heartbeat_interval\":45000}}")));
  rpc.poll(now + 10);
  REQUIRE(rpc.is_connected());
  REQUIRE(rpc.last_error().empty());
  REQUIRE_FALSE(rpc.probed_endpoints().empty());
  REQUIRE(rpc.probed_endpoints().front() == endpoint);

  // An activity reaches Discord as SET_ACTIVITY with the rendered content.
  rpc.send_activity(sample_activity());
  REQUIRE(read_frame(peer, opcode, json));
  REQUIRE(opcode == 1); // FRAME
  REQUIRE(json.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos);
  REQUIRE(json.find("\"details\":\"Editing main.rs\"") != std::string::npos);
  REQUIRE(json.find("\"large_image\":\"rust\"") != std::string::npos);
  REQUIRE(json.find("\"state\":\"Workspace: jot\"") != std::string::npos);

  // An error reply (an asset key that was never uploaded) must surface instead
  // of being swallowed, and must not tear the session down.
  REQUIRE(send_all(peer,
                   frame_bytes(1,
                               "{\"cmd\":\"SET_ACTIVITY\",\"evt\":\"ERROR\",\"data\":{"
                               "\"code\":4000,\"message\":\"Invalid Asset\"}}")));
  rpc.poll(now + 20);
  REQUIRE(rpc.is_connected());
  REQUIRE(rpc.last_error() == "Invalid Asset (code 4000)");

  // Clearing sends SET_ACTIVITY without an activity object.
  rpc.clear_presence();
  REQUIRE(read_frame(peer, opcode, json));
  REQUIRE(json.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos);
  REQUIRE(json.find("\"activity\"") == std::string::npos);

  // Discord going away drops the session (the next poll retries later).
  close(peer);
  rpc.poll(now + 30);
  REQUIRE_FALSE(rpc.is_connected());

  close(listener);
  unlink(endpoint.c_str());
  fs::remove_all(dir);
}

TEST_CASE("Discord IPC queues an activity until the handshake completes", "[jot]")
{
  char tmpl[] = "/tmp/jot_discord_rpc_XXXXXX";
  REQUIRE(mkdtemp(tmpl) != nullptr);
  const std::string dir = tmpl;
  const std::string endpoint = dir + "/discord-ipc-0";
  const int listener = make_listener(endpoint);
  REQUIRE(listener >= 0);
  ScopedRuntimeDir runtime(dir);

  DiscordRPC rpc;
  rpc.set_app_id("42");

  // Sent while still connecting: the presence is remembered, not lost.
  rpc.send_activity(sample_activity());
  REQUIRE(rpc.has_pending_activity());

  rpc.poll(5000);
  const int peer = accept(listener, nullptr, nullptr);
  REQUIRE(peer >= 0);
  int opcode = -1;
  std::string json;
  REQUIRE(read_frame(peer, opcode, json));
  REQUIRE(opcode == 0);

  REQUIRE(send_all(peer,
                   frame_bytes(1, "{\"evt\":\"READY\",\"data\":{\"heartbeat_interval\":30000}}")));
  rpc.poll(5010);
  REQUIRE(rpc.is_connected());
  // The queued activity is replayed right after the handshake.
  REQUIRE(read_frame(peer, opcode, json));
  REQUIRE(opcode == 1);
  REQUIRE(json.find("\"details\":\"Editing main.rs\"") != std::string::npos);

  // disable() drops the queued presence so it is not replayed on reconnect.
  rpc.disconnect();
  REQUIRE_FALSE(rpc.has_pending_activity());
  REQUIRE(rpc.last_error().empty());

  close(peer);
  close(listener);
  unlink(endpoint.c_str());
  fs::remove_all(dir);
}
