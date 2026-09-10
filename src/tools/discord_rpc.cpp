// Discord IPC client: framing and connection state machine over the platform
// transport. See discord_rpc.h for the split of responsibilities.
#include "discord_rpc.h"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <process.h>
#include <windows.h>
#else
#include <ctime>
#include <unistd.h>
#endif

namespace
{
  constexpr long long kReconnectDelayMs = 30000;
  constexpr long long kHandshakeTimeoutMs = 10000;
  constexpr size_t kReadChunk = 4096;

  long pid_value()
  {
#if defined(_WIN32)
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
  }

  void install_sigpipe_handler()
  {
#if !defined(_WIN32)
    static bool installed = false;
    if (!installed)
    {
      signal(SIGPIPE, SIG_IGN);
      installed = true;
    }
#endif
  }

  // The Discord replies we inspect are tiny and fixed-shape (READY, ERROR), so
  // a full JSON parser would be overkill here; the presence payload itself is
  // generated, never parsed.
  std::string json_string_field(const std::string &json, const std::string &key)
  {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
      return "";
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
    {
      return "";
    }
    pos = json.find('"', pos);
    if (pos == std::string::npos)
    {
      return "";
    }
    std::string out;
    for (size_t i = pos + 1; i < json.size(); i++)
    {
      const char c = json[i];
      if (c == '\\' && i + 1 < json.size())
      {
        out += json[++i];
        continue;
      }
      if (c == '"')
      {
        break;
      }
      out += c;
    }
    return out;
  }

  bool json_int_field(const std::string &json, const std::string &key, long long &out)
  {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
      return false;
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
    {
      return false;
    }
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
    {
      pos++;
    }
    size_t end = pos;
    if (end < json.size() && json[end] == '-')
    {
      end++;
    }
    while (end < json.size() && json[end] >= '0' && json[end] <= '9')
    {
      end++;
    }
    if (end == pos)
    {
      return false;
    }
    out = std::strtoll(json.substr(pos, end - pos).c_str(), nullptr, 10);
    return true;
  }
} // namespace

DiscordRPC::DiscordRPC()
    : handle_(discord_ipc::kInvalidHandle), state_(DISCONNECTED), last_heartbeat_ms_(0),
      last_connect_attempt_ms_(0), heartbeat_interval_ms_(30000), nonce_counter_(0)
{
}

DiscordRPC::~DiscordRPC()
{
  close_connection();
}

void DiscordRPC::set_app_id(const std::string &app_id)
{
  if (app_id != app_id_)
  {
    app_id_ = app_id;
    // A different application means a different presence identity: reconnect so
    // the new client id is used instead of the cached session.
    close_connection();
    last_connect_attempt_ms_ = 0;
  }
}

std::string DiscordRPC::make_nonce()
{
  nonce_counter_++;
  return std::to_string(nonce_counter_);
}

bool DiscordRPC::find_and_connect()
{
  probed_.clear();
  const std::vector<std::string> candidates = discord_ipc::candidate_endpoints();
  for (const std::string &endpoint : candidates)
  {
    const discord_ipc::Handle handle = discord_ipc::connect_endpoint(endpoint);
    if (handle == discord_ipc::kInvalidHandle)
    {
      continue;
    }
    probed_.push_back(endpoint);
    handle_ = handle;
    return true;
  }
  // Nothing accepted a connection: remember which endpoints at least *exist*
  // (a socket or pipe left behind by a dead client), so :discord status can
  // tell "Discord is not running" apart from "the socket is there but refuses".
  for (const std::string &endpoint : candidates)
  {
    if (discord_ipc::probe_exists(endpoint))
    {
      probed_.push_back(endpoint);
    }
  }
  return false;
}

void DiscordRPC::close_connection()
{
  if (handle_ != discord_ipc::kInvalidHandle)
  {
    discord_ipc::close_handle(handle_);
    handle_ = discord_ipc::kInvalidHandle;
  }
  state_ = DISCONNECTED;
  read_buf_.clear();
}

void DiscordRPC::disconnect()
{
  close_connection();
  has_pending_ = false;
  pending_ = jot_discord::Activity{};
  last_error_.clear();
}

void DiscordRPC::send_handshake()
{
  const std::string client_id = app_id_.empty() ? "0" : app_id_;
  send_frame(0, "{\"v\":1,\"client_id\":\"" + jot_discord::json_escape(client_id) + "\"}");
}

void DiscordRPC::poll(long long now_ms)
{
  switch (state_)
  {
  case DISCONNECTED:
  {
    if (last_connect_attempt_ms_ > 0 && now_ms - last_connect_attempt_ms_ < kReconnectDelayMs)
    {
      return;
    }
    last_connect_attempt_ms_ = now_ms;
    if (!find_and_connect())
    {
      return;
    }
    install_sigpipe_handler();
    send_handshake();
    state_ = HANDSHAKING;
    last_heartbeat_ms_ = now_ms;
    break;
  }

  case HANDSHAKING:
  {
    int opcode = -1;
    std::string json;
    while (read_frame(opcode, json))
    {
      handle_frame(opcode, json);
      if (state_ == CONNECTED)
      {
        last_heartbeat_ms_ = now_ms;
        send_pending();
        return;
      }
    }
    if (state_ != HANDSHAKING)
    {
      return;
    }
    if (now_ms - last_connect_attempt_ms_ > kHandshakeTimeoutMs)
    {
      close_connection();
    }
    break;
  }

  case CONNECTED:
  {
    int opcode = -1;
    std::string json;
    while (read_frame(opcode, json))
    {
      handle_frame(opcode, json);
    }
    if (state_ != CONNECTED)
    {
      return;
    }
    if (now_ms - last_heartbeat_ms_ >= heartbeat_interval_ms_)
    {
      send_frame(3, ""); // PING
      last_heartbeat_ms_ = now_ms;
    }
    break;
  }
  }
}

