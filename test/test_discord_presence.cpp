// Discord Rich Presence content model (src/tools/discord_presence.cpp).
// The rules ported from iCrawl/discord-vscode are pinned here: the asset-key
// tables and their order-sensitive lookup, the placeholder templates, the
// idling / editing / debugging states and the remove-* switches.
#include "tools/discord_presence.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace jot_discord;

namespace
{
  TemplateContext editing_context()
  {
    TemplateContext ctx;
    ctx.has_file = true;
    ctx.file_name = "main.rs";
    ctx.dir_name = "src";
    ctx.full_dir_name = "src/app";
    ctx.workspace = "jot";
    ctx.workspace_folder = "jot";
    ctx.workspace_and_folder = "jot";
    ctx.language = "rust";
    ctx.has_git = true;
    ctx.git_branch = "main";
    ctx.git_repo_name = "jot";
    ctx.current_line = 42;
    ctx.current_column = 7;
    ctx.total_lines = 120;
    ctx.file_size = 2000;
    ctx.current_errors = 3;
    return ctx;
  }
} // namespace

TEST_CASE("Presence asset keys resolve like the upstream tables", "[jot]")
{
  // Literal suffix rules.
  REQUIRE(resolve_asset_for_name("main.rs") == "rust");
  REQUIRE(resolve_asset_for_name("index.tsx") == "tsx");
  REQUIRE(resolve_asset_for_name("src/thing.cpp") == "cpp");

  // Specific names beat the generic extension they end with -- the generated
  // table is order-preserving for exactly this reason.
  REQUIRE(resolve_asset_for_name("package.json") == "npm");
  REQUIRE(resolve_asset_for_name("nodemon.json") == "nodemon");
  REQUIRE(resolve_asset_for_name("ordinary.json") == "json");

  // Regex rules (upstream requires flags on a "/.../" key).
  REQUIRE(resolve_asset_for_name("vite.config.ts") == "viteconfig");
  REQUIRE(resolve_asset_for_name("types.d.ts") == "typescript-def");
  REQUIRE(resolve_asset_for_name("Dockerfile") == "docker");
  REQUIRE(resolve_asset_for_name("docker-compose.yml") == "docker");
  REQUIRE(resolve_asset_for_name(".gitignore") == "git");

  // Language fallback, then the generic text key.
  REQUIRE(resolve_asset_for_language("rust") == "rust");
  REQUIRE(resolve_asset_for_language("MATLAB") == "matlab");
  REQUIRE(resolve_asset_for_language("matlab") == "matlab"); // case-insensitive fallback
  REQUIRE(resolve_asset_for_language("no-such-language").empty());
  REQUIRE(resolve_file_icon("Makefile", "makefile") == "makefile");
  REQUIRE(resolve_file_icon("LICENSE", "") == "text");
  REQUIRE(resolve_file_icon("main.rs", "rust") == "rust");
  // The name table wins when both would match.
  REQUIRE(resolve_file_icon("package.json", "json") == "npm");
}

TEST_CASE("Presence file sizes use upstream's decimal formatting", "[jot]")
{
  REQUIRE(format_file_size(0) == "0 bytes");
  REQUIRE(format_file_size(999) == "999 bytes");
  REQUIRE(format_file_size(1000) == "1000 bytes");
  REQUIRE(format_file_size(1001) == "1.00KB");
  REQUIRE(format_file_size(1500) == "1.50KB");
  // Decimal divisions, not binary: 2048 bytes is 2.05KB, not "2KB".
  REQUIRE(format_file_size(2048) == "2.05KB");
  REQUIRE(format_file_size(1000000) == "1.00MB");
  REQUIRE(format_file_size(2500000000LL) == "2.50GB");
}

TEST_CASE("Presence repository URLs are normalized like upstream", "[jot]")
{
  REQUIRE(normalize_remote_url("git@github.com:owner/repo.git") == "https://github.com/owner/repo");
  REQUIRE(normalize_remote_url("https://github.com/owner/repo.git")
          == "https://github.com/owner/repo");
  REQUIRE(normalize_remote_url("https://user:token@github.com/owner/repo.git")
          == "https://github.com/owner/repo");
  REQUIRE(normalize_remote_url("ssh://git@github.com/owner/repo.git")
          == "https://github.com/owner/repo");
  // A local path is not a browsable URL: no dead "View Repository" button.
  REQUIRE(normalize_remote_url("/srv/git/repo.git").empty());
  REQUIRE(normalize_remote_url("").empty());

  REQUIRE(repository_name_from_url("git@github.com:owner/jot.git") == "jot");
  REQUIRE(repository_name_from_url("https://github.com/owner/jot.git") == "jot");
  REQUIRE(repository_name_from_url("https://github.com/owner/jot") == "jot");
  REQUIRE(repository_name_from_url("").empty());
}

