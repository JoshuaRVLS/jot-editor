// The bundled themes: jot-dark and jot-light, the two schemes jot ships.
//
// A theme is data that fails quietly. A typo'd group name paints nothing and is
// invisible until someone notices one token type is the wrong colour; a name
// that no longer resolves leaves the editor on the built-in 16-colour defaults
// with only "Unknown theme" to say so; and a slot added to one theme but not the
// other silently falls back to that same default. This copies the shipped files
// into the config dir the editor reads, applies them, and asserts the colours
// that came out of the engine -- plus the two names that used to be bundled
// (`dark`, `light`) and still have to work.
#include "editor.h"
#include "jot/lua/api.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
  // "<repo>/.configs/configs/colors" from the compile-time runtime path, which
  // every test target has: "<repo>/runtime/lua".
  fs::path bundled_themes_dir()
  {
    return fs::path(JOT_LUA_SOURCE_DIR).parent_path().parent_path() / ".configs" / "configs" / "colors";
  }

  // Group names a theme file sets, with their fg/bg. The reader in the engine is
  // regex-based, so this is a small parser rather than a dependency: find
  // `"group": { ... }` and pull the two numbers out of the body.
  std::map<std::string, std::pair<int, int>> parse_theme(const fs::path &path)
  {
    std::ifstream in(path);
    REQUIRE(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::map<std::string, std::pair<int, int>> out;
    size_t pos = 0;
    while ((pos = text.find('"', pos)) != std::string::npos)
    {
      const size_t name_end = text.find('"', pos + 1);
      if (name_end == std::string::npos)
        break;
      const std::string name = text.substr(pos + 1, name_end - pos - 1);
      const size_t body_start = text.find('{', name_end);
      const size_t body_end = body_start == std::string::npos ? std::string::npos : text.find('}', body_start);
      if (body_start == std::string::npos || body_end == std::string::npos)
        break;
      const std::string body = text.substr(body_start, body_end - body_start);
      auto number = [&](const char *key) -> int
      {
        const size_t at = body.find(std::string("\"") + key + "\"");
        if (at == std::string::npos)
          return -1;
        const size_t colon = body.find(':', at);
        return colon == std::string::npos ? -1 : std::stoi(body.substr(colon + 1));
      };
      out[name] = {number("fg"), number("bg")};
      pos = body_end + 1;
    }
    return out;
  }

  // The editor every case shares: a temp config home seeded with the bundled
  // themes, so the shipped files are what gets applied.
  Editor &probe_editor()
  {
    static bool seeded = false;
    if (!seeded)
    {
      char cfgdir[] = "/tmp/jot_themes_test_XXXXXX";
      REQUIRE(mkdtemp(cfgdir) != nullptr);
      setenv("JOT_CONFIG_HOME", cfgdir, 1);
      setenv("JOT_CACHE_HOME", cfgdir, 1);
      const fs::path colors = fs::path(cfgdir) / "configs" / "colors";
      fs::create_directories(colors);
      for (const char *name : {"jot-dark.json", "jot-light.json"})
      {
        fs::copy_file(bundled_themes_dir() / name, colors / name, fs::copy_options::overwrite_existing);
      }
      seeded = true;
    }
    static Editor e;
    return e;
  }
} // namespace

TEST_CASE("jot-dark applies the warm charcoal scheme", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-dark"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  const Theme &t = e.theme_for_test();
  REQUIRE(t.fg_default == 223);  // cream ink
  REQUIRE(t.bg_default == 234);  // charcoal, not the neutral 235 a generic dark theme uses
  REQUIRE(t.fg_keyword == 215);  // amber -- the signature
  REQUIRE(t.fg_string == 79);    // soft teal
  REQUIRE(t.fg_function_method == 79);
  REQUIRE(t.fg_number == 210);
  // The active border carries the accent rather than a blue: this is what makes
  // jot's chrome read as its own palette.
  REQUIRE(t.fg_active_border == 215);
}

TEST_CASE("jot-light is the same scheme on warm paper", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");

  const Theme &t = e.theme_for_test();
  REQUIRE(t.fg_default == 235);
  REQUIRE(t.bg_default == 230);  // cream paper, not white
  REQUIRE(t.fg_keyword == 130);
  REQUIRE(t.fg_string == 29);
  REQUIRE(t.fg_function_method == 66);
  REQUIRE(t.fg_active_border == 130);
}