bool DiscordRPC::write_all(const uint8_t *data, size_t length)
{
  if (handle_ == discord_ipc::kInvalidHandle)
  {
    return false;
  }
  size_t sent = 0;
  while (sent < length)
  {
    const long n = discord_ipc::write_bytes(handle_, data + sent, length - sent);
    if (n > 0)
    {
      sent += static_cast<size_t>(n);
      continue;
    }
    if (n == -1)
    {
      // Transient backpressure: keep the session and let the next tick resend
      // (presence is idempotent), instead of stalling the editor loop.
      return false;
    }
    close_connection();
    return false;
  }
  return true;
}

bool DiscordRPC::send_frame(int opcode, const std::string &json)
{
  if (handle_ == discord_ipc::kInvalidHandle)
  {
    return false;
  }
  const uint32_t op = static_cast<uint32_t>(opcode);
  const uint32_t len = static_cast<uint32_t>(json.size());
  std::vector<uint8_t> frame;
  frame.reserve(8 + json.size());
  frame.push_back(op & 0xFF);
  frame.push_back((op >> 8) & 0xFF);
  frame.push_back((op >> 16) & 0xFF);
  frame.push_back((op >> 24) & 0xFF);
  frame.push_back(len & 0xFF);
  frame.push_back((len >> 8) & 0xFF);
  frame.push_back((len >> 16) & 0xFF);
  frame.push_back((len >> 24) & 0xFF);
  frame.insert(frame.end(),
               reinterpret_cast<const uint8_t *>(json.data()),
               reinterpret_cast<const uint8_t *>(json.data() + json.size()));
  return write_all(frame.data(), frame.size());
}

bool DiscordRPC::read_frame(int &opcode, std::string &json)
{
  if (handle_ == discord_ipc::kInvalidHandle)
  {
    return false;
  }

  uint8_t buffer[kReadChunk];
  const long n = discord_ipc::read_bytes(handle_, buffer, sizeof(buffer));
  if (n == 0 || n == -2)
  {
    close_connection();
    return false;
  }
  if (n < 0)
  {
    return false; // nothing available yet
  }
  read_buf_.append(reinterpret_cast<char *>(buffer), static_cast<size_t>(n));

  if (read_buf_.size() < 8)
  {
    return false;
  }
  const uint8_t *data = reinterpret_cast<const uint8_t *>(read_buf_.data());
  const uint32_t frame_op = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
                            | (static_cast<uint32_t>(data[2]) << 16)
                            | (static_cast<uint32_t>(data[3]) << 24);
  const uint32_t frame_len = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8)
                             | (static_cast<uint32_t>(data[6]) << 16)
                             | (static_cast<uint32_t>(data[7]) << 24);
  if (read_buf_.size() < 8 + frame_len)
  {
    return false;
  }

  opcode = static_cast<int>(frame_op);
  json.assign(read_buf_, 8, frame_len);
  read_buf_.erase(0, 8 + frame_len);
  return true;
}

void DiscordRPC::handle_frame(int opcode, const std::string &json)
{
  switch (opcode)
  {
  case 0:
  case 1:
  {
    if (json.find("\"evt\":\"READY\"") != std::string::npos
        || json.find("\"heartbeat_interval\"") != std::string::npos)
    {
      long long interval = 0;
      if (json_int_field(json, "heartbeat_interval", interval) && interval > 0)
      {
        heartbeat_interval_ms_ = static_cast<int>(interval);
      }
      state_ = CONNECTED;
      last_error_.clear();
      return;
    }
    // Discord rejects a bad request (unknown asset key, unknown client id, ...)
    // with an error event. Reporting it is the difference between "presence is
    // broken somehow" and knowing exactly which asset still needs uploading.
    if (json.find("\"evt\":\"ERROR\"") != std::string::npos)
    {
      const std::string message = json_string_field(json, "message");
      long long code = 0;
      if (json_int_field(json, "code", code))
      {
        last_error_ = message.empty() ? "code " + std::to_string(code)
                                      : message + " (code " + std::to_string(code) + ")";
      }
      else
      {
        last_error_ = message;
      }
    }
    break;
  }
  case 2: // CLOSE
    close_connection();
    break;
  case 4: // PONG
    break;
  default:
    break;
  }
}

void DiscordRPC::send_activity(const jot_discord::Activity &activity)
{
  pending_ = activity;
  has_pending_ = true;
  send_pending();
}

void DiscordRPC::send_pending()
{
  if (!has_pending_ || state_ != CONNECTED)
  {
    return;
  }
  const std::string payload =
      jot_discord::set_activity_payload(pending_, make_nonce(), pid_value());
  send_frame(1, payload);
}

void DiscordRPC::clear_presence()
{
  has_pending_ = false;
  pending_ = jot_discord::Activity{};
  if (state_ == CONNECTED)
  {
    // SET_ACTIVITY with no activity object clears the profile.
    const std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + make_nonce()
                                + "\",\"args\":{\"pid\":" + std::to_string(pid_value()) + "}}";
    send_frame(1, payload);
  }
}