TEST_CASE("Presence templates substitute every placeholder", "[jot]")
{
  const TemplateContext ctx = editing_context();
  REQUIRE(apply_template("Editing {file_name}", ctx) == "Editing main.rs");
  REQUIRE(apply_template("{dir_name}", ctx) == "src");
  REQUIRE(apply_template("{full_dir_name}", ctx) == "src/app");
  REQUIRE(apply_template("Workspace: {workspace}", ctx) == "Workspace: jot");
  REQUIRE(apply_template("{workspace_folder}", ctx) == "jot");
  REQUIRE(apply_template("{workspace_and_folder}", ctx) == "jot");
  REQUIRE(apply_template("{current_line}:{current_column}", ctx) == "42:7");
  REQUIRE(apply_template("{total_lines}", ctx) == "120");
  REQUIRE(apply_template("{file_size}", ctx) == "2.00KB");
  REQUIRE(apply_template("{current_errors}", ctx) == "3");
  REQUIRE(apply_template("{git_branch}", ctx) == "main");
  REQUIRE(apply_template("{git_repo_name}", ctx) == "jot");
  REQUIRE(apply_template("{lang}/{Lang}/{LANG}", ctx) == "rust/Rust/RUST");
  REQUIRE(apply_template("{app_name}", ctx) == "jot");

  // "{empty}" is the two-ZWSP run upstream uses to keep a row that would
  // otherwise be blank. (U+200B, spelled with \u escapes: a \x8B escape would
  // swallow the following hex-digit characters.)
  REQUIRE(apply_template("a{empty}b", ctx) == "a\u200B\u200Bb");
  // An unknown placeholder stays visible instead of silently disappearing.
  REQUIRE(apply_template("{nope}", ctx) == "{nope}");

  // Numbers and sizes are meaningless without an open file: upstream leaves the
  // braces in place, we render them empty (never "0:0" for an empty buffer).
  TemplateContext idle;
  idle.workspace = "jot";
  REQUIRE(apply_template("{current_line}:{current_column}", idle) == ":");
  REQUIRE(apply_template("{file_size}", idle).empty());
}

TEST_CASE("Presence activity follows the editor state", "[jot]")
{
  PresenceOptions options;
  const TemplateContext ctx = editing_context();

  // Editing: file details on both rows, the language as the large image.
  const Activity editing = build_activity(options, PresenceState::Editing, ctx, "", 1700000000);
  REQUIRE(editing.details == "Editing main.rs");
  REQUIRE(editing.state == "Workspace: jot");
  REQUIRE(editing.large_image_key == "rust");
  REQUIRE(editing.large_image_text == "Editing a RUST file");
  REQUIRE(editing.small_image_key == "jot");
  REQUIRE(editing.small_image_text == "jot");
  REQUIRE(editing.has_timestamp);
  REQUIRE(editing.start_timestamp == 1700000000);
  REQUIRE_FALSE(editing.has_button);

  // Debugging: its own templates, the debug badge, the language still on top.
  const Activity debugging = build_activity(options, PresenceState::Debugging, ctx, "", 1700000000);
  REQUIRE(debugging.details == "Debugging main.rs");
  REQUIRE(debugging.state == "Debugging: jot");
  REQUIRE(debugging.large_image_key == "rust");
  REQUIRE(debugging.small_image_key == "debug");

  // Idling (no file): the idle artwork and text, and no state row -- upstream
  // only fills the second line once a file is open.
  TemplateContext idle;
  idle.workspace = "jot";
  idle.workspace_folder = "jot";
  idle.workspace_and_folder = "jot";
  const Activity idling = build_activity(options, PresenceState::Idling, idle, "", 1700000000);
  REQUIRE(idling.details == "Idling");
  REQUIRE(idling.state.empty());
  REQUIRE(idling.large_image_key == "jot");
  REQUIRE(idling.large_image_text == "Idling");
}

