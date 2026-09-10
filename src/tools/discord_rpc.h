#ifndef DISCORD_RPC_H
#define DISCORD_RPC_H

#include "discord_presence.h"
#include "discord_rpc_transport.h"

#include <cstdint>
#include <string>
#include <vector>

// Discord IPC client: connect / handshake / heartbeat / SET_ACTIVITY over the
// platform transport (discord_rpc_transport.h). The presence *content* lives in
// discord_presence.h; this class only moves bytes and tracks connection state.
class DiscordRPC
{
public:
  enum State
  {
    DISCONNECTED,
    HANDSHAKING,
    CONNECTED
  };

  DiscordRPC();
  ~DiscordRPC();

  // Discord application the presence authenticates as (config: discord_app_id).
  // Discord displays that application's name, and every asset key must exist on
  // it, so this is a user-visible setting rather than a constant.
  void set_app_id(const std::string &app_id);

  void poll(long long now_ms);
  bool is_connected() const
  {
    return state_ == CONNECTED;
  }
  State get_state() const
  {
    return state_;
  }

  // Sends a full activity. The pending copy is kept so a reconnect (or a
  // handshake that finishes later) replays the latest presence instead of
  // waiting for the next editor update.
  void send_activity(const jot_discord::Activity &activity);
  // Clears the profile (SET_ACTIVITY without an activity) and drops the pending
  // presence, so it is not replayed until the editor sends again.
  void clear_presence();
  // Drops the session: the next poll reconnects with a fresh handshake.
  void disconnect();

  // Last protocol-level failure reported by Discord ("Invalid Asset", ...),
  // empty when there is none. Cleared once a handshake succeeds.
  const std::string &last_error() const
  {
    return last_error_;
  }
  // An activity is waiting to be (re)sent, e.g. queued before the handshake
  // finished.
  bool has_pending_activity() const
  {
    return has_pending_;
  }
  // Endpoints that existed during the last connect attempt, for diagnostics
  // (:discord status).
  const std::vector<std::string> &probed_endpoints() const
  {
    return probed_;
  }

private:
  discord_ipc::Handle handle_;
  State state_;
  long long last_heartbeat_ms_;
  long long last_connect_attempt_ms_;
  int heartbeat_interval_ms_;
  int nonce_counter_;
  std::string app_id_;
  std::string read_buf_;
  std::string last_error_;
  std::vector<std::string> probed_;
  jot_discord::Activity pending_;
  bool has_pending_ = false;

  bool find_and_connect();
  void send_handshake();
  // Closes the transport and resets session state. NEVER sends a frame (a write
  // on a half-broken socket is what used to recurse into disconnect()).
  void close_connection();
  bool write_all(const uint8_t *data, size_t length);
  bool send_frame(int opcode, const std::string &json);
  bool read_frame(int &opcode, std::string &json);
  void handle_frame(int opcode, const std::string &json);
  void send_pending();
  std::string make_nonce();
};

#endif // DISCORD_RPC_H
