#ifndef JOT_EDITOR_DISCORD_CONTROLLER_H
#define JOT_EDITOR_DISCORD_CONTROLLER_H

#include "tools/discord_rpc.h" // DiscordRPC (which pulls in the presence model)
#include <string>

class Editor;
struct FileBuffer;

// The Rich Presence session: the IPC client, the two clocks it runs on (the 1s
// poll and the focus report) and the content rules that turn live editor state
// into a Discord activity, plus the `:discord` command and the status the
// status-line chip shows.
//
// This used to be ten EditorState members plus eleven Editor methods spread
// over editor.h and app/discord_session.cpp. It is the second of the editor's
// collaborators (after SearchController): the state and the behaviour live
// together here, Editor owns one, and the session reaches the shared editor
// state through the Editor it was built with.
//
// The pure content model (placeholder templates, asset-key tables, the
// activity shape) stays in tools/discord_presence.* and knows nothing about the
// editor; this class is the glue that feeds it.
class DiscordController
{
public:
  explicit DiscordController(Editor &editor) : editor_(editor) {}

  // --- Driving the session ---
  // The 1s editor timer: reconnects, throttles, and sends the presence when
  // what it shows changed. `now_ms` is jot_discord::monotonic_ms().
  void poll(long long now_ms);
  // Terminal focus in/out. Landing back from an idle stretch that cleared the
  // presence forgets the sent signature so the next poll restores it.
  void note_focus(bool focused, long long now_ms);
  // Backing of the `:discord` command (enable/disable/reconnect/disconnect/
  // assets/status) and what the Lua command surface returns.
  std::string command(const std::string &argument);

  // --- What the status line reports ---
  const std::string &status() const { return status_; }
  bool idle_cleared() const { return idle_cleared_; }
  // The IPC client, for the diagnostics the status chip and the tests ask
  // about (connected? pending activity? last error?).
  const DiscordRPC &rpc() const { return rpc_; }

private:
  // The content rules: what the presence should say about the editor right
  // now. Each one reads the shared editor state through editor_.
  bool workspace_excluded();
  const std::string &repository_remote();
  long long buffer_size(const FileBuffer &buf);
  long long error_count(const std::string &filepath);
  jot_discord::TemplateContext template_context();
  jot_discord::PresenceState presence_state();
  jot_discord::PresenceOptions presence_options();
  void set_status(const std::string &status);

  Editor &editor_;
  DiscordRPC rpc_;

  // Signature of what the profile shows (the two text rows plus the artwork,
  // so a language switch counts). The presence is only re-sent when it changes.
  std::string last_signature_;
  // Short state of the session: "on", "off", "connecting", "idle", "excluded",
  // "error". Read by the status-line chip and reported by `:discord status`.
  std::string status_;
  // The last exclude pattern that failed to compile, reported once so a bad
  // rule cannot disable presence silently.
  std::string pattern_error_;
  std::string remote_root_;
  std::string remote_url_;
  long long presence_start_ms_ = 0;
  long long last_send_ms_ = 0;
  long long unfocused_since_ms_ = 0;
  long long remote_fetched_ms_ = 0;
  bool idle_cleared_ = false;
};

#endif
