// The bundled themes: jot-dark and jot-light, the two schemes jot ships.
//
// A theme is data that fails quietly. A typo'd group name paints nothing and is
// invisible until someone notices one token type is the wrong colour; a name
// that no longer resolves leaves the editor on the built-in 16-colour defaults
// with only "Unknown theme" to say so; a slot added to one theme but not the
// other silently falls back to that same default; and a colour the reader cannot
// parse leaves the slot at whatever it inherited. This copies the shipped files
// into the config dir the editor reads, applies them, and asserts the colours
// that came out of the engine -- plus the two names that used to be bundled
// (`dark`, `light`) and still have to work.
//
// Both shipped themes name exact 24-bit colours ("#f5b06b") rather than xterm
// palette indices, so a case that wants a slot's colour reads it back through
// the same conversion the renderer uses instead of comparing an index.
#include "editor.h"
#include "jot/lua/api.h"
#include "jot/lua/bindings_internal.h"
#include "ui/xterm_palette.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace fs = std::filesystem;

namespace
{
  // "<repo>/.configs/configs/colors" from the compile-time runtime path, which
  // every test target has: "<repo>/runtime/lua".
  fs::path bundled_themes_dir()
  {
    return fs::path(JOT_LUA_SOURCE_DIR).parent_path().parent_path() / ".configs" / "configs" / "colors";
  }

  // One slot as the file wrote it.
  struct SlotColor
  {
    bool present = false; // the key is in the group body
    bool hex = false;     // written as a "#rrggbb" string, not a palette index
    long long value = -1; // 0xRRGGBB for hex, the index otherwise (-1 = unset)
  };

  // The colour a theme slot resolved to, whichever form it carries: an exact
  // colour comes back as its rgb, an index through the palette table.
  long long rgb_of(int value)
  {
    unsigned char r = 0;
    unsigned char g = 0;
    unsigned char b = 0;
    if (!jot_ui::exact_color_rgb(value, r, g, b))
    {
      jot_ui::palette_rgb(value, r, g, b);
    }
    return ((long long)r << 16) | ((long long)g << 8) | b;
  }

  long long rgb_of(int r, int g, int b)
  {
    return ((long long)r << 16) | ((long long)g << 8) | b;
  }

