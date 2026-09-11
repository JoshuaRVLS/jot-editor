// Font catalog: choosing a typeface by name (src/ui/gui/font_catalog.cpp).
//
// The parsing rules are tested directly because they are where this goes
// wrong: a font's family name, its filename and what a user types are three
// different spellings of the same thing, and a style word on the end must not
// make a family unfindable.
#include "ui/gui/font_catalog.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using jot_gui::classify_slant;
using jot_gui::FontSlant;
using jot_gui::normalize_family;

TEST_CASE("Family names key the same however they are spelled", "[jot][font]")
{
  // The font's own name, a filename-shaped spelling, a lower-case/hyphenated
  // spelling and one with stray punctuation all have to collide.
  const std::vector<std::string> spellings = {
      "JetBrains Mono",
      "JetBrainsMono",
      "jetbrains-mono",
      "jetbrains_mono",
      "JETBRAINS  MONO",
      " jetbrains.mono ",
  };
  const std::string key = normalize_family(spellings.front());
  REQUIRE(key == "jetbrainsmono");
  for (const std::string &spelling : spellings)
  {
    REQUIRE(normalize_family(spelling) == key);
  }

  // A shorter name must not be swallowed by a longer one: these are separate
  // families that both ship on a typical Linux desktop.
  REQUIRE(normalize_family("JetBrains Mono") != normalize_family("JetBrains Mono NL"));
  REQUIRE(normalize_family("JetBrainsMono NFM")
          != normalize_family("JetBrainsMono Nerd Font Mono"));
}

TEST_CASE("Style is read from the style name and the filename", "[jot][font]")
{
  SECTION("style flags alone")
  {
    REQUIRE(classify_slant("Regular", "whatever", false, false) == FontSlant::Regular);
    REQUIRE(classify_slant("Bold", "whatever", true, false) == FontSlant::Bold);
    REQUIRE(classify_slant("Italic", "whatever", false, true) == FontSlant::Italic);
    REQUIRE(classify_slant("Bold Italic", "whatever", true, true) == FontSlant::BoldItalic);
  }

  SECTION("filename decides when the flags are missing")
  {
    // Many renamed and Nerd Font builds only say so in the filename.
    REQUIRE(classify_slant("", "JetBrainsMono-Bold", false, false) == FontSlant::Bold);
    REQUIRE(classify_slant("", "JetBrainsMono-Italic", false, false) == FontSlant::Italic);
    REQUIRE(classify_slant("", "JetBrainsMono-BoldItalic", false, false) == FontSlant::BoldItalic);
    // Oblique is a slanted face under another name.
    REQUIRE(classify_slant("", "FiraCode-Oblique", false, false) == FontSlant::Italic);
  }

  SECTION("only regular and bold are recognised as weights")
  {
    // A Light or Black face must not take the regular or bold slot, or a
    // family shipping several weights would get whichever one sorted first.
    REQUIRE(classify_slant("Light", "Family-Light", false, false) == FontSlant::Regular);
    REQUIRE(classify_slant("Black", "Family-Black", false, false) == FontSlant::Regular);
  }
}

TEST_CASE("Only font files are considered", "[jot][font]")
{
  REQUIRE(jot_gui::is_font_file("/x/Foo.ttf"));
  REQUIRE(jot_gui::is_font_file("/x/Foo.OTF"));
  REQUIRE(jot_gui::is_font_file("/x/Foo.ttc"));
  REQUIRE_FALSE(jot_gui::is_font_file("/x/Foo.txt"));
  REQUIRE_FALSE(jot_gui::is_font_file("/x/Foo"));
  // A trailing dot must not be read as an extension.
  REQUIRE_FALSE(jot_gui::is_font_file("/x/Foo."));
}

TEST_CASE("A face resolves to the family that owns it", "[jot][font]")
{
  // Resolution reads the installed fonts, so this only asserts when the
  // machine has one; the parsing rules above are the part under test.
  FT_Library lib = nullptr;
  REQUIRE(FT_Init_FreeType(&lib) == 0);

  const std::vector<jot_gui::FontFamily> families = jot_gui::installed_families(lib);
  if (!families.empty())
  {
    const std::string name = families.front().name;

    // By its own name, by a differently-spelled version of it, and by a path
    // to one of its files: all three are the same family.
    jot_gui::FontFamily by_name;
    REQUIRE(jot_gui::resolve_font_family(lib, name, by_name));
    REQUIRE(by_name.name == name);
    REQUIRE(by_name.valid());

    jot_gui::FontFamily by_spelling;
    REQUIRE(jot_gui::resolve_font_family(lib, jot_gui::normalize_family(name), by_spelling));
    REQUIRE(by_spelling.name == name);

    jot_gui::FontFamily by_path;
    REQUIRE(jot_gui::resolve_font_family(lib, by_name.regular, by_path));
    REQUIRE(by_path.name == name);
    REQUIRE(by_path.regular == by_name.regular);

    // Every family found is usable as a regular face path.
    for (const jot_gui::FontFamily &family : families)
    {
      REQUIRE_FALSE(family.regular.empty());
      REQUIRE(std::filesystem::exists(family.regular));
    }
  }

  // An unknown name leaves the caller's family untouched, which is what lets
  // a bad value in the config fall back instead of leaving the UI fontless.
  jot_gui::FontFamily untouched;
  untouched.name = "sentinel";
  REQUIRE_FALSE(jot_gui::resolve_font_family(lib, "definitely-not-installed-xyz", untouched));
  REQUIRE(untouched.name == "sentinel");

  FT_Done_FreeType(lib);
}
