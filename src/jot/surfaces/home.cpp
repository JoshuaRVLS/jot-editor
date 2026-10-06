#include "editor.h"
#include "jot/file_icons.h"
#include "jot/lua/api.h"
#include "ui/text.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
  enum HomeAction
  {
    HOME_ACTION_OPEN_RECENT = 1,
    HOME_ACTION_OPEN_RECENT_WORKSPACE = 2,
    HOME_ACTION_NEW_FILE = 3,
    HOME_ACTION_OPEN_RECENT_PROMPT = 4,
    HOME_ACTION_COMMAND_PALETTE = 5,
    HOME_ACTION_THEME_CHOOSER = 6,
    HOME_ACTION_OPEN_PATH_PROMPT = 7,
    HOME_ACTION_RESUME = 8,
    HOME_ACTION_CONTINUE_EDITOR = 9,
    HOME_ACTION_QUIT = 10,
    HOME_ACTION_OPEN_PATH = 11
  };

  struct HomeMenuRenderItem
  {
    int action = 0;
    int recent_index = -1;
    int recent_workspace_index = -1;
    std::string label;
    std::string secondary;
    std::string path; // what the row opens, empty for the command rows
    char key = 0;     // the key that opens it while the screen is up
    std::string icon; // leading glyph, in its own color (may be empty)
    int icon_fg = -1; // -1: the row's own fg
  };

  // One column of the home screen: the title, the rows in it, and how many of
  // them the layout below has room for. The title carries its own glyph and
  // accent so the eye finds the lists without reading their names.
  struct HomeSection
  {
    std::string title;
    std::string icon;
    int fg = -1;
    std::vector<HomeMenuRenderItem> items;
    std::string empty_label;
    int cap = 6;
    int show = 0;
  };

  std::string icon_for_path(const std::string &path)
  {
    // Shared per-extension glyph map (core/file_icons.h) so the home screen,
    // the file explorer and the status line agree on file icons. The row keeps
    // the glyph in its own field, so it can carry the file's brand color
    // instead of the label's.
    return std::string(jot_icons::file_type_icon(path).glyph);
  }

  // The big wordmark: three rows of block letters, one run per letter so every
  // letter can take its own accent (keyword / function / type), one blank
  // column apart. kHomeHeaderRows is what the model spends on the whole header
  // before the first section title: the art (three rows), the tagline row
  // (with the key legend) and the context line, plus one blank.
  const std::vector<std::vector<std::string>> &wordmark_art()
  {
    static const std::vector<std::vector<std::string>> art = {
        {"  █", "███", "███"},
        {"  █", "█ █", " █ "},
        {"███", "███", " █ "},
    };
    return art;
  }
  constexpr int kHomeHeaderRows = 6;        // art + tagline + context + one blank
  constexpr int kHomeCompactHeaderRows = 4; // wordmark + context + two blanks
  // Usable rows at which the big wordmark is worth its two extra rows. Below
  // it the compact one-line wordmark leaves the rows to the lists: on a short
  // screen the actions column, the section titles and their items all have to
  // fit under the header.
  constexpr int kHomeBigWordmarkRows = 26;
  // Usable rows below which the bottom strip is dropped: on a short terminal the
  // lists need the rows more than the quote does.
  constexpr int kHomeStripMinRows = 18;

  long long home_now_ms()
  {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  struct HomeProject
  {
    std::string path;
    int score = 0;
  };

  struct BrowseEntry
  {
    std::string path;
    std::string name;
    bool dir = false;
  };

  // The two scans below are cached for a few seconds because the model is
  // rebuilt on every frame, hover included. File scope (and not a function
  // local) so a test can drop the cache after reshaping the tree on disk.
  struct HomeScanCache
  {
    long long projects_at_ms = 0;
    std::vector<HomeProject> projects;
    long long browse_at_ms = 0;
    std::string browse_key;
    std::vector<BrowseEntry> browse_entries;

    void clear()
    {
      projects_at_ms = 0;
      projects.clear();
      browse_at_ms = 0;
      browse_key.clear();
      browse_entries.clear();
    }
  };

  HomeScanCache &scan_cache()
  {
    static HomeScanCache cache;
    return cache;
  }

  // What makes a directory look like a project, strongest first: the scan stops
  // at the first hit, so a repository costs a single stat.
  const char *const kProjectMarkers[] = {".git",
                                         "package.json",
                                         "Cargo.toml",
                                         "go.mod",
                                         "pyproject.toml",
                                         "CMakeLists.txt",
                                         "composer.json",
                                         "Gemfile",
                                         "pom.xml",
                                         "build.gradle",
                                         "deno.json"};

  bool project_marker_score(const fs::path &dir, int &score)
  {
    std::error_code ec;
    for (int i = 0; i < (int)(sizeof(kProjectMarkers) / sizeof(kProjectMarkers[0])); i++)
    {
      if (fs::exists(dir / kProjectMarkers[i], ec) && !ec)
      {
        score = 100 - i;
        return true;
      }
    }
    return false;
  }

  std::string user_home_path()
  {
    if (const char *home = std::getenv("HOME"); home && *home)
    {
      return home;
    }
#ifdef _WIN32
    if (const char *profile = std::getenv("USERPROFILE"); profile && *profile)
    {
      return profile;
    }
#endif
    return "";
  }

  // Where to look for projects: the launch folder, its parent (starting jot
  // inside a subdirectory still finds the repo), and the usual dev folders.
  std::vector<fs::path> project_roots()
  {
    std::vector<fs::path> roots;
    std::error_code ec;
    auto push = [&](const fs::path &p)
    {
      if (p.empty() || !fs::is_directory(p, ec) || ec)
      {
        return;
      }
      for (const auto &existing : roots)
      {
        if (existing == p)
        {
          return;
        }
      }
      roots.push_back(p);
    };

    const fs::path cwd = fs::current_path(ec);
    push(cwd);
    push(cwd.parent_path());
    const std::string home = user_home_path();
    if (!home.empty())
    {
      for (const char *name :
           {"Projects", "projects", "dev", "Dev", "src", "code", "repos", "work"})
      {
        push(fs::path(home) / name);
      }
    }
    return roots;
  }

  const std::vector<HomeProject> &scan_projects()
  {
    HomeScanCache &cache = scan_cache();
    std::vector<HomeProject> &found = cache.projects;
    const long long now = home_now_ms();
    if (cache.projects_at_ms != 0 && now - cache.projects_at_ms < 5000)
    {
      return found;
    }
    cache.projects_at_ms = now;
    found.clear();

    std::error_code ec;
    for (const auto &root : project_roots())
    {
      int score = 0;
      if (project_marker_score(root, score))
      {
        found.push_back({root.string(), score + 50});
      }
      int seen = 0;
      for (const auto &entry : fs::directory_iterator(root, ec))
      {
        if (ec || ++seen > 200)
        {
          break;
        }
        const std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.' || name == "node_modules")
        {
          continue;
        }
        if (!entry.is_directory(ec) || ec)
        {
          continue;
        }
        if (project_marker_score(entry.path(), score))
        {
          found.push_back({entry.path().string(), score});
        }
      }
    }

    std::sort(found.begin(),
              found.end(),
              [](const HomeProject &a, const HomeProject &b)
              {
                if (a.score != b.score)
                {
                  return a.score > b.score;
                }
                return a.path < b.path;
              });
    // One row per directory: the same project is reachable from the launch
    // folder and from its parent.
    std::vector<HomeProject> unique;
    std::unordered_set<std::string> seen_paths;
    for (auto &project : found)
    {
      if (seen_paths.insert(project.path).second)
      {
        unique.push_back(std::move(project));
      }
    }
    found = std::move(unique);
    return found;
  }

  // Subdirectories first, then files, so the launch folder reads like a file
  // manager's list and the arrow keys walk it predictably.
  const std::vector<BrowseEntry> &browse_dir(const std::string &dir)
  {
    HomeScanCache &cache = scan_cache();
    std::vector<BrowseEntry> &entries = cache.browse_entries;
    const long long now = home_now_ms();
    if (cache.browse_at_ms != 0 && cache.browse_key == dir && now - cache.browse_at_ms < 5000)
    {
      return entries;
    }
    cache.browse_at_ms = now;
    cache.browse_key = dir;
    entries.clear();

    std::error_code ec;
    std::vector<BrowseEntry> dirs, files;
    int seen = 0;
    for (const auto &entry : fs::directory_iterator(dir, ec))
    {
      if (ec || ++seen > 400)
      {
        break;
      }
      const std::string name = entry.path().filename().string();
      if (name.empty() || name[0] == '.' || name == "node_modules")
      {
        continue;
      }
      const bool is_dir = entry.is_directory(ec) && !ec;
      (is_dir ? dirs : files).push_back({entry.path().string(), name, is_dir});
    }
    auto by_name = [](const BrowseEntry &a, const BrowseEntry &b) { return a.name < b.name; };
    std::sort(dirs.begin(), dirs.end(), by_name);
    std::sort(files.begin(), files.end(), by_name);
    entries = std::move(dirs);
    entries.insert(entries.end(), files.begin(), files.end());
    return entries;
  }

  // "2h ago" from a path's own timestamp: for two checkouts with the same name
  // it is what tells which one was touched last.
  std::string relative_age(const std::string &path)
  {
    std::error_code ec;
    const fs::file_time_type stamp = fs::last_write_time(path, ec);
    if (ec)
    {
      return "";
    }
    const long long secs =
        std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - stamp)
            .count();
    if (secs < 0)
    {
      return "";
    }
    if (secs < 60)
    {
      return "just now";
    }
    if (secs < 3600)
    {
      return std::to_string(secs / 60) + "m ago";
    }
    if (secs < 86400)
    {
      return std::to_string(secs / 3600) + "h ago";
    }
    if (secs < 30LL * 86400)
    {
      return std::to_string(secs / 86400) + "d ago";
    }
    return std::to_string(secs / (30LL * 86400)) + "mo ago";
  }

  std::string lowered_copy(const std::string &text)
  {
    std::string out = text;
    std::transform(
        out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
  }

  // A filtered row matches on its label or on the path it would open.
  bool
  row_matches(const std::string &needle, const std::string &label, const std::string &secondary)
  {
    if (needle.empty())
    {
      return true;
    }
    return lowered_copy(label).find(needle) != std::string::npos
           || lowered_copy(secondary).find(needle) != std::string::npos;
  }

  std::string shorten_tail(const std::string &text, int max_len)
  {
    if (max_len <= 0)
    {
      return "";
    }
    if ((int)text.size() <= max_len)
    {
      return text;
    }
    if (max_len <= 3)
    {
      return text.substr(0, max_len);
    }
    return "..." + text.substr(text.size() - (size_t)(max_len - 3));
  }

  std::string ellipsize_right(const std::string &text, int max_len)
  {
    if (max_len <= 0)
    {
      return "";
    }
    if ((int)text.size() <= max_len)
    {
      return text;
    }
    if (max_len <= 3)
    {
      return text.substr(0, (size_t)max_len);
    }
    return text.substr(0, (size_t)(max_len - 3)) + "...";
  }

  // The verse of the day, wrapped to the panel and capped at `max_lines`: the
  // bottom strip is a hint, so a long passage is cut with an ellipsis rather
  // than read as the screen's own text. Whitespace runs (the API sends the text
  // with newlines in it) fold to single spaces.
  std::vector<std::string> wrap_quote(const std::string &text, int width, int max_lines)
  {
    std::vector<std::string> lines;
    if (width <= 0 || max_lines <= 0)
    {
      return lines;
    }
    std::string line;
    bool more = false;
    size_t i = 0;
    while (i < text.size())
    {
      while (i < text.size() && std::isspace((unsigned char)text[i]))
      {
        i++;
      }
      const size_t start = i;
      while (i < text.size() && !std::isspace((unsigned char)text[i]))
      {
        i++;
      }
      if (start == i)
      {
        break;
      }
      const std::string word = text.substr(start, i - start);
      if (!line.empty() && ui_cell_count(line) + 1 + ui_cell_count(word) > width)
      {
        lines.push_back(line);
        line.clear();
        if ((int)lines.size() >= max_lines)
        {
          more = true;
          break;
        }
      }
      if (!line.empty())
      {
        line += " ";
      }
      line += word;
    }
    if (more)
    {
      if (!lines.empty())
      {
        lines.back() = ellipsize_right(lines.back() + " ...", width);
      }
      return lines;
    }
    if (!line.empty())
    {
      lines.push_back(line);
    }
    return lines;
  }

  // The day this binary was built, spelled for the footer: __DATE__ is
  // "Mmm dd yyyy" with a space-padded day, and the strip reads better without
  // the pad. It is the only "last updated" a self-contained binary can honestly
  // report about itself.
  std::string build_date_label()
  {
    const std::string raw = __DATE__;
    if (raw.size() < 11)
    {
      return raw;
    }
    std::string day = raw.substr(4, 2);
    if (!day.empty() && day[0] == ' ')
    {
      day.erase(0, 1);
    }
    return raw.substr(0, 3) + " " + day + ", " + raw.substr(7, 4);
  }

  // The folder glyph every directory row leads with; the color comes from the
  // caller, so the same glyph works on the lists and in the actions column.
  std::string folder_icon()
  {
    return "";
  }

  std::string display_parent(const std::string &path)
  {
    // Canonicalizing a path costs a realpath/stat chain per component, and
    // the home model is rebuilt on every frame (hover included). The result
    // only depends on the path string, so it is cached for the session; the
    // map is bounded by the number of entries in the recent lists.
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(path);
    if (it != cache.end())
    {
      return it->second;
    }
    std::error_code ec;
    std::filesystem::path p(path);
    std::filesystem::path parent = p.parent_path();
    std::string result;
    if (parent.empty())
    {
      result = path;
    }
    else
    {
      std::filesystem::path normalized = std::filesystem::weakly_canonical(parent, ec);
      result = ec ? parent.string() : normalized.string();
    }
    cache.emplace(path, result);
    return result;
  }
} // namespace

