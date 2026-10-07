// The bundled themes: jot-dark (Binaryify's One Dark Pro), jot-light ("mochi
// milk"), the schemes jot maintains, plus the flexoki pair ported from kepano's
// Flexoki palette.
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
// Both shipped themes name exact 24-bit colours ("#c678dd") rather than xterm
// palette indices, so a case that wants a slot's colour reads it back through
// the same conversion the renderer uses instead of comparing an index.
#include "editor.h"
#include "lua/api.h"
#include "lua/api_internal.h"
#include "ui/xterm_palette.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
    return fs::path(JOT_LUA_SOURCE_DIR).parent_path().parent_path() / ".configs" / "configs"
           / "colors";
  }

  // Every scheme jot ships, in the order the chooser lists them. The cases below
  // are written against this list instead of a hardcoded pair, so a new theme
  // reaches all of them (the fixture, the chooser, the slot-parity and the
  // all-hex checks) by being added here.
  const std::vector<std::string> &bundled_theme_files()
  {
    static const std::vector<std::string> files = {
        "flexoki-dark.json", "flexoki-light.json", "jot-dark.json", "jot-light.json"};
    return files;
  }

  // "jot-dark.json" -> "jot-dark": what the chooser lists and `:colorscheme`
  // takes.
  std::string theme_name_of(const std::string &file)
  {
    return file.substr(0, file.size() - std::string(".json").size());
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
      const size_t body_end =
          body_start == std::string::npos ? std::string::npos : text.find('}', body_start);
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
      for (const std::string &name : bundled_theme_files())
      {
        fs::copy_file(
            bundled_themes_dir() / name, colors / name, fs::copy_options::overwrite_existing);
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

TEST_CASE("jot-dark carries One Dark Pro's palette", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-dark"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  const Theme &t = e.theme_for_test();
  // Exact colours, not palette entries: the whole point of the hex form is that
  // the scheme is authored in 24-bit space.
  REQUIRE(jot_ui::is_exact_color(t.fg_default));
  REQUIRE(rgb_of(t.fg_default) == 0xABB2BF); // One Dark Pro's grey ink
  REQUIRE(rgb_of(t.bg_default) == 0x282C34); // its editor ground
  REQUIRE(jot_ui::is_exact_color(t.bg_default));
  REQUIRE(rgb_of(t.fg_keyword) == 0xC678DD);         // purple
  REQUIRE(rgb_of(t.fg_string) == 0x98C379);          // green
  REQUIRE(rgb_of(t.fg_function_method) == 0x61AFEF); // blue
  REQUIRE(rgb_of(t.fg_number) == 0xD19A66);          // orange
  // More of the palette is spoken for than the five inks the scheme used to
  // carry: properties and tags take the variable red, modules and types the
  // yellow.
  REQUIRE(rgb_of(t.fg_field) == 0xE06C75);
  REQUIRE(rgb_of(t.fg_module) == 0xE5C07B);
  REQUIRE(rgb_of(t.fg_tag) == 0xE06C75);
  // The accent is One Dark Pro's blue, spent where a hand goes: the active
  // border, the cursor line number and the focused terminal tab.
  REQUIRE(rgb_of(t.fg_active_border) == 0x61AFEF);
  // The chrome sits on the theme's own panel surface, one step behind the
  // editor's ground: the sidebar and the status line share it.
  REQUIRE(rgb_of(t.bg_sidebar) == 0x21252B);
  REQUIRE(rgb_of(t.bg_status) == 0x21252B);
}

TEST_CASE("Jot themes keep separators visible and selected rows distinct", "[jot][theme]")
{
  const auto luminance = [](long long rgb)
  {
    const auto channel = [](int value)
    {
      const double v = value / 255.0;
      return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((rgb >> 16) & 255) + 0.7152 * channel((rgb >> 8) & 255)
           + 0.0722 * channel(rgb & 255);
  };
  const auto contrast = [&](long long a, long long b)
  {
    const double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
  };
  for (const std::string name : {"jot-dark.json", "jot-light.json"})
  {
    const auto slots = parse_theme(bundled_themes_dir() / name);
    INFO(name);
    REQUIRE(contrast(slots.at("WinSeparator").first.value, slots.at("Normal").second.value) >= 3.0);
    REQUIRE(contrast(slots.at("SidebarSel").second.value, slots.at("Sidebar").second.value) >= 1.5);
    REQUIRE(contrast(slots.at("SidebarSel").first.value, slots.at("SidebarSel").second.value)
            >= 4.5);
  }
}

TEST_CASE("The breadcrumb groups reach the Winbar slots", "[jot][theme]")
{
  // Every group name in a theme file is a string the reader has to translate,
  // and a name no alias maps is not an error: the slot quietly keeps its struct
  // default. That is what the four Winbar groups did -- the bundled themes
  // named them, the slot chain matched only their lowercase spellings, and the
  // breadcrumb row painted in the built-in black band, which on a near-black
  // scheme is no band at all.
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-dark"));
  const Theme &t = e.theme_for_test();
  REQUIRE(jot_ui::is_exact_color(t.bg_winbar));
  REQUIRE(rgb_of(t.bg_winbar) == 0x21252B);
  REQUIRE(rgb_of(t.fg_winbar) == 0xABB2BF);
  REQUIRE(rgb_of(t.fg_winbar_crumb) == 0x7F848E);
  REQUIRE(rgb_of(t.bg_winbar_crumb) == 0x21252B);
  REQUIRE(rgb_of(t.fg_winbar_separator) == 0x515A6B);
  REQUIRE(rgb_of(t.fg_winbar_hover) == 0xD7DAE0);
  REQUIRE(rgb_of(t.bg_winbar_hover) == 0x323842);
}

TEST_CASE("jot-light is the same inks as jam and matcha on cream paper", "[jot][theme]")
{
  Editor &e = probe_editor();
  REQUIRE(e.apply_theme_for_test("jot-light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");

  const Theme &t = e.theme_for_test();
  REQUIRE(jot_ui::is_exact_color(t.fg_default));
  REQUIRE(rgb_of(t.fg_default) == 0x3A2C3F);
  REQUIRE(rgb_of(t.bg_default) == 0xFFFAF4); // warm milk, not white
  REQUIRE(rgb_of(t.fg_keyword) == 0xCA246C);
  REQUIRE(rgb_of(t.fg_string) == 0x0D7C64);
  REQUIRE(rgb_of(t.fg_function_method) == 0x186F9F);
  REQUIRE(rgb_of(t.fg_type) == 0x7B4BD1); // taro purple
  REQUIRE(rgb_of(t.fg_active_border) == 0xCA246C);
}

TEST_CASE("flexoki carries kepano's palette, not an approximation", "[jot][theme]")
{
  // The Flexoki pair is data ported from another project. The slot-parity and
  // all-hex cases above check the *shape* of a theme; this one pins the actual
  // values, so a wrong step (base-900 where the surface is base-850) or a hue
  // that quietly drifts back to the jot palette fails here.
  Editor &e = probe_editor();

  REQUIRE(e.apply_theme_for_test("flexoki-dark"));
  REQUIRE(e.theme_name_for_test() == "flexoki-dark");
  {
    const Theme &t = e.theme_for_test();
    REQUIRE(jot_ui::is_exact_color(t.bg_default));
    REQUIRE(rgb_of(t.fg_default) == 0xCECDC3);  // base-200 ink
    REQUIRE(rgb_of(t.bg_default) == 0x100F0F);  // black
    REQUIRE(rgb_of(t.fg_comment) == 0x878580);  // base-500
    REQUIRE(rgb_of(t.fg_keyword) == 0x879A39);  // green-400
    REQUIRE(rgb_of(t.fg_string) == 0x3AA99F);   // cyan-400
    REQUIRE(rgb_of(t.fg_number) == 0x8B7EC8);   // purple-400
    REQUIRE(rgb_of(t.fg_function) == 0xDA702C); // orange-400
    REQUIRE(rgb_of(t.fg_type) == 0xD0A215);     // yellow-400
    // The surfaces the chrome reads: base-950 float over black, base-850 for
    // the raised band under the pointer.
    REQUIRE(rgb_of(t.bg_tab_hover) == 0x343331);
  }

  REQUIRE(e.apply_theme_for_test("flexoki-light"));
  REQUIRE(e.theme_name_for_test() == "flexoki-light");
  {
    const Theme &t = e.theme_for_test();
    REQUIRE(rgb_of(t.fg_default) == 0x100F0F);  // black ink
    REQUIRE(rgb_of(t.bg_default) == 0xFFFCF0);  // paper
    // Six of kepano's steps sit under 4.5:1 on paper; the values below are his
    // hues darkened until they clear it (audit 001, finding 3).
    REQUIRE(rgb_of(t.fg_comment) == 0x6C6B67);  // base-600
    REQUIRE(rgb_of(t.fg_keyword) == 0x5B720A);  // green-600
    REQUIRE(rgb_of(t.fg_string) == 0x237F77);   // cyan-600
    REQUIRE(rgb_of(t.fg_number) == 0x5E409D);   // purple-600
    REQUIRE(rgb_of(t.fg_function) == 0xB34E14); // orange-600
    REQUIRE(rgb_of(t.fg_type) == 0x8A6801);     // yellow-600
    REQUIRE(rgb_of(t.bg_tab_hover) == 0xDAD8CE);
  }
}

TEST_CASE("Hex theme colours accept every documented form", "[jot][theme]")
{
  // The points of the hex form: an exact colour, so nothing is snapped to the
  // 256-entry grid, and any of the three spellings. A value the reader cannot
  // parse must leave the slot alone -- painting it black (or leaving the
  // previous theme's colour) is the failure this guards.
  Editor &e = probe_editor();
  const UserThemeFile theme("hex_forms",
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
  REQUIRE(rgb_of(t.fg_comment) == 0x7F848E);
  REQUIRE(rgb_of(t.fg_string) == 0x98C379);
  REQUIRE(rgb_of(t.fg_number) == 0xD19A66);
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

TEST_CASE("set_hl and decorations take an index or a hex colour", "[jot][theme]")
{
  // The Lua face of the same colour values: `jot.set_hl("Keyword", {fg = 215})`
  // is the long-standing form, and `{fg = "#e8ddcc"}` is the hex one the
  // bundled themes now use. `jot.decoration.set` reads its colours through the
  // same helper, so both APIs accept both forms; anything else has to leave the
  // slot alone rather than paint a wrong colour.
  lua_State *L = luaL_newstate();
  lua_newtable(L); // the options table, kept at stack index 1
  lua_pushinteger(L, 215);
  lua_setfield(L, 1, "fg");
  lua_pushstring(L, "#e8ddcc");
  lua_setfield(L, 1, "bg");
  lua_pushstring(L, "chartreuse");
  lua_setfield(L, 1, "virt_fg");

  REQUIRE(jot_lua::table_color(L, 1, "fg") == 215);
  const int hex = jot_lua::table_color(L, 1, "bg");
  REQUIRE(jot_ui::is_exact_color(hex));
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  REQUIRE(jot_ui::exact_color_rgb(hex, r, g, b));
  REQUIRE(r == 0xE8);
  REQUIRE(g == 0xDD);
  REQUIRE(b == 0xCC);
  REQUIRE(jot_lua::table_color(L, 1, "missing") == -1);
  REQUIRE(jot_lua::table_color(L, 1, "virt_fg") == -1);

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
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0x282C34);

  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0xFFFAF4);

  // Case does not matter, the same way it never did for a real theme name.
  REQUIRE(e.apply_theme_for_test("DARK"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");

  // A theme that does not exist fails and leaves the current one alone, rather
  // than half-applying a palette.
  REQUIRE_FALSE(e.apply_theme_for_test("gruvbox"));
  REQUIRE(e.theme_name_for_test() == "jot-dark");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0x282C34);
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
  REQUIRE(rgb_of(e.theme_for_test().fg_keyword) == 0xCA246C);

  const std::string name = mine.name();
  fs::remove(fs::path(getenv("JOT_CONFIG_HOME")) / "configs" / "colors" / (name + ".json"));
  REQUIRE(e.apply_theme_for_test("light"));
  REQUIRE(e.theme_name_for_test() == "jot-light");
  REQUIRE(rgb_of(e.theme_for_test().bg_default) == 0xFFFAF4);
}

TEST_CASE("The chooser lists the schemes jot bundles and nothing else", "[jot][theme]")
{
  // jot ships no third-party catalog any more: the chooser (what `:theme`
  // opens) shows the schemes the editor actually maintains -- the two jot
  // palettes and the Flexoki pair. The test's config dir holds a copy of each
  // and the bundled tree holds the originals, and they resolve to the same
  // names.
  Editor &e = probe_editor();
  const auto themes = e.available_themes_for_test();
  REQUIRE(themes.size() == bundled_theme_files().size());
  for (const std::string &file : bundled_theme_files())
  {
    const std::string name = theme_name_of(file);
    INFO("bundled theme not listed: " << name);
    REQUIRE(std::find(themes.begin(), themes.end(), name) != themes.end());
  }

  // None of the names the removed catalog was keyed on is listed, including the
  // two that still resolve as aliases: a stale name must not look selectable.
  for (const char *gone : {"dark",
                           "light",
                           "gruvbox",
                           "tokyonight",
                           "catppuccin",
                           "onedark",
                           "monokai",
                           "solarized",
                           "dracula",
                           "nord"})
  {
    INFO("removed theme still listed: " << gone);
    REQUIRE(std::find(themes.begin(), themes.end(), gone) == themes.end());
  }
}

TEST_CASE("Every bundled theme defines the same slots", "[jot][theme]")
{
  // A slot in one file but not another does not fail loudly -- it falls back to
  // the struct default, which is a 16-colour value on the wrong palette.
  // jot-dark is the reference; every other scheme has to carry the same set.
  const auto reference = parse_theme(bundled_themes_dir() / "jot-dark.json");
  REQUIRE(reference.size() > 70);

  for (const std::string &file : bundled_theme_files())
  {
    const auto slots = parse_theme(bundled_themes_dir() / file);
    std::set<std::string> only_reference;
    std::set<std::string> only_theme;
    for (const auto &entry : reference)
    {
      if (slots.find(entry.first) == slots.end())
        only_reference.insert(entry.first);
    }
    for (const auto &entry : slots)
    {
      if (reference.find(entry.first) == reference.end())
        only_theme.insert(entry.first);
    }
    INFO(file << " -- jot-dark only: " << only_reference.size()
              << ", this theme only: " << only_theme.size());
    REQUIRE(only_reference.empty());
    REQUIRE(only_theme.empty());

    // Every slot that names a background must name a foreground too: a group
    // with a bg but no fg paints text in the inherited colour, which on a light
    // theme is invisible. CursorLine is the one deliberate exception (it only
    // tints the row).
    int bg_without_fg = 0;
    for (const auto &entry : slots)
    {
      if (entry.second.second.present && entry.second.second.value >= 0
          && !entry.second.first.present)
        bg_without_fg++;
    }
    REQUIRE(bg_without_fg <= 1);
  }
}

TEST_CASE("Every bundled theme colour is an exact 24-bit value", "[jot][theme]")
{
  // The shipped schemes are true 24-bit palettes: no slot is left as an xterm
  // index, which would be the quiet way for the themes to drift back onto the
  // 256-entry grid (an index paints fine, it just is not the colour the scheme
  // was designed with). The only numeric slots allowed are the -1s that mean
  // "this group only sets one half": WordHighlight names a band and no ink, the
  // git slots name an ink and no band.
  for (const std::string &name : bundled_theme_files())
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
