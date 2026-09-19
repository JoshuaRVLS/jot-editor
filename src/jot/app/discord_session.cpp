// Discord Rich Presence session (jot/editor/discord_controller.h): turns live
// editor state into presence content and drives the IPC client
// (discord_rpc.cpp), plus the :discord command and the status chip's state.
//
// Kept apart from editor.cpp so the presence port stays one readable unit: the
// pure content rules live in discord_presence.cpp, the wire protocol in
// discord_rpc.cpp, and this file is the glue (config, editor state, idle timer,
// workspace rules).
//
// The session used to be Editor members; it is a collaborator now, so the
// shared state it reads (buffers, config, the diagnostics and debugger models,
// the message line and the redraw flag) is reached through `editor_`.
#include "jot/editor/discord_controller.h"

#include "editor.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <regex>
#include <string>

namespace fs = std::filesystem;

namespace
{
  // jot's Discord application. The app's *name* is what Discord displays, so
  // users see "jot"; the asset keys referenced below must be uploaded to it
  // (packaging/discord-presence/ASSETS.md).

  constexpr const char *kDefaultAppId = "1513610110256021524";

  // Presence updates are throttled like upstream's 2s document throttle: fast
  // enough to feel live while typing, slow enough to never flood Discord.
  constexpr long long kPresenceThrottleMs = 2000;
  // The repository remote only changes when the user re-points origin, so it is
  // re-read rarely (a git subprocess per second would be wasteful).
  constexpr long long kRemoteRefreshMs = 300000;

  std::string trimmed(const std::string &text)
  {
    size_t start = 0;
    size_t end = text.size();
    while (start < end
           && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n'
               || text[start] == '\r'))
    {
      start++;
    }
    while (end > start
           && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n'
               || text[end - 1] == '\r'))
    {
      end--;
    }
    return text.substr(start, end - start);
  }

  std::string file_name_of(const std::string &path)
  {
    return fs::path(path).filename().string();
  }

  // Relative directory of `path` under `root`, without the file name, using
  // forward slashes (upstream feeds the same shape into {full_dir_name}).
  std::string relative_directory(const std::string &path, const std::string &root)
  {
    std::error_code ec;
    fs::path relative;
    if (!root.empty())
    {
      relative = fs::relative(path, root, ec);
      if (ec || relative.empty() || relative.native().rfind("..", 0) == 0)
      {
        relative = fs::path(path).parent_path();
      }
    }
    else
    {
      relative = fs::path(path).parent_path();
    }
    std::string out = relative.parent_path().generic_string();
    if (out == ".")
    {
      out.clear();
    }
    return out;
  }
} // namespace

void DiscordController::note_focus(bool focused, long long now_ms)
{
  unfocused_since_ms_ = focused ? 0 : now_ms;
  if (!focused || !idle_cleared_)
  {
    // Ordinary typing (which also lands here) must not force a resend: only a
    // return from an idle stretch that actually cleared the presence does.
    return;
  }
  // The profile was cleared while away, so the content is unchanged from the
  // client's point of view; clearing the signature is what makes the next poll
  // restore it instead of taking the "nothing changed" shortcut.
  idle_cleared_ = false;
  last_signature_.clear();
}

bool DiscordController::workspace_excluded()
{
  const std::vector<std::string> patterns =
      editor_.config.get_list("discord_exclude_workspaces");
  if (patterns.empty() || editor_.root_dir.empty())
  {
    return false;
  }
  for (const std::string &pattern : patterns)
  {
    if (pattern.empty())
    {
      continue;
    }
    try
    {
      if (std::regex_search(editor_.root_dir, std::regex(pattern)))
      {
        return true;
      }
    }
    catch (const std::regex_error &)
    {
      // A malformed pattern must not disable presence silently: report it once
      // through the status chip and ignore the rule.
      pattern_error_ = pattern;
    }
  }
  return false;
}

const std::string &DiscordController::repository_remote()
{
  if (editor_.root_dir != remote_root_ || remote_url_.empty())
  {
    const long long now = jot_discord::monotonic_ms();
    if (editor_.root_dir != remote_root_ || now - remote_fetched_ms_ > kRemoteRefreshMs)
    {
      remote_root_ = editor_.root_dir;
      remote_fetched_ms_ = now;
      remote_url_.clear();
      if (editor_.has_git_repo())
      {
        remote_url_ = trimmed(editor_.run_git_capture("remote get-url origin 2>/dev/null"));
      }
    }
  }
  return remote_url_;
}

