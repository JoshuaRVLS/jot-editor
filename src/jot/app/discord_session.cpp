// Discord Rich Presence session: turns live editor state into presence content
// and drives the IPC client (discord_rpc.cpp), plus the :discord command.
//
// Kept apart from editor.cpp so the presence port stays one readable unit: the
// pure content rules live in discord_presence.cpp, the wire protocol in
// discord_rpc.cpp, and this file is the glue (config, editor state, idle timer,
// workspace rules).
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

void Editor::discord_note_focus(bool focused, long long now_ms)
{
  discord_unfocused_since_ms = focused ? 0 : now_ms;
  if (!focused || !discord_idle_cleared)
  {
    // Ordinary typing (which also lands here) must not force a resend: only a
    // return from an idle stretch that actually cleared the presence does.
    return;
  }
  // The profile was cleared while away, so the content is unchanged from the
  // client's point of view; clearing the signature is what makes the next poll
  // restore it instead of taking the "nothing changed" shortcut.
  discord_idle_cleared = false;
  discord_last_signature.clear();
}

bool Editor::discord_workspace_excluded()
{
  const std::vector<std::string> patterns = config.get_list("discord_exclude_workspaces");
  if (patterns.empty() || root_dir.empty())
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
      if (std::regex_search(root_dir, std::regex(pattern)))
      {
        return true;
      }
    }
    catch (const std::regex_error &)
    {
      // A malformed pattern must not disable presence silently: report it once
      // through the status chip and ignore the rule.
      discord_pattern_error = pattern;
    }
  }
  return false;
}

const std::string &Editor::discord_repository_remote()
{
  if (root_dir != discord_remote_root || discord_remote_url.empty())
  {
    const long long now = jot_discord::monotonic_ms();
    if (root_dir != discord_remote_root || now - discord_remote_fetched_ms > kRemoteRefreshMs)
    {
      discord_remote_root = root_dir;
      discord_remote_fetched_ms = now;
      discord_remote_url.clear();
      if (has_git_repo())
      {
        discord_remote_url = trimmed(run_git_capture("remote get-url origin 2>/dev/null"));
      }
    }
  }
  return discord_remote_url;
}

long long Editor::discord_buffer_size(const FileBuffer &buf)
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

