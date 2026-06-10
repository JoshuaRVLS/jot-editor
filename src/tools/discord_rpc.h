#ifndef DISCORD_RPC_H
#define DISCORD_RPC_H

#include <cstdint>
#include <string>
#include <vector>

class DiscordRPC {
public:
  enum State { DISCONNECTED, HANDSHAKING, CONNECTED };

  DiscordRPC();
  ~DiscordRPC();

  void poll(long long now_ms);
  bool is_connected() const { return state_ == CONNECTED; }
  State get_state() const { return state_; }

  void update_presence(const std::string &details, const std::string &state);
  void clear_presence();
  void disconnect();

private:
  int sockfd_;
  State state_;
  long long last_heartbeat_ms_;
  long long last_presence_update_ms_;
  long long last_connect_attempt_ms_;
  int heartbeat_interval_ms_;
  int nonce_counter_;
  std::string read_buf_;
  std::vector<uint8_t> write_buf_;
  std::string pending_presence_details_;
  std::string pending_presence_state_;

  bool find_and_connect_socket();
  void send_handshake();
  // close_socket() closes the local socket file descriptor and
  // resets in-memory state. It MUST NOT send any frame or call
  // write_all() / send_frame() / disconnect() (which would
  // re-enter the close path and SIGSEGV from stack overflow).
  void close_socket();
  bool write_all(const uint8_t *data, size_t len);
  bool send_frame(int opcode, const std::string &json);
  bool read_frame(int &opcode, std::string &json);
  void handle_frame(int opcode, const std::string &json);
  void send_presence(const std::string &details, const std::string &state);
  std::string make_nonce();
};

#endif