// Builds the screen's rows for the current query and hands them back for
// painting. The rows Enter, a digit and a pin act on come from here too: a
// terminal can deliver several keys in one read (a paste), so the input
// handler rebuilds when the query it was built for is no longer the query on
// the screen.
HomeView Editor::build_home_menu_model()
{
  const int screen_w = ui->get_render_width();
  const int screen_h = ui->get_height();
  const int usable_h = std::max(1, screen_h - status_height);

  // No full-screen fill: render() already cleared the grid to the theme's
  // default colors (UI::clear runs at the top of the frame, and theme changes
  // push the new defaults through UI::set_default_colors), so repainting every
  // cell here would only duplicate that work on each hover frame.
  const int content_w = std::max(1, std::min(screen_w - 4, 118));
  const int content_x = std::max(1, (screen_w - content_w) / 2);
  const int content_y = std::max(0, std::min(2, usable_h - 1));
  const int content_h = std::max(1, usable_h - content_y);

  home_menu_panel_x = content_x;
  home_menu_panel_y = content_y;
  home_menu_panel_w = content_w;
  home_menu_panel_h = content_h;

  // The context line names the launch folder, which is also what the "Here"
  // section lists: this is the directory jot was started in.
  std::error_code cwd_ec;
  const fs::path launch_path = fs::current_path(cwd_ec);
  const std::string launch = cwd_ec ? std::string() : launch_path.string();
  const std::string context =
      launch.empty() ? std::string("No recent workspace yet") : "Here  " + launch;

  const bool two_column = screen_w >= 88 && content_w >= 78;
  const int gap = two_column ? 4 : 0;
  const int action_w = two_column ? std::max(24, std::min(34, content_w / 3)) : content_w;
  const int recent_w = two_column ? std::max(1, content_w - action_w - gap) : content_w;
  const int action_x = content_x;
  const int recent_x = two_column ? content_x + action_w + gap : content_x;
  // The big wordmark owns the top of the panel when the terminal can afford
  // it; the section titles start below its header (kHomeHeaderRows), or below
  // the compact one when it cannot.
  const bool big_wordmark = usable_h >= kHomeBigWordmarkRows;
  const int section_y = content_y + (big_wordmark ? kHomeHeaderRows : kHomeCompactHeaderRows);

  // The bottom strip: the verse of the day centered over its reference, with the
  // footer on the panel's last row. The verse is wrapped here so `strip_rows`
  // counts the rows it really takes -- its lines plus the reference under them;
  // the `- 1` in `row_limit` reserves the footer row, which the strip always
  // owns. A terminal too short to spare those rows keeps the lists and drops the
  // verse instead.
  const std::string votd_text = bible_verse_of_the_day();
  std::vector<std::string> votd_lines;
  if (usable_h >= kHomeStripMinRows && !votd_text.empty())
  {
    votd_lines = wrap_quote(votd_text, std::max(8, content_w - 6), 2);
  }
  const int strip_rows = votd_lines.empty() ? 0 : (int)votd_lines.size() + 1;
  const int row_limit = std::max(section_y + 1, usable_h - 1 - strip_rows);

  home_menu_entries.clear();

  const std::string needle = lowered_copy(home_filter);
  // The typed text matches a row's label, its dimmed tail or the path it opens,
  // so "2h" finds the folder touched last and "src" the checkout.
  auto keep = [&](const std::string &label, const std::string &secondary, const std::string &path)
  {
    if (row_matches(needle, label, secondary))
    {
      return true;
    }
    return !needle.empty() && !path.empty() && lowered_copy(path).find(needle) != std::string::npos;
  };

  // Pins are split here so a pinned folder shows up once, as a project, and not
  // again through the recent list it is also in.
  std::vector<std::string> pinned_dirs;
  std::vector<std::string> pinned_files;
  for (const auto &pin : home_pinned)
  {
    std::error_code pin_ec;
    if (fs::is_directory(pin, pin_ec) && !pin_ec)
    {
      pinned_dirs.push_back(pin);
    }
    else if (fs::exists(pin, pin_ec) && !pin_ec)
    {
      pinned_files.push_back(pin);
    }
  }
  auto pinned_dir = [&](const std::string &path)
  { return std::find(pinned_dirs.begin(), pinned_dirs.end(), path) != pinned_dirs.end(); };
  auto pinned_file = [&](const std::string &path)
  { return std::find(pinned_files.begin(), pinned_files.end(), path) != pinned_files.end(); };
  // Parent folder and age together: the tail is what survives the truncation,
  // and the age is what tells two same-named checkouts apart.
  auto join_meta = [&](const std::string &path)
  {
    const std::string age = relative_age(path);
    return age.empty() ? display_parent(path) : display_parent(path) + "  " + age;
  };

  std::vector<HomeMenuRenderItem> items;
  if (!recent_workspaces.empty() || !recent_files.empty())
  {
    HomeMenuRenderItem resume;
    resume.action = HOME_ACTION_RESUME;
    if (!recent_workspaces.empty())
    {
      resume.recent_workspace_index = 0;
      resume.path = recent_workspaces.front();
      resume.icon = folder_icon();
      resume.icon_fg = theme.fg_sidebar_directory;
      resume.label = "Resume " + get_filename(recent_workspaces.front());
      resume.secondary = recent_workspaces.front();
    }
    else
    {
      resume.recent_index = 0;
      resume.path = recent_files.front();
      const jot_icons::FileTypeIcon type_icon = jot_icons::file_type_icon(recent_files.front());
      resume.icon = type_icon.glyph;
      resume.icon_fg = type_icon.color;
      resume.label = "Resume " + get_filename(recent_files.front());
      resume.secondary = recent_files.front();
    }
    items.push_back(std::move(resume));
  }
  // Every command row leads with its own glyph and accent; the letter at the
  // right edge is the key that runs it while the screen is up.
  auto action_row = [&](int action,
                        const char *icon,
                        int icon_fg,
                        const std::string &label,
                        const std::string &secondary,
                        char key)
  {
    HomeMenuRenderItem row;
    row.action = action;
    row.icon = icon;
    row.icon_fg = icon_fg;
    row.label = label;
    row.secondary = secondary;
    row.key = key;
    return row;
  };
  items.push_back(
      {HOME_ACTION_OPEN_PATH_PROMPT, -1, -1, "  Open Folder / File", "open ", "", 'o'});
  items.push_back(
      {HOME_ACTION_OPEN_RECENT_PROMPT, -1, -1, "󰱼  Open Recent", "openrecent ", "", 'r'});
  items.push_back({HOME_ACTION_NEW_FILE, -1, -1, "  New File", "", "", 'n'});
  items.push_back({HOME_ACTION_COMMAND_PALETTE, -1, -1, "  Command Palette", "", "", 'p'});
  items.push_back({HOME_ACTION_THEME_CHOOSER, -1, -1, "󰔎  Theme", "", "", 't'});
  items.push_back({HOME_ACTION_CONTINUE_EDITOR, -1, -1, "󰋖  Continue Editing", "", "", 'e'});

  // Quit is a row now that the screen lists it, and a row is easier to find
  // than the chord it also keeps.
  items.push_back({HOME_ACTION_QUIT, -1, -1, "  Quit", "", "", 'q'});
  items.erase(std::remove_if(items.begin(),
                             items.end(),
                             [&](const HomeMenuRenderItem &row)
                             { return !keep(row.label, row.secondary, row.path); }),
              items.end());

  // ---- The lists: projects, this folder, then the recents ----
  // Each keeps more candidates than it can show so a filter can still reach a
  // row the layout would not have had room for.
  const int kCandidateCap = 24;
  std::vector<HomeSection> sections;

  HomeSection projects;
  projects.title = "Projects";
  projects.icon = folder_icon();
  projects.fg = theme.fg_function;
  projects.empty_label = "  Nothing found nearby";
  projects.cap = 6;
  for (const auto &dir : pinned_dirs)
  {
    HomeMenuRenderItem row;
    row.action = HOME_ACTION_OPEN_PATH;
    row.path = dir;
    row.icon = "\u2605";
    row.icon_fg = theme.fg_number;
    row.label = get_filename(dir);
    row.secondary = join_meta(dir);
    if (keep(row.label, row.secondary, row.path))
    {
      projects.items.push_back(std::move(row));
    }
  }
  for (const auto &project : scan_projects())
  {
    if (pinned_dir(project.path))
    {
      continue;
    }
    HomeMenuRenderItem row;
    row.action = HOME_ACTION_OPEN_PATH;
    row.path = project.path;
    row.icon = folder_icon();
    row.icon_fg = theme.fg_sidebar_directory;
    row.label = get_filename(project.path);
    row.secondary = join_meta(project.path);
    if (keep(row.label, row.secondary, row.path))
    {
      projects.items.push_back(std::move(row));
    }
    if ((int)projects.items.size() >= kCandidateCap)
    {
      break;
    }
  }
  sections.push_back(std::move(projects));

  HomeSection here;
  here.title = launch.empty() ? "Here" : "Here  " + get_filename(launch);
  here.icon = "\uf015";
  here.fg = theme.fg_string;
  here.empty_label = "  This folder is empty";
  here.cap = 7;
  if (!launch.empty())
  {
    for (const auto &entry : browse_dir(launch))
    {
      HomeMenuRenderItem row;
      row.action = HOME_ACTION_OPEN_PATH;
      row.path = entry.path;
      row.icon = entry.dir ? folder_icon() : icon_for_path(entry.path);
      row.icon_fg =
          entry.dir ? theme.fg_sidebar_directory : jot_icons::file_type_icon(entry.path).color;
      row.label = entry.name;
      row.secondary = relative_age(entry.path);
      if (keep(row.label, row.secondary, row.path))
      {
        here.items.push_back(std::move(row));
      }
      if ((int)here.items.size() >= kCandidateCap)
      {
        break;
      }
    }
  }
  sections.push_back(std::move(here));

  HomeSection folders;
  folders.title = "Recent Folders";
  folders.icon = "\uf07c";
  folders.fg = theme.fg_type;
  folders.empty_label = "  No recent folders";
  folders.cap = 5;
  for (int i = 0; i < (int)recent_workspaces.size() && (int)folders.items.size() < kCandidateCap;
       i++)
  {
    const std::string &path = recent_workspaces[i];
    if (pinned_dir(path))
    {
      continue;
    }
    HomeMenuRenderItem row;
    row.action = HOME_ACTION_OPEN_RECENT_WORKSPACE;
    row.recent_workspace_index = i;
    row.path = path;
    row.icon = folder_icon();
    row.icon_fg = theme.fg_sidebar_directory;
    row.label = get_filename(path);
    row.secondary = join_meta(path);
    if (keep(row.label, row.secondary, row.path))
    {
      folders.items.push_back(std::move(row));
    }
  }
  sections.push_back(std::move(folders));

  HomeSection files;
  files.title = "Recent Files";
  files.icon = "\uf15b";
  files.fg = theme.fg_number;
  files.empty_label = "  No recent files";
  files.cap = 5;
  for (const auto &pin : pinned_files)
  {
    HomeMenuRenderItem row;
    row.action = HOME_ACTION_OPEN_PATH;
    row.path = pin;
    row.icon = "\u2605";
    row.icon_fg = theme.fg_number;
    row.label = get_filename(pin);
    row.secondary = join_meta(pin);
    if (keep(row.label, row.secondary, row.path))
    {
      files.items.push_back(std::move(row));
    }
  }
  for (int i = 0; i < (int)recent_files.size() && (int)files.items.size() < kCandidateCap; i++)
  {
    const std::string &path = recent_files[i];
    if (pinned_file(path))
    {
      continue;
    }
    HomeMenuRenderItem row;
    row.action = HOME_ACTION_OPEN_RECENT;
    row.recent_index = i;
    row.path = path;
    const jot_icons::FileTypeIcon type_icon = jot_icons::file_type_icon(path);
    row.icon = type_icon.glyph;
    row.icon_fg = type_icon.color;
    row.label = get_filename(path);
    row.secondary = join_meta(path);
    if (keep(row.label, row.secondary, row.path))
    {
      files.items.push_back(std::move(row));
    }
  }
  sections.push_back(std::move(files));

  // Room is handed out a section at a time, headers included, so a short
  // terminal still shows every list instead of only the first one.
  {
    int alive = 0;
    for (const auto &section : sections)
    {
      alive += section.items.empty() ? 0 : 1;
    }
    int remaining = std::max(0, row_limit - section_y);
    for (auto &section : sections)
    {
      if (section.items.empty())
      {
        continue;
      }
      const int rows = std::min<int>({remaining - alive, (int)section.items.size(), section.cap});
      section.show = std::max(0, rows);
      remaining -= section.show + 1;
      alive--;
    }
  }

  // The key printed on a row is the key that opens it, handed out in layout
  // order so a digit always means "the Nth row you can see".
  char next_digit = '1';
  for (auto &section : sections)
  {
    for (int i = 0; i < section.show; i++)
    {
      if (next_digit <= '9')
      {
        section.items[i].key = next_digit++;
      }
    }
  }

  // Build the full home model first (rows + native hit rects), then hand it
  // to a Lua UI handler when one is registered; otherwise paint it natively.
  // Mouse and keyboard input keep working because home_menu_entries (the
  // rects they hit-test) are populated during the model build either way.
  HomeView view;
  view.panel_x = content_x;
  view.panel_y = content_y;
  view.panel_w = content_w;
  view.panel_h = content_h;
  view.wordmark = "JOT";
  if (big_wordmark)
  {
    view.wordmark_art = wordmark_art();
  }
  view.tagline = "Developer workspace";
  view.context = context;
  view.context_icon = "\uf015";
  view.filter =
      home_filter_armed || !home_filter.empty() ? "Filter  " + home_filter : std::string();
  view.hint = home_filter.empty() && !home_filter_armed
                  ? "/ filter  f pin  j/k move  Enter open  Esc close"
                  : "Esc clear  Enter open  backspace edit";

  // The bottom strip's content. The verse is wrapped above, so both painters
  // only center what they are handed; the reference sits under it, and the
  // footer -- the build's own date and the credit line -- is always on the
  // panel's last row. The verse comes from the worker queue and is cached for
  // the whole day, so the fetch this triggers is a no-op after the day's first
  // frame: while it is in flight the strip is simply absent, and the delivery
  // repaints into the next build.
  view.quote_lines = std::move(votd_lines);
  view.quote_ref = view.quote_lines.empty() ? std::string() : bible_votd_label;
  view.last_updated = "Last updated " + build_date_label();
  view.developed_by = "Developed by Joshua Ravael";

  // The command rows keep their glyph at the head of the label -- the label is
  // what the filter reads -- so the view splits it back out here and gives it
  // the row's accent. List rows bring their own icon and stay on the text
  // color.
  auto command_accent = [&](int action) -> int
  {
    switch (action)
    {
    case HOME_ACTION_OPEN_PATH_PROMPT:
      return theme.fg_function;
    case HOME_ACTION_OPEN_RECENT_PROMPT:
      return theme.fg_type;
    case HOME_ACTION_NEW_FILE:
      return theme.fg_string;
    case HOME_ACTION_COMMAND_PALETTE:
      return theme.fg_keyword;
    case HOME_ACTION_THEME_CHOOSER:
      return theme.fg_number;
    case HOME_ACTION_CONTINUE_EDITOR:
      return theme.fg_function;
    case HOME_ACTION_QUIT:
      return theme.fg_diagnostic_error;
    default:
      return -1;
    }
  };

  auto add_section = [&](int x, int y, const HomeSection &section, int width)
  {
    if (y >= row_limit || width <= 0)
    {
      return;
    }
    HomeEntryView r;
    r.section = true;
    r.icon = section.icon;
    r.icon_fg = section.fg;
    r.fg = section.fg;
    r.label = ellipsize_right(section.title, width);
    r.x = x;
    r.y = y;
    r.w = width;
    view.rows.push_back(std::move(r));
  };

  auto add_home_item = [&](const HomeMenuRenderItem &item, int x, int y, int width) -> bool
  {
    if (y >= row_limit || width <= 0)
    {
      return false;
    }
    HomeEntryView r;
    r.x = x;
    r.y = y;
    r.w = width;
    r.selected = ((int)home_menu_entries.size() == home_menu_selected);
    r.icon = item.icon;
    r.icon_fg = item.icon_fg;
    r.fg = command_accent(item.action);
    // The icon takes its own column and the gap after it, so the label gives
    // that width up before it is truncated.
    const int icon_cells = item.icon.empty() ? 0 : ui_cell_count(item.icon) + 1;
    r.label = ellipsize_right(item.label, std::max(0, width - 2 - icon_cells));
    if (!item.secondary.empty() && width > 34)
    {
      r.secondary = shorten_tail(item.secondary, width / 2);
    }
    if (item.key != 0)
    {
      r.key = std::string(1, item.key);
    }
    view.rows.push_back(std::move(r));
    home_menu_entries.push_back({item.action,
                                 item.recent_index,
                                 item.recent_workspace_index,
                                 x,
                                 y,
                                 width,
                                 item.path,
                                 item.key});
    return true;
  };

  // The actions column's own title: a play glyph in the green that says "go".
  HomeSection start;
  start.title = "Start";
  start.icon = "\uf04b";
  start.fg = theme.fg_string;
  int action_y = section_y;
  add_section(action_x, action_y, start, action_w);
  action_y += 2;
  for (const auto &item : items)
  {
    if (add_home_item(item, action_x, action_y, action_w))
    {
      action_y++;
    }
  }

  int list_y = two_column ? section_y : action_y + 1;
  for (const auto &section : sections)
  {
    if (section.items.empty())
    {
      // An empty list still says so, unless a filter is what emptied it.
      if (!needle.empty())
      {
        continue;
      }
      add_section(recent_x, list_y, section, recent_w);
      list_y++;
      if (list_y >= row_limit)
      {
        continue;
      }
      HomeEntryView r;
      r.label = ellipsize_right(section.empty_label, recent_w);
      r.x = recent_x;
      r.y = list_y;
      r.w = recent_w;
      view.rows.push_back(std::move(r));
      list_y++;
      continue;
    }
    if (section.show == 0)
    {
      continue; // no room at all: a header on its own would say nothing
    }
    add_section(recent_x, list_y, section, recent_w);
    list_y++;
    for (int i = 0; i < section.show && list_y < row_limit; i++)
    {
      if (add_home_item(section.items[i], recent_x, list_y, recent_w))
      {
        list_y++;
      }
    }
  }

  if (home_menu_entries.empty())
  {
    home_menu_selected = 0;
  }
  else
  {
    home_menu_selected = std::clamp(home_menu_selected, 0, (int)home_menu_entries.size() - 1);
  }
  home_model_filter = home_filter;
  return view;
}