long long DiscordController::buffer_size(const FileBuffer &buf)
{
  // An unsaved file has no meaningful size on disk, and re-reading the file
  // every second would be pointless work: the in-memory lines are the truth
  // for anything modified, the file itself otherwise.
  if (!buf.modified && !buf.filepath.empty())
  {
    std::error_code ec;
    const auto size = fs::file_size(buf.filepath, ec);
    if (!ec)
    {
      return static_cast<long long>(size);
    }
  }
  long long bytes = 0;
  for (int i = 0; i < (int)buf.line_count(); i++)
  {
    bytes += static_cast<long long>(buf.line(i).size()) + 1; // + newline
  }
  return bytes;
}

long long DiscordController::error_count(const std::string &filepath)
{
  if (filepath.empty())
  {
    return 0;
  }
  const auto count_for = [this](const std::string &key) -> long long
  {
    const auto it = editor_.lsp_diag_slices_.find(key);
    if (it == editor_.lsp_diag_slices_.end())
    {
      return -1;
    }
    long long errors = 0;
    for (const auto &per_server : it->second)
    {
      for (const Diagnostic &diagnostic : per_server.second)
      {
        if (diagnostic.severity == 1)
        {
          errors++;
        }
      }
    }
    return errors;
  };
  long long errors = count_for(filepath);
  if (errors < 0)
  {
    std::error_code ec;
    const fs::path absolute = fs::absolute(filepath, ec);
    if (!ec)
    {
      errors = count_for(absolute.lexically_normal().string());
    }
  }
  return errors < 0 ? 0 : errors;
}

jot_discord::TemplateContext DiscordController::template_context()
{
  jot_discord::TemplateContext ctx;
  ctx.app_name = "jot";

  // Workspace. jot has no multi-root workspaces, so the "workspace" and the
  // "folder" are the same thing; {workspace_and_folder} therefore collapses to
  // the name instead of upstream's "Workspace - Folder".
  if (!editor_.root_dir.empty() && editor_.root_dir != ".")
  {
    ctx.workspace = file_name_of(editor_.root_dir);
    ctx.workspace_folder = ctx.workspace;
    ctx.workspace_and_folder = ctx.workspace;
    ctx.has_workspace = true;
  }
  else
  {
    ctx.workspace = editor_.config.get("discord_lower_details_no_workspace", "No workspace");
    ctx.workspace_folder = ctx.workspace;
    ctx.workspace_and_folder = ctx.workspace;
  }

  if (editor_.has_git_repo())
  {
    ctx.has_git = true;
    ctx.git_branch = editor_.git_branch;
    ctx.git_repo_name = jot_discord::repository_name_from_url(repository_remote());
    if (ctx.git_repo_name.empty())
    {
      ctx.git_repo_name = ctx.workspace;
    }
  }

  if (!editor_.buffers.empty() && editor_.current_buffer >= 0
      && editor_.current_buffer < (int)editor_.buffers.size())
  {
    const FileBuffer &buf = editor_.buffers[editor_.current_buffer];
    if (!buf.filepath.empty())
    {
      ctx.has_file = true;
      ctx.file_name = file_name_of(buf.filepath);
      ctx.dir_name = file_name_of(fs::path(buf.filepath).parent_path().string());
      ctx.full_dir_name = relative_directory(buf.filepath, editor_.root_dir);
      ctx.current_line = buf.cursor.y + 1;
      ctx.current_column = buf.cursor.x + 1;
      ctx.total_lines = buf.line_count();
      ctx.file_size = buffer_size(buf);
      ctx.current_errors = error_count(buf.filepath);
      ctx.language = jot_discord::resolve_file_icon(buf.filepath, buf.syntax_language_label);
    }
  }
  return ctx;
}

jot_discord::PresenceState DiscordController::presence_state()
{
  for (const DebuggerSessionState &session : editor_.debugger_session_state)
  {
    if (session.running || session.stopped)
    {
      return jot_discord::PresenceState::Debugging;
    }
  }
  bool has_file = !editor_.buffers.empty() && editor_.current_buffer >= 0
                  && editor_.current_buffer < (int)editor_.buffers.size()
                  && !editor_.buffers[editor_.current_buffer].filepath.empty();
  return has_file ? jot_discord::PresenceState::Editing : jot_discord::PresenceState::Idling;
}

