#ifndef DISCORD_PRESENCE_H
#define DISCORD_PRESENCE_H

// Rich Presence content model: everything that decides *what* jot shows on a
// Discord profile, with no sockets or editor state involved, so it can be unit
// tested directly.
//
// Ported from iCrawl/discord-vscode (MIT -- see packaging/discord-presence/
// LICENSE): the template/placeholder system, the idling / editing / debugging
// states, the asset-key resolution tables and the "View Repository" button.
// The placeholder names match upstream exactly, so config strings can be
// copied from the extension's documentation unchanged.

#include <cstdint>
#include <string>

namespace jot_discord
{
  // Monotonic milliseconds: the clock every timing decision in the presence
  // path uses (update throttling, idle timeouts). Exposed so the editor can
  // stamp events with it and tests can drive the session with their own
  // readings instead of mixing in wall-clock time.
  long long monotonic_ms();

  // --- Asset keys ---------------------------------------------------------

  // Asset key for a file, from the vendored name table (upstream's
  // KNOWN_EXTENSIONS). Walks the table in order and returns the first match,
  // which is how specific names beat generic extensions ("package.json" wins
  // over ".json"). A table key is either a literal filename suffix (".rs") or
  // a "/regex/flags" rule; like upstream, a slash-wrapped key only counts as a
  // regex when it carries at least one flag character.
  std::string resolve_asset_for_name(const std::string &filename);

  // Asset key for a language id (upstream's KNOWN_LANGUAGES), empty when the
  // language is not in the table.
  std::string resolve_asset_for_language(const std::string &language_id);

  // Upstream's precedence: the file name table wins, the language id is the
  // fallback, and "text" is the last resort.
  std::string resolve_file_icon(const std::string &filename, const std::string &language_id);

  // --- Value formatting ---------------------------------------------------

  // Upstream formats the file size with decimal (1000) divisions and a
  // space only in the byte unit: "512 bytes", "1.50KB", "2.00MB".
  std::string format_file_size(long long bytes);

  // Turns a git remote into a browsable https URL: scp-style ("git@host:o/r")
  // and ssh:// remotes become https, embedded credentials are dropped and the
  // trailing ".git" is stripped. Returns an empty string when there is nothing
  // worth linking to.
  std::string normalize_remote_url(const std::string &remote);

  // Repository name for "{git_repo_name}": the last path segment of a remote,
  // without ".git". Upstream takes the *second* segment (the owner for
  // "github.com/owner/repo" urls, and empty for https urls), which reads like a
  // bug; the repository is what a presence wants to name.
  std::string repository_name_from_url(const std::string &remote);

  // --- Templates ----------------------------------------------------------

  // Values available to every template. Field names mirror upstream's
  // placeholder names.
  struct TemplateContext
  {
    std::string file_name;
    std::string dir_name;             // immediate parent folder name
    std::string full_dir_name;        // relative directory path, no file name
    std::string workspace;            // workspace name
    std::string workspace_folder;     // accessed folder (== workspace for jot)
    std::string workspace_and_folder; // "Workspace - Folder" when they differ
    std::string language;             // asset key, i.e. upstream's fileIcon
    std::string git_branch;
    std::string git_repo_name;
    std::string app_name = "jot";
    long long current_line = 0; // 1-based, 0 when unknown
    long long current_column = 0;
    long long total_lines = 0;
    long long file_size = 0;
    long long current_errors = 0;
    bool has_file = false;
    bool has_workspace = false;
    bool has_git = false;
  };

  // Substitutes every "{placeholder}" in `raw`. Unknown placeholders are left
  // verbatim so a typo is visible instead of silently empty. "{empty}" becomes
  // a two-ZWSP run, exactly like upstream, so a field can be intentionally
  // blank without Discord dropping the row.
  std::string apply_template(const std::string &raw, const TemplateContext &ctx);

  // --- Activity -----------------------------------------------------------

  enum class PresenceState
  {
    Idling,
    Editing,
    Debugging
  };

  // User-configurable presence content. Defaults are upstream's.
  struct PresenceOptions
  {
    std::string details_idling = "Idling";
    std::string details_editing = "Editing {file_name}";
    std::string details_debugging = "Debugging {file_name}";
    std::string lower_details_idling = "Idling";
    std::string lower_details_editing = "Workspace: {workspace}";
    std::string lower_details_debugging = "Debugging: {workspace}";
    std::string lower_details_no_workspace = "No workspace";
    std::string large_image = "Editing a {LANG} file";
    std::string large_image_idling = "Idling";
    std::string small_image = "{app_name}";

    // Idling artwork and the small "app" badge (asset keys, not file paths).
    std::string idle_image_key = "jot";
    std::string app_image_key = "jot";
    std::string debug_image_key = "debug";

    bool swap_big_and_small_image = false;
    bool remove_details = false;
    bool remove_lower_details = false;
    bool remove_timestamp = false;
    bool remove_remote_repository = false;
  };

  // The resolved activity, ready to be serialized.
  struct Activity
  {
    std::string details; // empty = omitted
    std::string state;   // empty = omitted
    bool has_timestamp = false;
    long long start_timestamp = 0;
    std::string large_image_key;
    std::string large_image_text;
    std::string small_image_key;
    std::string small_image_text;
    bool has_button = false;
    std::string button_label = "View Repository";
    std::string button_url;
  };

  // Builds the activity for the current state. `remote` is the raw git remote
  // used for the repository button, `start_timestamp` the stable session start
  // kept across updates (upstream reuses the previous timestamp so the "for
  // 12 minutes" clock never resets while editing).
  Activity build_activity(const PresenceOptions &opts,
                          PresenceState state,
                          const TemplateContext &ctx,
                          const std::string &remote,
                          long long start_timestamp);

  // Escapes a string for a JSON string literal (shared with the IPC layer).
  std::string json_escape(const std::string &value);

  // Serializes the activity object (the "activity" field of SET_ACTIVITY).
  std::string activity_to_json(const Activity &activity);

  // Full SET_ACTIVITY frame payload for a process.
  std::string
  set_activity_payload(const Activity &activity, const std::string &nonce, long long pid);
} // namespace jot_discord

#endif // DISCORD_PRESENCE_H