void Editor::render_home_menu()
{
  if (!show_home_menu)
  {
    return;
  }

  const HomeView view = build_home_menu_model();
  const int content_x = view.panel_x;
  const int content_y = view.panel_y;
  const int content_w = view.panel_w;
  const int content_h = view.panel_h;
  if (lua_api && lua_api->has_lua_ui_handler("home_screen") && lua_api->emit_home(view))
  {
    return;
  }

  // Native fallback paint (no Lua handler registered): the Lua float paints
  // the header itself, so the big wordmark, its tagline row and the context
  // line only need drawing on this path. This mirrors the shape the Lua
  // painter composes (features/ui/home.lua).
  const int letter_fg[3] = {theme.fg_keyword, theme.fg_function, theme.fg_type};
  int header_y = content_y;
  for (const auto &letters : view.wordmark_art)
  {
    int x = content_x;
    for (size_t i = 0; i < letters.size(); i++)
    {
      ui->draw_text(
          x, header_y, letters[i], letter_fg[std::min(i, (size_t)2)], theme.bg_default, true);
      x += ui_cell_count(letters[i]) + 1;
    }
    header_y++;
  }
  if (view.wordmark_art.empty())
  {
    ui->draw_text(content_x, header_y, view.wordmark, theme.fg_keyword, theme.bg_default, true);
    header_y++;
  }
  if (!view.tagline.empty())
  {
    ui->draw_text(content_x, header_y, "\uf121", theme.fg_keyword, theme.bg_default);
    ui->draw_text(content_x + 2, header_y, view.tagline, theme.fg_comment, theme.bg_default);
  }
  const std::string hint = view.hint;
  const int hint_x = content_x + content_w - ui_cell_count(hint) - 1;
  if (hint_x > content_x + 26)
  {
    ui->draw_text(hint_x, header_y, hint, theme.fg_comment, theme.bg_default);
  }
  const int context_y = header_y + 1;
  if (view.filter.empty() && !view.context_icon.empty())
  {
    ui->draw_text(content_x, context_y, view.context_icon, theme.fg_keyword, theme.bg_default);
    ui->draw_text(content_x + ui_cell_count(view.context_icon) + 1,
                  context_y,
                  ellipsize_right(view.context,
                                  std::max(0, content_w - ui_cell_count(view.context_icon) - 3)),
                  theme.fg_default,
                  theme.bg_default);
  }
  else
  {
    ui->draw_text(content_x,
                  context_y,
                  ellipsize_right(view.filter.empty() ? view.context : view.filter,
                                  std::max(0, content_w - 2)),
                  theme.fg_default,
                  theme.bg_default);
  }
  for (const auto &row : view.rows)
  {
    if (row.section)
    {
      int x = row.x;
      const int fg = row.fg >= 0 ? row.fg : theme.fg_sidebar_directory;
      if (!row.icon.empty())
      {
        ui->draw_text(
            x, row.y, row.icon, row.icon_fg >= 0 ? row.icon_fg : fg, theme.bg_default, true);
        x += ui_cell_count(row.icon) + 1;
      }
      ui->draw_text(x,
                    row.y,
                    ellipsize_right(row.label, std::max(0, row.w - (x - row.x))),
                    fg,
                    theme.bg_default,
                    true);
      continue;
    }
    const bool selected = row.selected;
    const int fg = selected ? theme.fg_selection : (row.fg >= 0 ? row.fg : theme.fg_default);
    const int bg = selected ? theme.bg_selection : theme.bg_default;
    const int icon_cells = row.icon.empty() ? 0 : ui_cell_count(row.icon) + 1;
    // The band hugs the label (and its icon) rather than the whole row: the
    // panel is much wider than the names it lists, and a bar run edge to edge
    // reads as a rule across the screen instead of a highlight on the row.
    if (selected)
    {
      UIRect band = {row.x + 1, row.y, icon_cells + ui_cell_count(row.label), 1};
      ui->fill_rect(band, " ", fg, bg);
    }
    int label_x = row.x + 1;
    if (!row.icon.empty())
    {
      ui->draw_text(label_x, row.y, row.icon, row.icon_fg >= 0 ? row.icon_fg : fg, bg);
      label_x += icon_cells;
    }
    ui->draw_text(label_x, row.y, row.label, fg, bg, selected);
    const int key_gap = row.key.empty() ? 0 : 2;
    if (!row.secondary.empty())
    {
      std::string secondary = row.secondary;
      int sx = row.x + row.w - (int)secondary.size() - 1 - key_gap;
      if (sx > label_x + (int)row.label.size() + 2)
      {
        ui->draw_text(sx, row.y, secondary, theme.fg_comment, theme.bg_default);
      }
    }
    if (!row.key.empty())
    {
      ui->draw_text(row.x + row.w - (int)row.key.size() - 1,
                    row.y,
                    row.key,
                    theme.fg_keyword,
                    theme.bg_default);
    }
  }

  // The bottom strip's rows, painted the shape the Lua kit composes
  // (features/ui/home.lua): the verse centered over its reference, the footer on
  // the panel's last row. The model leaves these rows to the lists, so nothing
  // above paints into them.
  const auto centered_x = [&](const std::string &text)
  { return content_x + std::max(0, (content_w - ui_cell_count(text)) / 2); };
  int strip_y = content_y + content_h - 1; // the panel's last row
  if (!view.last_updated.empty() || !view.developed_by.empty())
  {
    if (!view.last_updated.empty())
    {
      ui->draw_text(content_x + 2, strip_y, view.last_updated, theme.fg_comment, theme.bg_default);
    }
    if (!view.developed_by.empty())
    {
      ui->draw_text(content_x + content_w - ui_cell_count(view.developed_by) - 2,
                    strip_y,
                    view.developed_by,
                    theme.fg_comment,
                    theme.bg_default);
    }
    strip_y--;
  }
  if (!view.quote_ref.empty())
  {
    ui->draw_text(
        centered_x(view.quote_ref), strip_y, view.quote_ref, theme.fg_keyword, theme.bg_default);
    strip_y--;
  }
  for (auto it = view.quote_lines.rbegin(); it != view.quote_lines.rend(); ++it)
  {
    ui->draw_text(centered_x(*it), strip_y, *it, theme.fg_comment, theme.bg_default);
    strip_y--;
  }
}