jot_discord::PresenceOptions DiscordController::presence_options()
{
  jot_discord::PresenceOptions options;
  options.details_idling =
      editor_.config.get("discord_details_idling", options.details_idling);
  options.details_editing =
      editor_.config.get("discord_details_editing", options.details_editing);
  options.details_debugging =
      editor_.config.get("discord_details_debugging", options.details_debugging);
  options.lower_details_idling =
      editor_.config.get("discord_lower_details_idling", options.lower_details_idling);
  options.lower_details_editing =
      editor_.config.get("discord_lower_details_editing", options.lower_details_editing);
  options.lower_details_debugging =
      editor_.config.get("discord_lower_details_debugging", options.lower_details_debugging);
  options.lower_details_no_workspace =
      editor_.config.get("discord_lower_details_no_workspace", options.lower_details_no_workspace);
  options.large_image = editor_.config.get("discord_large_image", options.large_image);
  options.large_image_idling =
      editor_.config.get("discord_large_image_idling", options.large_image_idling);
  options.small_image = editor_.config.get("discord_small_image", options.small_image);
  options.app_image_key = editor_.config.get("discord_app_image", options.app_image_key);
  options.idle_image_key = editor_.config.get("discord_idle_image", options.idle_image_key);
  options.debug_image_key = editor_.config.get("discord_debug_image", options.debug_image_key);
  options.swap_big_and_small_image = editor_.config.get_bool("discord_swap_images", false);
  options.remove_details = editor_.config.get_bool("discord_remove_details", false);
  options.remove_lower_details = editor_.config.get_bool("discord_remove_lower_details", false);
  options.remove_timestamp = editor_.config.get_bool("discord_remove_timestamp", false);
  options.remove_remote_repository =
      editor_.config.get_bool("discord_remove_repository_button", false);
  return options;
}

void DiscordController::set_status(const std::string &status)
{
  if (status_ == status)
  {
    return;
  }
  status_ = status;
  // The status line is immediate-mode: a change has to ask for a repaint or the
  // chip stays stale until something else happens to redraw the bottom rows.
  editor_.needs_redraw = true;
}

void DiscordController::poll(long long now_ms)
{
  const bool enabled = editor_.config.get_bool("discord_rpc", true);
  if (!enabled || workspace_excluded())
  {
    if (rpc_.is_connected() || rpc_.has_pending_activity())
    {
      rpc_.clear_presence();
    }
    rpc_.disconnect();
    set_status(enabled ? "excluded" : "off");
    return;
  }

  rpc_.set_app_id(editor_.config.get("discord_app_id", kDefaultAppId));
  rpc_.poll(now_ms);

  if (!rpc_.last_error().empty())
  {
    set_status("error");
  }
  else if (rpc_.is_connected())
  {
    set_status("on");
  }
  else
  {
    set_status("connecting");
  }

  // Idle: upstream clears the presence when the window has been unfocused for
  // the configured number of seconds, and restores it on return. A keystroke
  // also clears the marker, so a terminal that never reports focus (or reports
  // it wrongly) cannot leave the presence stuck in the cleared state.
  const int idle_timeout_s =
      std::clamp(editor_.config.get_int("discord_idle_timeout", 0), 0, 86400);
  if (idle_timeout_s > 0 && unfocused_since_ms_ > 0
      && now_ms - unfocused_since_ms_ >= idle_timeout_s * 1000LL)
  {
    if (!idle_cleared_)
    {
      idle_cleared_ = true;
      rpc_.clear_presence();
      set_status("idle");
    }
    return;
  }
  if (idle_cleared_)
  {
    return;
  }

  if (presence_start_ms_ <= 0)
  {
    presence_start_ms_ = (long long)std::time(nullptr);
  }
  const jot_discord::Activity activity = jot_discord::build_activity(presence_options(),
                                                                     presence_state(),
                                                                     template_context(),
                                                                     repository_remote(),
                                                                     presence_start_ms_);

  // Signature of what the profile shows: the two text rows plus the artwork,
  // so switching files (or languages) updates without spamming Discord while
  // typing. The RPC layer replays the latest activity after a reconnect.
  const std::string signature = activity.details + "\x1f" + activity.state + "\x1f"
                                + activity.large_image_key + "\x1f" + activity.small_image_key
                                + "\x1f" + (activity.has_button ? activity.button_url : "");
  if (signature == last_signature_)
  {
    return;
  }
  if (last_signature_.empty() || now_ms - last_send_ms_ >= kPresenceThrottleMs)
  {
    last_signature_ = signature;
    last_send_ms_ = now_ms;
    rpc_.send_activity(activity);
  }
}