long long Editor::discord_error_count(const std::string &filepath)
{
  if (filepath.empty())
  {
    return 0;
  }
  const auto count_for = [this](const std::string &key) -> long long
  {
    const auto it = lsp_diag_slices_.find(key);
    if (it == lsp_diag_slices_.end())
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

jot_discord::TemplateContext Editor::discord_template_context()
{
  jot_discord::TemplateContext ctx;
  ctx.app_name = "jot";

  // Workspace. jot has no multi-root workspaces, so the "workspace" and the
  // "folder" are the same thing; {workspace_and_folder} therefore collapses to
  // the name instead of upstream's "Workspace - Folder".
  if (!root_dir.empty() && root_dir != ".")
  {
    ctx.workspace = file_name_of(root_dir);
    ctx.workspace_folder = ctx.workspace;
    ctx.workspace_and_folder = ctx.workspace;
    ctx.has_workspace = true;
  }
  else
  {
    ctx.workspace = config.get("discord_lower_details_no_workspace", "No workspace");
    ctx.workspace_folder = ctx.workspace;
    ctx.workspace_and_folder = ctx.workspace;
  }

  if (has_git_repo())
  {
    ctx.has_git = true;
    ctx.git_branch = git_branch;
    ctx.git_repo_name = jot_discord::repository_name_from_url(discord_repository_remote());
    if (ctx.git_repo_name.empty())
    {
      ctx.git_repo_name = ctx.workspace;
    }
  }

  if (!buffers.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size())
  {
    const FileBuffer &buf = buffers[current_buffer];
    if (!buf.filepath.empty())
    {
      ctx.has_file = true;
      ctx.file_name = file_name_of(buf.filepath);
      ctx.dir_name = file_name_of(fs::path(buf.filepath).parent_path().string());
      ctx.full_dir_name = relative_directory(buf.filepath, root_dir);
      ctx.current_line = buf.cursor.y + 1;
      ctx.current_column = buf.cursor.x + 1;
      ctx.total_lines = buf.line_count();
      ctx.file_size = discord_buffer_size(buf);
      ctx.current_errors = discord_error_count(buf.filepath);
      ctx.language = jot_discord::resolve_file_icon(buf.filepath, buf.syntax_language_label);
    }
  }
  return ctx;
}

jot_discord::PresenceState Editor::discord_presence_state()
{
  for (const DebuggerSessionState &session : debugger_session_state)
  {
    if (session.running || session.stopped)
    {
      return jot_discord::PresenceState::Debugging;
    }
  }
  bool has_file = !buffers.empty() && current_buffer >= 0 && current_buffer < (int)buffers.size()
                  && !buffers[current_buffer].filepath.empty();
  return has_file ? jot_discord::PresenceState::Editing : jot_discord::PresenceState::Idling;
}

jot_discord::PresenceOptions Editor::discord_presence_options()
{
  jot_discord::PresenceOptions options;
  options.details_idling = config.get("discord_details_idling", options.details_idling);
  options.details_editing = config.get("discord_details_editing", options.details_editing);
  options.details_debugging = config.get("discord_details_debugging", options.details_debugging);
  options.lower_details_idling =
      config.get("discord_lower_details_idling", options.lower_details_idling);
  options.lower_details_editing =
      config.get("discord_lower_details_editing", options.lower_details_editing);
  options.lower_details_debugging =
      config.get("discord_lower_details_debugging", options.lower_details_debugging);
  options.lower_details_no_workspace =
      config.get("discord_lower_details_no_workspace", options.lower_details_no_workspace);
  options.large_image = config.get("discord_large_image", options.large_image);
  options.large_image_idling = config.get("discord_large_image_idling", options.large_image_idling);
  options.small_image = config.get("discord_small_image", options.small_image);
  options.app_image_key = config.get("discord_app_image", options.app_image_key);
  options.idle_image_key = config.get("discord_idle_image", options.idle_image_key);
  options.debug_image_key = config.get("discord_debug_image", options.debug_image_key);
  options.swap_big_and_small_image = config.get_bool("discord_swap_images", false);
  options.remove_details = config.get_bool("discord_remove_details", false);
  options.remove_lower_details = config.get_bool("discord_remove_lower_details", false);
  options.remove_timestamp = config.get_bool("discord_remove_timestamp", false);
  options.remove_remote_repository = config.get_bool("discord_remove_repository_button", false);
  return options;
}

void Editor::discord_set_status(const std::string &status)
{
  if (discord_status == status)
  {
    return;
  }
  discord_status = status;
  // The status line is immediate-mode: a change has to ask for a repaint or the
  // chip stays stale until something else happens to redraw the bottom rows.
  needs_redraw = true;
}

void Editor::poll_discord_rpc(long long now_ms)
{
  const bool enabled = config.get_bool("discord_rpc", true);
  if (!enabled || discord_workspace_excluded())
  {
    if (discord_rpc.is_connected() || discord_rpc.has_pending_activity())
    {
      discord_rpc.clear_presence();
    }
    discord_rpc.disconnect();
    discord_set_status(enabled ? "excluded" : "off");
    return;
  }

  discord_rpc.set_app_id(config.get("discord_app_id", kDefaultAppId));
  discord_rpc.poll(now_ms);

  if (!discord_rpc.last_error().empty())
  {
    discord_set_status("error");
  }
  else if (discord_rpc.is_connected())
  {
    discord_set_status("on");
  }
  else
  {
    discord_set_status("connecting");
  }

  // Idle: upstream clears the presence when the window has been unfocused for
  // the configured number of seconds, and restores it on return. A keystroke
  // also clears the marker, so a terminal that never reports focus (or reports
  // it wrongly) cannot leave the presence stuck in the cleared state.
  const int idle_timeout_s = std::clamp(config.get_int("discord_idle_timeout", 0), 0, 86400);
  if (idle_timeout_s > 0 && discord_unfocused_since_ms > 0
      && now_ms - discord_unfocused_since_ms >= idle_timeout_s * 1000LL)
  {
    if (!discord_idle_cleared)
    {
      discord_idle_cleared = true;
      discord_rpc.clear_presence();
      discord_set_status("idle");
    }
    return;
  }
  if (discord_idle_cleared)
  {
    return;
  }

  if (discord_presence_start_ms <= 0)
  {
    discord_presence_start_ms = (long long)std::time(nullptr);
  }
  const jot_discord::Activity activity = jot_discord::build_activity(discord_presence_options(),
                                                                     discord_presence_state(),
                                                                     discord_template_context(),
                                                                     discord_repository_remote(),
                                                                     discord_presence_start_ms);

  // Signature of what the profile shows: the two text rows plus the artwork,
  // so switching files (or languages) updates without spamming Discord while
  // typing. The RPC layer replays the latest activity after a reconnect.
  const std::string signature = activity.details + "\x1f" + activity.state + "\x1f"
                                + activity.large_image_key + "\x1f" + activity.small_image_key
                                + "\x1f" + (activity.has_button ? activity.button_url : "");
  if (signature == discord_last_signature)
  {
    return;
  }
  if (discord_last_signature.empty() || now_ms - discord_last_send_ms >= kPresenceThrottleMs)
  {
    discord_last_signature = signature;
    discord_last_send_ms = now_ms;
    discord_rpc.send_activity(activity);
  }
}

std::string Editor::discord_command(const std::string &argument)
{
  const std::string arg = trimmed(argument);
  const auto report = [this](const std::string &text)
  {
    set_message(text);
    return text;
  };

  if (arg == "enable" || arg == "on")
  {
    config.set("discord_rpc", "true");
    config.save();
    discord_presence_start_ms = (long long)std::time(nullptr);
    discord_last_signature.clear();
    discord_rpc.disconnect();
    needs_redraw = true;
    return report("Discord presence enabled");
  }
  if (arg == "disable" || arg == "off")
  {
    config.set("discord_rpc", "false");
    config.save();
    discord_rpc.clear_presence();
    discord_rpc.disconnect();
    discord_set_status("off");
    return report("Discord presence disabled");
  }
  if (arg == "reconnect")
  {
    discord_rpc.disconnect();
    discord_last_signature.clear();
    needs_redraw = true;
    return report("Reconnecting to Discord");
  }
  if (arg == "disconnect")
  {
    discord_rpc.clear_presence();
    discord_rpc.disconnect();
    discord_set_status("off");
    return report("Disconnected from Discord");
  }
  if (arg == "assets")
  {
    // Discord hides an image whose asset key was never uploaded, and it does
    // not reliably report that -- the profile just shows text with no artwork.
    // Listing the exact keys the current activity asks for is what turns that
    // into a checklist against the developer portal.
    const jot_discord::PresenceOptions options = discord_presence_options();
    const jot_discord::TemplateContext ctx = discord_template_context();
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
    const std::string app_id = config.get("discord_app_id", kDefaultAppId);
    return report("Discord assets needed: " + list + " -- upload them at "
                  + "discord.com/developers/applications/" + app_id + "/rich-presence/assets"
                  + " (key = file name without .png; see "
                    "packaging/discord-presence/ASSETS.md)");
  }

  // status (also the bare ":discord"): everything needed to tell a missing
  // client apart from a rejected asset key.
  std::string text;
  const bool enabled = config.get_bool("discord_rpc", true);
  text += enabled ? "Discord presence: " : "Discord presence: disabled";
  if (enabled)
  {
    text += discord_rpc.is_connected()                           ? "connected"
            : discord_rpc.get_state() == DiscordRPC::HANDSHAKING ? "connecting"
                                                                 : "not connected";
    if (discord_workspace_excluded())
    {
      text += " (workspace excluded)";
    }
  }
  if (!discord_rpc.last_error().empty())
  {
    text += " -- last error: " + discord_rpc.last_error();
    // An asset rejection is the one error a user can fix themselves, and the
    // fix is not obvious from Discord's wording.
    const std::string lowered = [&]
    {
      std::string value = discord_rpc.last_error();
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
  if (!discord_pattern_error.empty())
  {
    text += " -- bad exclude pattern: " + discord_pattern_error;
  }
  if (!discord_rpc.probed_endpoints().empty())
  {
    text += " -- endpoints: " + discord_rpc.probed_endpoints().front();
  }
  else if (enabled)
  {
    text += " -- no discord-ipc socket found (is Discord running?)";
  }
  if (enabled)
  {
    text += " -- app id " + config.get("discord_app_id", kDefaultAppId);
  }
  return report(text);
}