TEST_CASE("Presence options shape the activity", "[jot]")
{
  PresenceOptions options;
  const TemplateContext ctx = editing_context();

  options.remove_details = true;
  options.remove_lower_details = true;
  options.remove_timestamp = true;
  const Activity bare = build_activity(options, PresenceState::Editing, ctx, "", 1700000000);
  REQUIRE(bare.details.empty());
  REQUIRE(bare.state.empty());
  REQUIRE_FALSE(bare.has_timestamp);

  options.remove_details = false;
  options.remove_lower_details = false;
  options.remove_timestamp = false;
  options.swap_big_and_small_image = true;
  const Activity swapped = build_activity(options, PresenceState::Editing, ctx, "", 1700000000);
  REQUIRE(swapped.small_image_key == "rust");
  REQUIRE(swapped.large_image_key == "jot");
  REQUIRE(swapped.large_image_text == "jot");
  REQUIRE(swapped.small_image_text == "Editing a RUST file");

  options.swap_big_and_small_image = false;
  const Activity linked = build_activity(
      options, PresenceState::Editing, ctx, "git@github.com:owner/jot.git", 1700000000);
  REQUIRE(linked.has_button);
  REQUIRE(linked.button_url == "https://github.com/owner/jot");
  REQUIRE(linked.button_label == "View Repository");

  options.remove_remote_repository = true;
  const Activity unlinked = build_activity(
      options, PresenceState::Editing, ctx, "git@github.com:owner/jot.git", 1700000000);
  REQUIRE_FALSE(unlinked.has_button);
}

TEST_CASE("Presence image keys name an asset the app must have uploaded", "[jot]")
{
  // Regression for the "text is there but the language icon never appears"
  // report: the key we ask Discord for must be the plain language asset
  // ("cpp"), because that is the name the artwork is uploaded under. A key
  // that carries a path, an extension or a leading dot can never resolve.
  const TemplateContext ctx = editing_context();
  const PresenceOptions options;
  const Activity activity = build_activity(options, PresenceState::Editing, ctx, "", 1);
  REQUIRE(activity.large_image_key == "rust");
  REQUIRE(activity.large_image_key.find('.') == std::string::npos);
  REQUIRE(activity.large_image_key.find('/') == std::string::npos);

  TemplateContext cpp;
  cpp.has_file = true;
  cpp.file_name = "main.cpp";
  cpp.language = resolve_file_icon("main.cpp", "");
  REQUIRE(cpp.language == "cpp");
  const Activity cpp_activity = build_activity(options, PresenceState::Editing, cpp, "", 1);
  REQUIRE(cpp_activity.large_image_key == "cpp");
  REQUIRE(cpp_activity.large_image_text == "Editing a CPP file");

  // Headers resolve through the language id, since the name table has no bare
  // ".h" rule.
  TemplateContext header;
  header.has_file = true;
  header.file_name = "util.h";
  header.language = resolve_file_icon("util.h", "c");
  REQUIRE(header.language == "c");
  // An unknown extension still lands on a real key, never an empty one that
  // Discord would drop.
  TemplateContext unknown;
  unknown.has_file = true;
  unknown.file_name = "NOTES";
  unknown.language = resolve_file_icon("NOTES", "");
  REQUIRE(unknown.language == "text");
}

TEST_CASE("Presence activity serializes to valid SET_ACTIVITY JSON", "[jot]")
{
  PresenceOptions options;
  const TemplateContext ctx = editing_context();
  const Activity activity = build_activity(options, PresenceState::Editing, ctx, "", 1700000000);
  const std::string json = set_activity_payload(activity, "7", 4242);

  REQUIRE(json.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos);
  REQUIRE(json.find("\"nonce\":\"7\"") != std::string::npos);
  REQUIRE(json.find("\"pid\":4242") != std::string::npos);
  REQUIRE(json.find("\"details\":\"Editing main.rs\"") != std::string::npos);
  REQUIRE(json.find("\"state\":\"Workspace: jot\"") != std::string::npos);
  REQUIRE(json.find("\"large_image\":\"rust\"") != std::string::npos);
  REQUIRE(json.find("\"small_image\":\"jot\"") != std::string::npos);
  REQUIRE(json.find("\"timestamps\":{\"start\":1700000000}") != std::string::npos);
  REQUIRE(json.empty() == false);

  // Exact shape for the minimal activity: catches separator bugs (a stray
  // comma before a closing brace) that a substring check would miss.
  Activity minimal;
  minimal.details = "Idling";
  REQUIRE(activity_to_json(minimal) == "{\"details\":\"Idling\"}");

  Activity with_button;
  with_button.details = "Editing";
  with_button.has_timestamp = true;
  with_button.start_timestamp = 5;
  with_button.has_button = true;
  with_button.button_url = "https://example.com/r";
  REQUIRE(activity_to_json(with_button)
          == "{\"details\":\"Editing\",\"timestamps\":{\"start\":5},"
             "\"buttons\":[{\"label\":\"View Repository\",\"url\":\"https://example.com/r\"}]}");

  // Quotes and backslashes must not break the payload.
  Activity quotes;
  quotes.details = "Editing \"weird\\name\"";
  REQUIRE(activity_to_json(quotes) == "{\"details\":\"Editing \\\"weird\\\\name\\\"\"}");
}
