#include "discord_rpc.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>

namespace {

constexpr const char *kDiscordAppId = "1513610110256021524";
constexpr long long kReconnectDelayMs = 30000;
constexpr long long kPresenceThrottleMs = 15000;
constexpr long long kHandshakeTimeoutMs = 10000;

std::string json_escape(const std::string &value) {
  std::string result;
  result.reserve(value.size() * 2);
  for (char c : value) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
      break;
    }
  }
  return result;
}

bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

std::string socket_path_for_index(int index) {
#if defined(__APPLE__)
  const char *temp = getenv("TMPDIR");
  if (!temp || !*temp)
    temp = "/tmp";
  return std::string(temp) + "/discord-ipc-" + std::to_string(index);
#else
  std::string base;
  const char *xdg = getenv("XDG_RUNTIME_DIR");
  if (xdg && *xdg) {
    base = xdg;
  } else {
    const char *snap = getenv("SNAP_USER_COMMON");
    if (snap && *snap) {
      base = snap;
    } else {
      const char *tmpdir = getenv("TMPDIR");
      if (tmpdir && *tmpdir) {
        base = tmpdir;
      } else {
        base = "/tmp";
      }
    }
  }
  return base + "/discord-ipc-" + std::to_string(index);
#endif
}

long long monotonic_ms_now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void install_sigpipe_handler() {
  static bool installed = false;
  if (!installed) {
    signal(SIGPIPE, SIG_IGN);
    installed = true;
  }
}

} // namespace

DiscordRPC::DiscordRPC()
    : sockfd_(-1), state_(DISCONNECTED), last_heartbeat_ms_(0),
      last_presence_update_ms_(0), last_connect_attempt_ms_(0),
      started_at_(time(nullptr)),
      heartbeat_interval_ms_(30000), nonce_counter_(0) {}

DiscordRPC::~DiscordRPC() { close_socket(); }

bool DiscordRPC::find_and_connect_socket() {
  for (int i = 0; i <= 9; i++) {
    std::string path = socket_path_for_index(i);
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
      continue;
    }
    if (!S_ISSOCK(st.st_mode)) {
      continue;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
      continue;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr),
                  sizeof(addr)) < 0) {
      close(fd);
      continue;
    }

    sockfd_ = fd;
    return true;
  }
  return false;
}

void DiscordRPC::close_socket() {
  // Non-recursive teardown. Closes the local socket fd and resets
  // in-memory state. NEVER calls write_all(), send_frame(), or
  // disconnect() (which would re-enter this path and SIGSEGV from
  // stack overflow). Sending a CLOSE frame is optional in the
  // Discord IPC protocol; closing the local fd is sufficient.
  if (sockfd_ >= 0) {
    close(sockfd_);
    sockfd_ = -1;
  }
  state_ = DISCONNECTED;
  read_buf_.clear();
  write_buf_.clear();
}

void DiscordRPC::disconnect() {
  // Public disconnect path. Closes the local socket without trying
  // to send a CLOSE frame (sending on a half-broken socket is what
  // caused the recursive disconnect -> write_all -> disconnect
  // SIGSEGV).
  close_socket();
}

void DiscordRPC::send_handshake() {
  std::string json = "{\"v\":1,\"client_id\":\"" + std::string(kDiscordAppId) +
                     "\"}";
  send_frame(0, json); // HANDSHAKE
}

void DiscordRPC::poll(long long now_ms) {
  switch (state_) {
  case DISCONNECTED: {
    if (last_connect_attempt_ms_ > 0 &&
        now_ms - last_connect_attempt_ms_ < kReconnectDelayMs) {
      return;
    }
    last_connect_attempt_ms_ = now_ms;

    if (!find_and_connect_socket()) {
      return;
    }

    install_sigpipe_handler();
    set_nonblocking(sockfd_);
    send_handshake();
    state_ = HANDSHAKING;
    last_heartbeat_ms_ = now_ms;
    break;
  }

  case HANDSHAKING: {
    int opcode = -1;
    std::string json;
    while (read_frame(opcode, json)) {
      handle_frame(opcode, json);
      if (state_ == CONNECTED) {
        last_heartbeat_ms_ = now_ms;
        if (!pending_presence_details_.empty() ||
            !pending_presence_state_.empty()) {
          send_presence(pending_presence_details_, pending_presence_state_);
        }
        return;
      }
    }
    if (now_ms - last_connect_attempt_ms_ > kHandshakeTimeoutMs) {
      // Handshake timed out. close_socket() is non-recursive and
      // safe to call from the poll() loop.
      close_socket();
    }
    break;
  }

  case CONNECTED: {
    int opcode = -1;
    std::string json;
    while (read_frame(opcode, json)) {
      handle_frame(opcode, json);
    }
    if (state_ != CONNECTED) {
      return;
    }
  if (now_ms - last_heartbeat_ms_ >= heartbeat_interval_ms_) {
    send_frame(3, ""); // PING
    last_heartbeat_ms_ = now_ms;
  }
    break;
  }
  }
}