  // Group names a theme file sets, with their fg/bg. The reader in the engine is
  // regex-based, so this is a small parser rather than a dependency: find
  // `"group": { ... }` and pull the two colours out of the body.
  std::map<std::string, std::pair<SlotColor, SlotColor>> parse_theme(const fs::path &path)
  {
    std::ifstream in(path);
    REQUIRE(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::map<std::string, std::pair<SlotColor, SlotColor>> out;
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
      auto color = [&](const char *key) -> SlotColor
      {
        SlotColor slot;
        const size_t at = body.find(std::string("\"") + key + "\"");
        if (at == std::string::npos)
          return slot;
        const size_t colon = body.find(':', at);
        if (colon == std::string::npos)
          return slot;
        const size_t first = body.find_first_not_of(" \t", colon + 1);
        if (first == std::string::npos || first >= body.size())
          return slot;
        slot.present = true;
        if (body[first] == '"')
        {
          const size_t end = body.find('"', first + 1);
          REQUIRE(end != std::string::npos);
          unsigned char r = 0;
          unsigned char g = 0;
          unsigned char b = 0;
          // Without the quotes: parse_hex_color takes the colour text itself.
          REQUIRE(jot_ui::parse_hex_color(body.substr(first + 1, end - first - 1), r, g, b));
          slot.hex = true;
          slot.value = rgb_of(r, g, b);
          return slot;
        }
        slot.value = std::stoll(body.substr(first));
        return slot;
      };
      out[name] = {color("fg"), color("bg")};
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

  // Writes a theme file into the shared config dir for the length of one case.
  class UserThemeFile
  {
  public:
    UserThemeFile(const std::string &name, const std::string &body)
        : path_(fs::path(getenv("JOT_CONFIG_HOME")) / "configs" / "colors" / (name + ".json"))
    {
      std::ofstream out(path_);
      out << body;
    }
    ~UserThemeFile()
    {
      std::error_code ec;
      fs::remove(path_, ec);
    }
    UserThemeFile(const UserThemeFile &) = delete;
    UserThemeFile &operator=(const UserThemeFile &) = delete;

    const std::string name() const
    {
      return path_.stem().string();
    }

  private:
    fs::path path_;
  };
} // namespace

TEST_CASE("jot-dark applies the warm charcoal scheme", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-dark"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  const Theme &t = e.theme_for_test();
  // Exact colours, not palette entries: the whole point of the hex form is that
  // the scheme is authored in 24-bit space.
  REQUIRE(jot_ui::is_exact_color(t.fg_default));
  REQUIRE(rgb_of(t.fg_default) == 0xE8DDCC); // cream ink
  REQUIRE(rgb_of(t.bg_default) == 0x1E1B18); // warm charcoal, not the neutral #1c1c1c
  REQUIRE(jot_ui::is_exact_color(t.bg_default));
  REQUIRE(rgb_of(t.fg_keyword) == 0xF5B06B); // amber -- the signature
  REQUIRE(rgb_of(t.fg_string) == 0x63CFA8);  // soft teal
  REQUIRE(rgb_of(t.fg_function_method) == 0x63CFA8);
  REQUIRE(rgb_of(t.fg_number) == 0xEF8D8D);
  // The active border carries the accent rather than a blue: this is what makes
  // jot's chrome read as its own palette.
  REQUIRE(rgb_of(t.fg_active_border) == 0xF5B06B);
}

TEST_CASE("jot-light is the same scheme on warm paper", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");

  const Theme &t = e.theme_for_test();
  REQUIRE(jot_ui::is_exact_color(t.fg_default));
  REQUIRE(rgb_of(t.fg_default) == 0x25201B);
  REQUIRE(rgb_of(t.bg_default) == 0xF9F4EA); // warm paper, not white
  REQUIRE(rgb_of(t.fg_keyword) == 0xA95C14);
  REQUIRE(rgb_of(t.fg_string) == 0x1D6B52);
  REQUIRE(rgb_of(t.fg_function_method) == 0x396F6C);
  REQUIRE(rgb_of(t.fg_active_border) == 0xA95C14);
}

TEST_CASE("Hex theme colours accept every documented form", "[jot][theme]")
{
  // The points of the hex form: an exact colour, so nothing is snapped to the
  // 256-entry grid, and any of the three spellings. A value the reader cannot
  // parse must leave the slot alone -- painting it black (or leaving the
  // previous theme's colour) is the failure this guards.
  Editor &e = probe_editor();
  const UserThemeFile theme(
      "hex_forms",
      "{\n"
      "  \"extends\": \"jot-dark\",\n"
      "  \"Normal\": {\"fg\": \"#abc\", \"bg\": \"#010203\"},\n"
      "  \"Keyword\": {\"fg\": \"#A0B0C0FF\"},\n"
      "  \"Comment\": {\"fg\": \"#12345\"},\n"
      "  \"String\": {\"fg\": \"chartreuse\"},\n"
      "  \"Number\": {\"fg\": \"#gggggg\"},\n"
      "  \"Cursor\": {\"fg\": \"  #0f0  \"}\n"
      "}\n");

  REQUIRE(e.apply_theme_for_test(theme.name()));
  const Theme &t = e.theme_for_test();
  // Short form expands each digit (#abc -> #aabbcc); the 8-digit form drops the
  // alpha channel; surrounding whitespace is tolerated.
  REQUIRE(rgb_of(t.fg_default) == 0xAABBCC);
  REQUIRE(rgb_of(t.bg_default) == 0x010203);
  REQUIRE(rgb_of(t.fg_keyword) == 0xA0B0C0);
  REQUIRE(rgb_of(t.fg_cursor) == 0x00FF00);
  // Unparseable colours are ignored: the slot keeps what the base theme set.
  REQUIRE(rgb_of(t.fg_comment) == 0x8B8178);
  REQUIRE(rgb_of(t.fg_string) == 0x63CFA8);
  REQUIRE(rgb_of(t.fg_number) == 0xEF8D8D);
}

TEST_CASE("A hex and an index can name the same slot value", "[jot][theme]")
{
  // The Lua setters (set_hl / jot.theme.set_color) speak the same colour values
  // as a theme file: an index as a number, an exact colour as a hex string. The
  // hex form is what the bundled files use, the numeric form is what every
  // pre-existing theme uses, and both must land in the slot unchanged.
  Editor &e = probe_editor();
  const UserThemeFile theme("mixed_forms",
                            "{\n"
                            "  \"extends\": \"jot-dark\",\n"
                            "  \"Normal\": {\"fg\": \"#123456\", \"bg\": 17},\n"
                            "  \"Keyword\": {\"fg\": 215, \"bg\": \"#0a0b0c\"}\n"
                            "}\n");

  REQUIRE(e.apply_theme_for_test(theme.name()));
  const Theme &t = e.theme_for_test();
  REQUIRE(rgb_of(t.fg_default) == 0x123456);
  REQUIRE(t.bg_default == 17); // a palette index stays a palette index
  REQUIRE(t.fg_keyword == 215);
  REQUIRE(rgb_of(t.bg_keyword) == 0x0A0B0C);
}

TEST_CASE("set_hl takes a palette index or a hex colour", "[jot][theme]")
{
  // The Lua face of the same colour values: `jot.set_hl("Keyword", {fg = 215})`
  // is the long-standing form, and `{fg = "#e8ddcc"}` is the hex one the
  // bundled themes now use. Both have to reach a theme slot; anything else has
  // to leave the slot alone rather than paint a wrong colour.
  lua_State *L = luaL_newstate();
  lua_newtable(L); // the options table, kept at stack index 1
  lua_pushinteger(L, 215);
  lua_setfield(L, 1, "fg");
  lua_pushstring(L, "#e8ddcc");
  lua_setfield(L, 1, "bg");
  lua_pushstring(L, "chartreuse");
  lua_setfield(L, 1, "virt_fg");

  REQUIRE(lua_bind::theme_color_field(L, 1, "fg") == 215);
  const int hex = lua_bind::theme_color_field(L, 1, "bg");
  REQUIRE(jot_ui::is_exact_color(hex));
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  REQUIRE(jot_ui::exact_color_rgb(hex, r, g, b));
  REQUIRE(r == 0xE8);
  REQUIRE(g == 0xDD);
  REQUIRE(b == 0xCC);
  REQUIRE(lua_bind::theme_color_field(L, 1, "missing") == -1);
  REQUIRE(lua_bind::theme_color_field(L, 1, "virt_fg") == -1);

  lua_settop(L, 0);
  lua_close(L);
}

TEST_CASE("The names the removed catalog used still resolve", "[jot][theme]")
{
  Editor &e = probe_editor();

  // Both aliases land on the jot theme that replaced them, and say so: a config
  // written as `color_scheme = "dark"` keeps working, and the chooser shows what
  // is actually painted.
  REQUIRE(e.apply_theme_for_test("dark"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0x1E1B18);

  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0xF9F4EA);

  // Case does not matter, the same way it never did for a real theme name.
  REQUIRE(e.apply_theme_for_test("DARK"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  // A theme that does not exist fails and leaves the current one alone, rather
  // than half-applying a palette.
  REQUIRE_FALSE(e.apply_theme_for_test("gruvbox"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0x1E1B18);
}

TEST_CASE("A file the user writes under a legacy name beats the alias", "[jot][theme]")
{
  // The aliases exist for configs written before the catalog shrank, not to
  // take a name away from the user: their own `light.json` is the theme they
  // asked for, and it is applied as written (here on top of jot-light, which it
  // extends). It mixes an index with the base's hex colours, so both forms have
  // to survive one resolution pass.
  Editor &e = probe_editor();
  const UserThemeFile mine("light",
                           "{\n"
                           "  \"extends\": \"jot-light\",\n"
                           "  \"Normal\": {\"fg\": 16, \"bg\": 17}\n"
                           "}\n");

  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "light");
  REQUIRE(e.theme_for_test().bg_default == 17);
  // The base it extends is still jot-light, so everything the user did not
  // override keeps the shipped look.
  REQUIRE(rgb_of(e.theme_for_test().fg_keyword) == 0xA95C14);

  const std::string name = mine.name();
  fs::remove(fs::path(getenv("JOT_CONFIG_HOME")) / "configs" / "colors" / (name + ".json"));
  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0xF9F4EA);
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
    if (entry.second.second.present && entry.second.second.value >= 0 && !entry.second.first.present)
      bg_without_fg++;
  }
  // CursorLine is the one deliberate exception (it only tints the row).
  REQUIRE(bg_without_fg <= 1);
}

TEST_CASE("Every bundled theme colour is an exact 24-bit value", "[jot][theme]")
{
  // The two shipped schemes are true 24-bit palettes: no slot is left as an
  // xterm index, which would be the quiet way for the themes to drift back onto
  // the 256-entry grid (an index paints fine, it just is not the colour the
  // scheme was designed with). The only numeric slots allowed are the -1s that
  // mean "this group only sets a foreground".
  for (const char *name : {"jot-dark.json", "jot-light.json"})
  {
    const auto groups = parse_theme(bundled_themes_dir() / name);
    int indices = 0;
    for (const auto &entry : groups)
    {
      for (const SlotColor *slot : {&entry.second.first, &entry.second.second})
      {
        if (slot->present && !slot->hex && slot->value != -1)
        {
          ++indices;
          INFO(name << ": " << entry.first << " carries the palette index " << slot->value);
        }
      }
    }
    REQUIRE(indices == 0);
  }
}