std::string DiscordController::command(const std::string &argument)
{
  const std::string arg = trimmed(argument);
  const auto report = [this](const std::string &text)
  {
    editor_.set_message(text);
    return text;
  };

  if (arg == "enable" || arg == "on")
  {
    editor_.config.set("discord_rpc", "true");
    editor_.config.save();
    presence_start_ms_ = (long long)std::time(nullptr);
    last_signature_.clear();
    rpc_.disconnect();
    editor_.needs_redraw = true;
    return report("Discord presence enabled");
  }
  if (arg == "disable" || arg == "off")
  {
    editor_.config.set("discord_rpc", "false");
    editor_.config.save();
    rpc_.clear_presence();
    rpc_.disconnect();
    set_status("off");
    return report("Discord presence disabled");
  }
  if (arg == "reconnect")
  {
    rpc_.disconnect();
    last_signature_.clear();
    editor_.needs_redraw = true;
    return report("Reconnecting to Discord");
  }
  if (arg == "disconnect")
  {
    rpc_.clear_presence();
    rpc_.disconnect();
    set_status("off");
    return report("Disconnected from Discord");
  }
  if (arg == "assets")
  {
    // Discord hides an image whose asset key was never uploaded, and it does
    // not reliably report that -- the profile just shows text with no artwork.
    // Listing the exact keys the current activity asks for is what turns that
    // into a checklist against the developer portal.
    const jot_discord::PresenceOptions options = presence_options();
    const jot_discord::TemplateContext ctx = template_context();
    std::vector<std::string> keys;
    const auto add = [&keys](const std::string &key)
    {
      if (key.empty())
      {
        return;
      }
      for (const std::string &existing : keys)
      {
        if (existing == key)
        {
          return;
        }
      }
      keys.push_back(key);
    };
    if (ctx.has_file)
    {
      add(jot_discord::resolve_file_icon(ctx.file_name, ctx.language));
    }
    else
    {
      add(options.idle_image_key);
    }
    add(options.app_image_key);
    add(options.debug_image_key);

    std::string list;
    for (const std::string &key : keys)
    {
      list += list.empty() ? key : ", " + key;
    }
    const std::string app_id = editor_.config.get("discord_app_id", kDefaultAppId);
    return report("Discord assets needed: " + list + " -- upload them at "
                  + "discord.com/developers/applications/" + app_id + "/rich-presence/assets"
                  + " (key = file name without .png; see "
                    "packaging/discord-presence/ASSETS.md)");
  }

  // status (also the bare ":discord"): everything needed to tell a missing
  // client apart from a rejected asset key.
  std::string text;
  const bool enabled = editor_.config.get_bool("discord_rpc", true);
  text += enabled ? "Discord presence: " : "Discord presence: disabled";
  if (enabled)
  {
    text += rpc_.is_connected()                           ? "connected"
            : rpc_.get_state() == DiscordRPC::HANDSHAKING ? "connecting"
                                                          : "not connected";
    if (workspace_excluded())
    {
      text += " (workspace excluded)";
    }
  }
  if (!rpc_.last_error().empty())
  {
    text += " -- last error: " + rpc_.last_error();
    // An asset rejection is the one error a user can fix themselves, and the
    // fix is not obvious from Discord's wording.
    const std::string lowered = [&]
    {
      std::string value = rpc_.last_error();
      std::transform(value.begin(),
                     value.end(),
                     value.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      return value;
    }();
    if (lowered.find("asset") != std::string::npos)
    {
      text += " (upload it: :discord assets)";
    }
  }
  if (!pattern_error_.empty())
  {
    text += " -- bad exclude pattern: " + pattern_error_;
  }
  if (!rpc_.probed_endpoints().empty())
  {
    text += " -- endpoints: " + rpc_.probed_endpoints().front();
  }
  else if (enabled)
  {
    text += " -- no discord-ipc socket found (is Discord running?)";
  }
  if (enabled)
  {
    text += " -- app id " + editor_.config.get("discord_app_id", kDefaultAppId);
  }
  return report(text);
}