bool DiscordRPC::write_all(const uint8_t *data, size_t len) {
  if (sockfd_ < 0) {
    return false;
  }
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = write(sockfd_, data + sent, len - sent);
    if (n <= 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(1000);
        continue;
      }
      // Unrecoverable write error. Close the socket directly so we
      // do NOT recurse back through send_frame / write_all / disconnect
      // (the bug that caused the SIGSEGV). The next poll() will
      // reconnect if Discord is available again.
      close_socket();
      return false;
    }
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool DiscordRPC::send_frame(int opcode, const std::string &json) {
  if (sockfd_ < 0) {
    return false;
  }

  uint32_t op = static_cast<uint32_t>(opcode);
  uint32_t len = static_cast<uint32_t>(json.size());

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

bool DiscordRPC::read_frame(int &opcode, std::string &json) {
  if (sockfd_ < 0) {
    return false;
  }

  uint8_t buf[4096];
  ssize_t n = read(sockfd_, buf, sizeof(buf));
  if (n <= 0) {
    if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      // n == 0 means EOF / peer closed. Any other non-recoverable
      // error means the socket is gone. Use close_socket() so we
      // do not recurse through send_frame / write_all.
      close_socket();
    }
    return false;
  }

  read_buf_.append(reinterpret_cast<char *>(buf), static_cast<size_t>(n));

  if (read_buf_.size() < 8) {
    return false;
  }

  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(read_buf_.data());
  uint32_t frame_op = static_cast<uint32_t>(data[0]) |
                      (static_cast<uint32_t>(data[1]) << 8) |
                      (static_cast<uint32_t>(data[2]) << 16) |
                      (static_cast<uint32_t>(data[3]) << 24);
  uint32_t frame_len = static_cast<uint32_t>(data[4]) |
                       (static_cast<uint32_t>(data[5]) << 8) |
                       (static_cast<uint32_t>(data[6]) << 16) |
                       (static_cast<uint32_t>(data[7]) << 24);

  if (read_buf_.size() < 8 + frame_len) {
    return false;
  }

  opcode = static_cast<int>(frame_op);
  json.assign(read_buf_, 8, frame_len);
  read_buf_.erase(0, 8 + frame_len);

  return true;
}

void DiscordRPC::handle_frame(int opcode, const std::string &json) {
  switch (opcode) {
  case 0:
  case 1: { // HANDSHAKE response / FRAME with READY
    size_t ready_pos = json.find("\"evt\":\"READY\"");
    size_t hb_pos = json.find("\"heartbeat_interval\"");
    if (ready_pos != std::string::npos || hb_pos != std::string::npos) {
      if (hb_pos != std::string::npos) {
        size_t colon = json.find(':', hb_pos);
        if (colon != std::string::npos) {
          size_t val_start = colon + 1;
          while (val_start < json.size() &&
                 (json[val_start] == ' ' || json[val_start] == '\t')) {
            val_start++;
          }
          std::string num;
          while (val_start < json.size() && json[val_start] >= '0' &&
                 json[val_start] <= '9') {
            num += json[val_start];
            val_start++;
          }
          if (!num.empty()) {
            heartbeat_interval_ms_ = std::stoi(num);
          }
        }
      }
      state_ = CONNECTED;
    }
    break;
  }
  case 2: // CLOSE
    // Peer asked us to close. close_socket() is non-recursive.
    close_socket();
    break;
  case 4: // PONG
    break;
  default:
    break;
  }
}

void DiscordRPC::send_presence(const std::string &details,
                               const std::string &state) {
  pending_presence_details_ = details;
  pending_presence_state_ = state;

  if (state_ != CONNECTED) {
    return;
  }

  std::ostringstream json;
  json << "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" << make_nonce()
       << "\",\"args\":{\"pid\":" << getpid() << ",\"activity\":{"
       << "\"details\":\"" << json_escape(details) << "\""
       << ",\"state\":\"" << json_escape(state) << "\""
       << ",\"assets\":{"
       << "\"large_image\":\"jot\""
       << ",\"large_text\":\"jot editor\""
       << "}"
       << ",\"timestamps\":{\"start\":" << started_at_ << "}"
       << "}}}";

  if (send_frame(1, json.str())) { // FRAME
    last_presence_update_ms_ = monotonic_ms_now();
  }
}

void DiscordRPC::update_presence(const std::string &details,
                                 const std::string &state) {
  long long now = monotonic_ms_now();
  if (last_presence_update_ms_ > 0 &&
      now - last_presence_update_ms_ < kPresenceThrottleMs &&
      details == pending_presence_details_ &&
      state == pending_presence_state_) {
    return;
  }
  send_presence(details, state);
}

void DiscordRPC::clear_presence() {
  if (state_ == CONNECTED) {
    std::ostringstream json;
    json << "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" << make_nonce()
         << "\",\"args\":{\"pid\":" << getpid() << "}}";
    send_frame(1, json.str()); // FRAME
  }
  pending_presence_details_.clear();
  pending_presence_state_.clear();
  last_presence_update_ms_ = 0;
}

std::string DiscordRPC::make_nonce() {
  nonce_counter_++;
  return std::to_string(nonce_counter_);
}
