#include "discord_rpc.h"

#include <ctime>

DiscordRPC::DiscordRPC()
    : sockfd_(-1), state_(DISCONNECTED), started_at_(time(nullptr)),
      last_heartbeat_ms_(0), last_presence_update_ms_(0),
      last_connect_attempt_ms_(0), heartbeat_interval_ms_(30000),
      nonce_counter_(0) {}

DiscordRPC::~DiscordRPC() { close_socket(); }

void DiscordRPC::poll(long long) {}

void DiscordRPC::update_presence(const std::string &details,
                                 const std::string &state) {
  pending_presence_details_ = details;
  pending_presence_state_ = state;
}

void DiscordRPC::clear_presence() {
  pending_presence_details_.clear();
  pending_presence_state_.clear();
}

void DiscordRPC::disconnect() { close_socket(); }

bool DiscordRPC::find_and_connect_socket() { return false; }
void DiscordRPC::send_handshake() {}
void DiscordRPC::close_socket() { state_ = DISCONNECTED; }
bool DiscordRPC::write_all(const uint8_t *, size_t) { return false; }
bool DiscordRPC::send_frame(int, const std::string &) { return false; }
bool DiscordRPC::read_frame(int &, std::string &) { return false; }
void DiscordRPC::handle_frame(int, const std::string &) {}
void DiscordRPC::send_presence(const std::string &, const std::string &) {}

std::string DiscordRPC::make_nonce() {
  return "win32-" + std::to_string(++nonce_counter_);
}
