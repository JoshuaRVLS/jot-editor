#pragma once

// Choosing the typeface the GUI renders with, as opposed to only its size.
//
// The GUI used to find a font by walking a short list of hard-coded paths, so
// the only way to change how text looked was to zoom it. This turns that list
// into a real catalog: every installed family is discovered, its style faces
// are grouped, and one can be selected by name.
//
// The logic is deliberately split from UIGui: everything here answers "which
// file backs this family, and in which styles", with no window or GL state, so
// it can be tested directly.
//
// The font file is the source of truth for the family name, not its filename:
// "JetBrainsMonoNerdFontMono-Regular.ttf" reports "JetBrainsMono Nerd Font
// Mono", and it is the latter a user would type.

#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>

namespace jot_gui
{

  // The style faces of one family. A face is empty when the family has no such
  // style; glyph rendering falls back to the regular face, so a family that ships
  // only regular and bold still renders italic text (just not slanted).
  struct FontFamily
  {
    std::string name; // as the font reports it
    std::string regular;
    std::string bold;
    std::string italic;
    std::string bold_italic;
    bool fixed_width = false; // safe for the cell grid

    bool valid() const
    {
      return !regular.empty();
    }
  };

  // A face's style, as decided by its style flags and its filename.
  enum class FontSlant
  {
    Regular,
    Bold,
    Italic,
    BoldItalic,
  };

  // Directories to search, most specific first: fonts the user installed win over
  // the same family shipped by the system, which wins over the fallbacks.
  std::vector<std::string> default_font_dirs();

  // One key for spelling variants, so "JetBrainsMono NFM", "jetbrains-mono-nfm"
  // and "JetBrains_Mono_NFM" all name the same family. Splitting camel-case into
  // words first is what lets a filename-shaped name match the font's own
  // (space-separated) family name.
  std::string normalize_family(const std::string &name);

  // Classifies one face. `style_name` and `filename` are both consulted: the
  // OS/2 style flags are authoritative for well-formed fonts, but many Nerd Font
  // and renamed builds carry "Bold"/"Italic" only in the filename.
  FontSlant classify_slant(const std::string &style_name,
                           const std::string &filename,
                           bool flag_bold,
                           bool flag_italic);

  // Font file extensions a face can live in.
  bool is_font_file(const std::string &path);

  // Every family installed under `dirs`. When `fixed_width_only` is set only
  // families that declare themselves fixed-width are returned, which is what a
  // picker wants: a proportional face would break the cell grid by rendering
  // glyphs of differing widths into fixed cells. Results are sorted by name.
  std::vector<FontFamily> scan_font_families(FT_Library lib,
                                             const std::vector<std::string> &dirs,
                                             bool fixed_width_only = true);

  // Every family installed under the default directories, fixed-width only.
  std::vector<FontFamily> installed_families(FT_Library lib);

  // The family named `name`, looked up over the default directories.
  //
  // `name` may be:
  //   * a family name, in any spelling ("JetBrains Mono", "jetbrains-mono"),
  //   * a font filename without its extension ("JetBrainsMono-Bold"),
  //   * a path to a font file, in which case the family it belongs to is
  //     resolved from it so the other styles come along too.
  //
  // Returns false when nothing matches, leaving `out` untouched; the caller keeps
  // whatever font it already had rather than losing the UI to a typo.
  bool resolve_font_family(FT_Library lib, const std::string &name, FontFamily &out);

} // namespace jot_gui