// --- home screen (test) ---
// The rows the model just built, in the order a digit press walks them, plus
// the state the keys read. A case pins what a key opens without driving the
// paint (the painted digits are asserted separately in test_home_open.cpp).
std::vector<std::string> Editor::home_row_paths_for_test() const
{
  std::vector<std::string> paths;
  paths.reserve(home_menu_entries.size());
  for (const HomeMenuEntry &entry : home_menu_entries)
  {
    paths.push_back(entry.path);
  }
  return paths;
}

std::vector<char> Editor::home_row_keys_for_test() const
{
  std::vector<char> keys;
  keys.reserve(home_menu_entries.size());
  for (const HomeMenuEntry &entry : home_menu_entries)
  {
    keys.push_back(entry.key);
  }
  return keys;
}

void Editor::home_input_for_test(int ch)
{
  handle_input(ch, false, false, false, ch);
}

void Editor::home_scan_cache_clear_for_test()
{
  scan_cache().clear();
}

bool Editor::handle_home_menu_input(int ch, bool is_ctrl, bool is_shift, bool is_alt)
{
  (void)is_shift;
  (void)is_alt;
  if (!show_home_menu)
  {
    return false;
  }

  // Leaving the screen always drops the query with it, so the next open starts
  // on the real lists instead of on a filter the user cannot see.
  auto dismiss_home = [&]()
  {
    show_home_menu = false;
    home_filter.clear();
    home_filter_armed = false;
    home_model_filter.clear();
  };

  // Keys and frames are two clocks: a paste, or a hand faster than the frame,
  // delivers several keys between two renders, and the rows Enter, a digit and
  // a pin act on are the ones the render built. Rebuild here so those keys act
  // on what is on the screen rather than on the list from before the query.
  if (home_menu_panel_w > 0 && home_model_filter != home_filter)
  {
    build_home_menu_model();
  }

  auto open_recent_by_index = [&](int idx) -> bool
  {
    if (idx < 0 || idx >= (int)recent_files.size())
    {
      return false;
    }
    dismiss_home();
    open_recent_file(std::to_string(idx + 1));
    needs_redraw = true;
    return true;
  };

  auto open_workspace_by_index = [&](int idx) -> bool
  {
    if (idx < 0 || idx >= (int)recent_workspaces.size())
    {
      return false;
    }
    std::error_code ec;
    if (!std::filesystem::exists(recent_workspaces[idx], ec) || ec
        || !std::filesystem::is_directory(recent_workspaces[idx], ec))
    {
      recent_workspaces.erase(recent_workspaces.begin() + idx);
      set_message("Workspace not found");
      needs_redraw = true;
      return true;
    }
    dismiss_home();
    open_workspace(recent_workspaces[idx], true);
    needs_redraw = true;
    return true;
  };

  auto is_startup_pristine = [&]()
  {
    if (buffers.size() != 1)
    {
      return false;
    }
    const auto &buf = buffers[0];
    return buf.filepath.empty() && !buf.modified && buf.line_count() == 1 && buf.line(0).empty();
  };

  auto execute_entry = [&](const HomeMenuEntry &entry) -> bool
  {
    switch (entry.action)
    {
    case HOME_ACTION_RESUME:
      if (entry.recent_workspace_index >= 0)
      {
        return open_workspace_by_index(entry.recent_workspace_index);
      }
      return open_recent_by_index(entry.recent_index);
    case HOME_ACTION_OPEN_RECENT:
      return open_recent_by_index(entry.recent_index);
    case HOME_ACTION_OPEN_RECENT_WORKSPACE:
      return open_workspace_by_index(entry.recent_workspace_index);
    case HOME_ACTION_NEW_FILE:
      dismiss_home();
      if (!is_startup_pristine())
      {
        create_new_buffer();
      }
      set_message("Ready: new file");
      needs_redraw = true;
      return true;
    case HOME_ACTION_OPEN_RECENT_PROMPT:
      dismiss_home();
      show_command_palette = true;
      command_palette_query = "openrecent ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
      return true;
    case HOME_ACTION_OPEN_PATH_PROMPT:
      dismiss_home();
      show_command_palette = true;
      command_palette_query = "open ";
      command_palette_results.clear();
      command_palette_selected = 0;
      command_palette_theme_mode = false;
      command_palette_theme_original.clear();
      refresh_command_palette();
      needs_redraw = true;
      return true;
    case HOME_ACTION_COMMAND_PALETTE:
      dismiss_home();
      toggle_command_palette();
      needs_redraw = true;
      return true;
    case HOME_ACTION_THEME_CHOOSER:
      dismiss_home();
      open_theme_chooser();
      needs_redraw = true;
      return true;
    case HOME_ACTION_CONTINUE_EDITOR:
      dismiss_home();
      set_message("Home hidden");
      needs_redraw = true;
      return true;
    case HOME_ACTION_OPEN_PATH:
    {
      if (entry.path.empty())
      {
        return false;
      }
      std::error_code open_ec;
      if (!std::filesystem::exists(entry.path, open_ec) || open_ec)
      {
        set_message("Missing: " + entry.path);
        needs_redraw = true;
        return true;
      }
      dismiss_home();
      home_filter.clear();
      home_filter_armed = false;
      if (std::filesystem::is_directory(entry.path, open_ec) && !open_ec)
      {
        open_workspace(entry.path, true);
      }
      else
      {
        open_file(entry.path);
      }
      needs_redraw = true;
      return true;
    }
    case HOME_ACTION_QUIT:
      running = false;
      return true;
    default:
      return false;
    }
  };

  if (is_ctrl && (ch == 'q' || ch == 'Q'))
  {
    bool unsaved = false;
    for (const auto &buffer : buffers)
    {
      if (buffer.modified)
      {
        unsaved = true;
        break;
      }
    }
    dismiss_home();
    if (unsaved)
    {
      show_quit_prompt = true;
      needs_redraw = true;
    }
    else
    {
      running = false;
    }
    return true;
  }

  const bool filtering = !home_filter.empty() || home_filter_armed;

  if (ch == 27)
  {
    if (filtering)
    {
      // The first Esc takes back what was typed, the second closes the screen.
      home_filter.clear();
      home_filter_armed = false;
      home_menu_selected = 0;
      needs_redraw = true;
      return true;
    }
    dismiss_home();
    set_message("Home hidden");
    needs_redraw = true;
    return true;
  }

  if (is_ctrl || is_alt)
  {
    // A chord is a shortcut, not filter text: hand it back to the dispatcher
    // after dropping the screen, the way a printable key used to.
    dismiss_home();
    needs_redraw = true;
    return false;
  }

  if (ch == 127 || ch == 8)
  {
    if (!home_filter.empty())
    {
      home_filter.pop_back();
      needs_redraw = true;
      return true;
    }
    if (home_filter_armed)
    {
      home_filter_armed = false;
      needs_redraw = true;
    }
    return true;
  }

  // A digit opens the row that prints it, which is what makes the keys on the
  // rows worth printing: they are the layout's order, not the list's.
  if (!filtering && ch >= '1' && ch <= '9')
  {
    for (int i = 0; i < (int)home_menu_entries.size(); i++)
    {
      if (home_menu_entries[i].key == (char)ch)
      {
        home_menu_selected = i;
        return execute_entry(home_menu_entries[i]);
      }
    }
  }

  const int count = (int)home_menu_entries.size();
  if (count > 0)
  {
    if (ch == 1008 || (!filtering && (ch == 'k' || ch == 'K')))
    {
      home_menu_selected = (home_menu_selected - 1 + count) % count;
      needs_redraw = true;
      return true;
    }
    if (ch == 1009 || (!filtering && (ch == 'j' || ch == 'J')))
    {
      home_menu_selected = (home_menu_selected + 1) % count;
      needs_redraw = true;
      return true;
    }
    if (ch == 1012)
    {
      home_menu_selected = 0;
      needs_redraw = true;
      return true;
    }
    if (ch == 1013)
    {
      home_menu_selected = count - 1;
      needs_redraw = true;
      return true;
    }
    if (ch == '\n' || ch == 13)
    {
      return execute_entry(home_menu_entries[home_menu_selected]);
    }
  }

  if (filtering)
  {
    // Once something is typed every printable key is part of the query, so a
    // letter that is also a shortcut cannot cut the filter short.
    if (ch >= 32 && ch < 127)
    {
      home_filter.push_back((char)ch);
      home_menu_selected = 0;
      needs_redraw = true;
      return true;
    }
    return true;
  }

  if (ch == 'o' || ch == 'O')
  {
    HomeMenuEntry e = {HOME_ACTION_OPEN_PATH_PROMPT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == '/')
  {
    // Arms the filter without typing a character, for a query that starts with
    // one of the shortcut letters.
    home_filter_armed = true;
    needs_redraw = true;
    return true;
  }
  if (ch == 'f' || ch == 'F')
  {
    if (home_menu_selected >= 0 && home_menu_selected < count)
    {
      const std::string &path = home_menu_entries[home_menu_selected].path;
      if (!path.empty())
      {
        toggle_home_pin(path);
        return true;
      }
    }
    return true;
  }
  if (ch == 'n' || ch == 'N')
  {
    HomeMenuEntry e = {HOME_ACTION_NEW_FILE, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'p' || ch == 'P')
  {
    HomeMenuEntry e = {HOME_ACTION_COMMAND_PALETTE, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 't' || ch == 'T')
  {
    HomeMenuEntry e = {HOME_ACTION_THEME_CHOOSER, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 's' || ch == 'S')
  {
    HomeMenuEntry e = {HOME_ACTION_OPEN_PATH_PROMPT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'r' || ch == 'R')
  {
    HomeMenuEntry e = {HOME_ACTION_OPEN_RECENT_PROMPT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'w' || ch == 'W')
  {
    if (open_workspace_by_index(0))
    {
      return true;
    }
  }
  if (ch == 'e' || ch == 'E')
  {
    HomeMenuEntry e = {HOME_ACTION_CONTINUE_EDITOR, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }
  if (ch == 'q' || ch == 'Q')
  {
    HomeMenuEntry e = {HOME_ACTION_QUIT, -1, -1, 0, 0, 0};
    return execute_entry(e);
  }

  if (ch >= 32 && ch < 127)
  {
    // Typing filters the screen instead of dismissing it: Enter then opens the
    // top match, and Esc (or Continue Editing) is how you get back to editing.
    home_filter.push_back((char)ch);
    home_menu_selected = 0;
    needs_redraw = true;
    return true;
  }

  return true;
}

bool Editor::handle_home_menu_mouse(int x, int y, bool is_click)
{
  if (!show_home_menu)
  {
    return false;
  }

  for (int i = 0; i < (int)home_menu_entries.size(); i++)
  {
    const auto &entry = home_menu_entries[i];
    if (y == entry.y && x >= entry.x && x < entry.x + entry.w)
    {
      if (home_menu_selected != i)
      {
        home_menu_selected = i;
        needs_redraw = true;
      }
      if (is_click)
      {
        return handle_home_menu_input('\n', false, false, false);
      }
      return true;
    }
  }

  bool inside_panel = x >= home_menu_panel_x && x < home_menu_panel_x + home_menu_panel_w
                      && y >= home_menu_panel_y && y < home_menu_panel_y + home_menu_panel_h;
  if (inside_panel)
  {
    return true;
  }

  if (!is_click)
  {
    return true;
  }

  show_home_menu = false;
  needs_redraw = true;
  return false;
}