TEST_CASE("The names the removed catalog used still resolve", "[jot][theme]")
{
  Editor &e = probe_editor();

  // Both aliases land on the jot theme that replaced them, and say so: a config
  // written as `color_scheme = "dark"` keeps working, and the chooser shows what
  // is actually painted.
  REQUIRE(e.apply_theme_for_test("dark"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");
  REQUIRE(e.theme_for_test().bg_default == 234);

  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(e.theme_for_test().bg_default == 230);

  // Case does not matter, the same way it never did for a real theme name.
  REQUIRE(e.apply_theme_for_test("DARK"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  // A theme that does not exist fails and leaves the current one alone, rather
  // than half-applying a palette.
  REQUIRE_FALSE(e.apply_theme_for_test("gruvbox"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");
  REQUIRE(e.theme_for_test().bg_default == 234);
}

TEST_CASE("A file the user writes under a legacy name beats the alias", "[jot][theme]")
{
  // The aliases exist for configs written before the catalog shrank, not to
  // take a name away from the user: their own `light.json` is the theme they
  // asked for, and it is applied as written (here on top of jot-light, which it
  // extends). The file is removed again so the shared config dir goes back to
  // the shipped two for the other cases.
  Editor &e = probe_editor();
  const fs::path colors = fs::path(getenv("JOT_CONFIG_HOME")) / "configs" / "colors";
  const fs::path mine = colors / "light.json";
  {
    std::ofstream out(mine);
    out << "{\n  \"extends\": \"jot-light\",\n  \"Normal\": {\"fg\": 16, \"bg\": 17}\n}\n";
  }

  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "light");
  REQUIRE(e.theme_for_test().bg_default == 17);
  // The base it extends is still jot-light, so everything the user did not
  // override keeps the shipped look.
  REQUIRE(e.theme_for_test().fg_keyword == 130);

  fs::remove(mine);
  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(e.theme_for_test().bg_default == 230);
}

TEST_CASE("The chooser lists jot's two themes and nothing else", "[jot][theme]")
{
  // jot ships no third-party catalog any more: the chooser (what `:theme`
  // opens) shows the two schemes the editor actually maintains. The test's
  // config dir holds a copy of each and the bundled tree holds the originals,
  // and they resolve to the same two names.
  Editor &e = probe_editor();
  const auto themes = e.available_themes_for_test();
  REQUIRE(themes.size() == 2);
  REQUIRE(std::find(themes.begin(), themes.end(), "jot-dark") != themes.end());
  REQUIRE(std::find(themes.begin(), themes.end(), "jot-light") != themes.end());

  // None of the names the removed catalog was keyed on is listed, including the
  // two that still resolve as aliases: a stale name must not look selectable.
  for (const char *gone : {"dark", "light", "gruvbox", "tokyonight", "catppuccin",
                           "onedark", "monokai", "solarized", "dracula", "nord"})
  {
    INFO("removed theme still listed: " << gone);
    REQUIRE(std::find(themes.begin(), themes.end(), gone) == themes.end());
  }
}

TEST_CASE("Both bundled themes define the same slots", "[jot][theme]")
{
  // A slot in one file but not the other does not fail loudly -- it falls back
  // to the struct default, which is a 16-colour value on the wrong palette.
  const auto dark = parse_theme(bundled_themes_dir() / "jot-dark.json");
  const auto light = parse_theme(bundled_themes_dir() / "jot-light.json");

  REQUIRE(dark.size() > 70);
  std::set<std::string> only_dark;
  std::set<std::string> only_light;
  for (const auto &entry : dark)
  {
    if (light.find(entry.first) == light.end())
      only_dark.insert(entry.first);
  }
  for (const auto &entry : light)
  {
    if (dark.find(entry.first) == dark.end())
      only_light.insert(entry.first);
  }
  INFO("dark only: " << only_dark.size() << ", light only: " << only_light.size());
  REQUIRE(only_dark.empty());
  REQUIRE(only_light.empty());

  // Every slot that names a background must name a foreground too: a group with
  // a bg but no fg paints text in the inherited colour, which on a light theme
  // is invisible.
  int bg_without_fg = 0;
  for (const auto &entry : dark)
  {
    if (entry.second.second >= 0 && entry.second.first < 0)
      bg_without_fg++;
  }
  // CursorLine is the one deliberate exception (it only tints the row).
  REQUIRE(bg_without_fg <= 1);
}
