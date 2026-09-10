// Rich Presence content model. See discord_presence.h for what this ports and
// from where (iCrawl/discord-vscode, MIT).
#include "discord_presence.h"
#include "discord_presence_data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <regex>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace jot_discord
{
  long long monotonic_ms()
  {
#if defined(_WIN32)
    return static_cast<long long>(GetTickCount64());
#else
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
  }

  namespace
  {
    // Upstream's FAKE_EMPTY: two zero-width spaces, so a "blank" row still has
    // content and Discord keeps the field instead of dropping it.
    constexpr const char *kFakeEmpty = "\xE2\x80\x8B\xE2\x80\x8B";

    bool starts_with(const std::string &text, const char *prefix)
    {
      const size_t n = std::strlen(prefix);
      return text.size() >= n && text.compare(0, n, prefix) == 0;
    }

    // Replaces the FIRST occurrence, matching JavaScript's String.replace()
    // with a string pattern -- upstream relies on that for ".git" removals.
    void replace_first(std::string &text, const std::string &needle, const std::string &with)
    {
      const size_t pos = text.find(needle);
      if (pos != std::string::npos)
      {
        text.replace(pos, needle.size(), with);
      }
    }

    void replace_all(std::string &text, const std::string &needle, const std::string &with)
    {
      size_t pos = 0;
      while ((pos = text.find(needle, pos)) != std::string::npos)
      {
        text.replace(pos, needle.size(), with);
        pos += with.size();
      }
    }

    bool ends_with(const std::string &text, const std::string &suffix)
    {
      return text.size() >= suffix.size()
             && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    struct CompiledRule
    {
      bool is_regex = false;
      std::string literal;
      std::regex pattern;
      std::string asset;
    };

    // The name table is walked in order and the first hit wins, so the rules
    // are compiled once, preserving the generated table's order.
    const std::vector<CompiledRule> &compiled_rules()
    {
      static const std::vector<CompiledRule> rules = []()
      {
        // Upstream only treats a slash-wrapped key as a regex when it carries
        // at least one flag: "/^Makefile/" (no flags) is compared as a literal
        // suffix and therefore never matches a file name.
        static const std::regex kRuleShape("^/(.*)/([gimy]+)$");
        std::vector<CompiledRule> out;
        out.reserve(sizeof(kNameAssets) / sizeof(kNameAssets[0]));
        for (const NameAsset &entry : kNameAssets)
        {
          CompiledRule rule;
          rule.asset = entry.asset;
          std::cmatch match;
          if (std::regex_match(entry.name, match, kRuleShape))
          {
            std::regex_constants::syntax_option_type flags = std::regex_constants::ECMAScript;
            const std::string flag_text = match[2].str();
            if (flag_text.find('i') != std::string::npos)
            {
              flags |= std::regex_constants::icase;
            }
            if (flag_text.find('m') != std::string::npos)
            {
              flags |= std::regex_constants::multiline;
            }
            try
            {
              rule.pattern = std::regex(match[1].str(), flags);
              rule.is_regex = true;
            }
            catch (const std::regex_error &)
            {
              // JavaScript-only escapes (e.g. /\\prettier.config.js/i) do not
              // compile under ECMAScript's stricter escape rules; upstream
              // would throw and abort the whole lookup, so skipping just the
              // rule is strictly better.
              rule.is_regex = false;
            }
          }
          if (!rule.is_regex)
          {
            rule.literal = entry.name;
          }
          out.push_back(std::move(rule));
        }
        return out;
      }();
      return rules;
    }
  } // namespace

  std::string resolve_asset_for_name(const std::string &filename)
  {
    if (filename.empty())
    {
      return "";
    }
    for (const CompiledRule &rule : compiled_rules())
    {
      if (rule.is_regex)
      {
        if (std::regex_search(filename, rule.pattern))
        {
          return rule.asset;
        }
        continue;
      }
      if (!rule.literal.empty() && ends_with(filename, rule.literal))
      {
        return rule.asset;
      }
    }
    return "";
  }

  std::string resolve_asset_for_language(const std::string &language_id)
  {
    if (language_id.empty())
    {
      return "";
    }
    std::string lowered = language_id;
    std::transform(lowered.begin(),
                   lowered.end(),
                   lowered.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    for (const LanguageAsset &entry : kLanguageAssets)
    {
      if (language_id == entry.language)
      {
        return entry.asset;
      }
    }
    // jot's language ids are lowercase; upstream's table has a few mixed-case
    // entries (MATLAB), so fall back to a case-insensitive pass.
    for (const LanguageAsset &entry : kLanguageAssets)
    {
      std::string known = entry.language;
      std::transform(known.begin(),
                     known.end(),
                     known.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      if (known == lowered)
      {
        return entry.asset;
      }
    }
    return "";
  }

  std::string resolve_file_icon(const std::string &filename, const std::string &language_id)
  {
    const std::string by_name = resolve_asset_for_name(filename);
    if (!by_name.empty())
    {
      return by_name;
    }
    const std::string by_language = resolve_asset_for_language(language_id);
    if (!by_language.empty())
    {
      return by_language;
    }
    return "text";
  }

  std::string format_file_size(long long bytes)
  {
    static const char *kUnits[] = {" bytes", "KB", "MB", "GB", "TB"};
    constexpr int kLastUnit = 4;
    if (bytes <= 1000)
    {
      return std::to_string(bytes) + kUnits[0];
    }
    double size = (double)bytes;
    int unit = 0;
    size /= 1000.0;
    unit++;
    // Upstream rolls over on "> 1000" only, so exactly 1,000,000 bytes prints as
    // "1000.00KB"; using ">=" gives the expected "1.00MB".
    while (size >= 1000.0 && unit < kLastUnit)
    {
      size /= 1000.0;
      unit++;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f%s", size, kUnits[unit]);
    return buffer;
  }

  std::string normalize_remote_url(const std::string &remote)
  {
    if (remote.empty())
    {
      return "";
    }
    std::string url = remote;
    if (starts_with(url, "git@") || starts_with(url, "ssh://"))
    {
      // scp-style "git@host:owner/repo.git" and ssh:// remotes become https,
      // exactly like upstream (first ':' only, then the "git@" prefix).
      replace_first(url, "ssh://", "");
      replace_first(url, ":", "/");
      replace_first(url, "git@", "https://");
      replace_first(url, ".git", "");
    }
    else
    {
      // Drop credentials from "https://user:token@host/owner/repo.git".
      static const std::regex kCredentials("^(https://)([^@]*)@(.*)$");
      std::smatch match;
      if (std::regex_match(url, match, kCredentials))
      {
        url = match[1].str() + match[3].str();
      }
      replace_first(url, ".git", "");
    }
    // A remote that is not a browsable URL (a local path, a bare host) would
    // produce a dead button; upstream emits it anyway, we drop it.
    if (!starts_with(url, "http://") && !starts_with(url, "https://"))
    {
      return "";
    }
    return url;
  }

  std::string repository_name_from_url(const std::string &remote)
  {
    if (remote.empty())
    {
      return "";
    }
    std::string url = remote;
    replace_first(url, "ssh://", "");
    // scp-style remotes separate the host from the path with ':'; turning it
    // into '/' makes the last segment extraction work for every form.
    replace_first(url, ":", "/");
    while (!url.empty() && url.back() == '/')
    {
      url.pop_back();
    }
    if (ends_with(url, ".git"))
    {
      url.resize(url.size() - 4);
    }
    const size_t slash = url.rfind('/');
    return slash == std::string::npos ? url : url.substr(slash + 1);
  }

  std::string apply_template(const std::string &raw, const TemplateContext &ctx)
  {
    // Upstream's toLower / toTitle / toUpper over the resolved icon key: the
    // key doubles as the language name in the templates ("{lang} file").
    std::string language_lower = ctx.language;
    std::transform(language_lower.begin(),
                   language_lower.end(),
                   language_lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::string language_upper = language_lower;
    std::transform(language_upper.begin(),
                   language_upper.end(),
                   language_upper.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    std::string language_title = language_lower;
    if (!language_title.empty())
    {
      language_title[0] = (char)std::toupper((unsigned char)language_title[0]);
    }

    struct Token
    {
      const char *name;
      std::string value;
    };
    // Numbers are only meaningful once a file is open; upstream leaves the
    // tokens verbatim when idling, which shows braces in the profile -- empty
    // is the better failure mode.
    const auto number = [&](long long value)
    { return ctx.has_file ? std::to_string(value) : std::string(); };
    const Token tokens[] = {
        {"{file_name}", ctx.file_name},
        {"{dir_name}", ctx.dir_name},
        {"{full_dir_name}", ctx.full_dir_name},
        {"{workspace}", ctx.workspace},
        {"{workspace_folder}", ctx.workspace_folder},
        {"{workspace_and_folder}", ctx.workspace_and_folder},
        {"{current_line}", number(ctx.current_line)},
        {"{current_column}", number(ctx.current_column)},
        {"{total_lines}", number(ctx.total_lines)},
        {"{file_size}", ctx.has_file ? format_file_size(ctx.file_size) : std::string()},
        {"{current_errors}", number(ctx.current_errors)},
        {"{git_branch}", ctx.has_git ? ctx.git_branch : std::string("Unknown")},
        {"{git_repo_name}", ctx.has_git ? ctx.git_repo_name : std::string("Unknown")},
        // Upstream derives all three casings from the resolved icon key, so
        // "Editing a {LANG} file" reads "Editing a CPP file".
        {"{lang}", language_lower},
        {"{Lang}", language_title},
        {"{LANG}", language_upper},
        {"{app_name}", ctx.app_name},
    };

    std::string out = raw;
    for (const Token &token : tokens)
    {
      replace_all(out, token.name, token.value);
    }
    // Upstream only substitutes this in the idling template and in the
    // no-workspace string; applying it to every template (last, so it cannot
    // interact with the rest) is the predictable version of that.
    replace_all(out, "{empty}", kFakeEmpty);
    return out;
  }

  Activity build_activity(const PresenceOptions &opts,
                          PresenceState state,
                          const TemplateContext &ctx,
                          const std::string &remote,
                          long long start_timestamp)
  {
    Activity activity;

    const std::string &details_template = state == PresenceState::Idling ? opts.details_idling
                                          : state == PresenceState::Debugging
                                              ? opts.details_debugging
                                              : opts.details_editing;
    const std::string &lower_template = state == PresenceState::Idling ? opts.lower_details_idling
                                        : state == PresenceState::Debugging
                                            ? opts.lower_details_debugging
                                            : opts.lower_details_editing;

    if (!opts.remove_details)
    {
      activity.details = apply_template(details_template, ctx);
    }
    // Upstream only fills the second row once a file is open: with no editor
    // the activity shows the details line alone.
    if (!opts.remove_lower_details && ctx.has_file)
    {
      activity.state = apply_template(lower_template, ctx);
    }

    if (state == PresenceState::Idling)
    {
      activity.large_image_key = opts.idle_image_key;
      activity.large_image_text = opts.large_image_idling;
    }
    else
    {
      activity.large_image_key = ctx.language.empty() ? "text" : ctx.language;
      activity.large_image_text = apply_template(opts.large_image, ctx);
    }
    activity.small_image_key =
        state == PresenceState::Debugging ? opts.debug_image_key : opts.app_image_key;
    activity.small_image_text = apply_template(opts.small_image, ctx);

    if (opts.swap_big_and_small_image)
    {
      std::swap(activity.large_image_key, activity.small_image_key);
      std::swap(activity.large_image_text, activity.small_image_text);
    }

    activity.has_timestamp = !opts.remove_timestamp;
    activity.start_timestamp = start_timestamp;

    if (!opts.remove_remote_repository)
    {
      const std::string url = normalize_remote_url(remote);
      if (!url.empty())
      {
        activity.has_button = true;
        activity.button_url = url;
      }
    }
    return activity;
  }

  std::string json_escape(const std::string &value)
  {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value)
    {
      switch (c)
      {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
      }
    }
    return out;
  }

  namespace
  {
    void add_string(std::string &json, bool &first, const char *key, const std::string &value)
    {
      if (value.empty())
      {
        return;
      }
      if (!first)
      {
        json += ",";
      }
      first = false;
      json += "\"";
      json += key;
      json += "\":\"";
      json += json_escape(value);
      json += "\"";
    }
  } // namespace

  std::string activity_to_json(const Activity &activity)
  {
    std::string json = "{";
    bool first = true;
    add_string(json, first, "details", activity.details);
    add_string(json, first, "state", activity.state);

    // "assets" and "timestamps" are objects of their own; they are only
    // emitted when they carry something.
    const bool has_assets = !activity.large_image_key.empty() || !activity.large_image_text.empty()
                            || !activity.small_image_key.empty()
                            || !activity.small_image_text.empty();
    if (has_assets)
    {
      if (!first)
      {
        json += ",";
      }
      first = false;
      json += "\"assets\":{";
      bool asset_first = true;
      add_string(json, asset_first, "large_image", activity.large_image_key);
      add_string(json, asset_first, "large_text", activity.large_image_text);
      add_string(json, asset_first, "small_image", activity.small_image_key);
      add_string(json, asset_first, "small_text", activity.small_image_text);
      json += "}";
    }
    if (activity.has_timestamp)
    {
      if (!first)
      {
        json += ",";
      }
      first = false;
      json += "\"timestamps\":{\"start\":" + std::to_string(activity.start_timestamp) + "}";
    }
    if (activity.has_button && !activity.button_url.empty())
    {
      if (!first)
      {
        json += ",";
      }
      first = false;
      json += "\"buttons\":[{\"label\":\"" + json_escape(activity.button_label) + "\",\"url\":\""
              + json_escape(activity.button_url) + "\"}]";
    }
    json += "}";
    return json;
  }

  std::string
  set_activity_payload(const Activity &activity, const std::string &nonce, long long pid)
  {
    return "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"" + json_escape(nonce) + "\",\"args\":{\"pid\":"
           + std::to_string(pid) + ",\"activity\":" + activity_to_json(activity) + "}}";
  }
} // namespace jot_discord
