#include "ui/gui/font_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <system_error>

namespace jot_gui
{

  namespace
  {

    std::string lower_ascii(std::string s)
    {
      for (char &c : s)
      {
        c = (char)std::tolower((unsigned char)c);
      }
      return s;
    }

    // Inserts a space at each lower->upper transition and around digits, so
    // "JetBrainsMonoNerdFontMono" and "JetBrainsMono Nerd Font Mono" normalize the
    // same way.
    std::string split_words(const std::string &s)
    {
      std::string out;
      out.reserve(s.size() + 8);
      for (size_t i = 0; i < s.size(); i++)
      {
        const char c = s[i];
        const bool upper = std::isupper((unsigned char)c) != 0;
        const bool digit = std::isdigit((unsigned char)c) != 0;
        if (i > 0)
        {
          const char prev = s[i - 1];
          const bool prev_upper = std::isupper((unsigned char)prev) != 0;
          const bool prev_digit = std::isdigit((unsigned char)prev) != 0;
          const bool prev_sep = std::isspace((unsigned char)prev) != 0;
          const bool next_lower = i + 1 < s.size() && std::islower((unsigned char)s[i + 1]) != 0;
          // Start a new word at an upper/digit after a lower, after a separator
          // run, or at the last capital of an acronym ("NFM" + "ono" -> NFM Mono).
          if ((upper || digit) && !prev_sep && (!prev_upper || next_lower) && !prev_digit)
          {
            out.push_back(' ');
          }
          else if ((upper || digit) && prev_digit)
          {
            out.push_back(' ');
          }
        }
        out.push_back(c);
      }
      return out;
    }

    // Splits into lowercase words: separators split, and so does a case change, so
    // a filename-shaped name and the font's own spaced-out family name both come
    // apart the same way.
    std::vector<std::string> words_of(const std::string &s)
    {
      const std::string spaced = split_words(s);
      std::vector<std::string> words;
      std::string current;
      for (char c : spaced)
      {
        if (std::isalnum((unsigned char)c))
        {
          current.push_back((char)std::tolower((unsigned char)c));
        }
        else if (!current.empty())
        {
          words.push_back(current);
          current.clear();
        }
      }
      if (!current.empty())
      {
        words.push_back(current);
      }
      return words;
    }

    std::string join_words(const std::vector<std::string> &words, size_t count)
    {
      std::string out;
      for (size_t i = 0; i < count && i < words.size(); i++)
      {
        out += words[i];
      }
      return out;
    }

    bool is_style_word(const std::string &word)
    {
      return word == "regular" || word == "bold" || word == "italic" || word == "oblique";
    }

    std::string stem(const std::string &path)
    {
      std::filesystem::path p(path);
      return p.stem().string();
    }

    // The family a font file belongs to, judged by its name: the style words are
    // dropped from the end ("JetBrainsMono-Bold" -> "jetbrainsmono").
    std::string file_family_key(const std::string &path)
    {
      if (path.empty())
      {
        return std::string();
      }
      std::vector<std::string> words = words_of(stem(path));
      while (!words.empty() && is_style_word(words.back()))
      {
        words.pop_back();
      }
      return join_words(words, words.size());
    }

    // Does the filename sound like this style? Used alongside the style flags.
    bool filename_says(const std::string &filename_lower, const char *word)
    {
      return filename_lower.find(word) != std::string::npos;
    }

    void collect_files(const std::filesystem::path &dir, std::vector<std::string> &out, int depth)
    {
      // Font trees are shallow; the cap keeps a symlinked or pathological
      // directory from turning startup into a full filesystem walk.
      if (depth > 6)
      {
        return;
      }
      std::error_code ec;
      std::filesystem::directory_iterator it(dir, ec);
      if (ec)
      {
        return;
      }
      std::vector<std::filesystem::path> subdirs;
      for (const std::filesystem::directory_entry &entry : it)
      {
        const std::filesystem::path &p = entry.path();
        std::error_code type_ec;
        if (entry.is_directory(type_ec))
        {
          subdirs.push_back(p);
        }
        else if (entry.is_regular_file(type_ec) && is_font_file(p.string()))
        {
          out.push_back(p.string());
        }
      }
      // Sorted so a family's faces are visited in a stable order across runs and
      // filesystems.
      std::sort(subdirs.begin(), subdirs.end());
      for (const std::filesystem::path &p : subdirs)
      {
        collect_files(p, out, depth + 1);
      }
    }

    // Fills `family` from one face, keeping whichever style slots are still empty
    // so the first face seen for a style wins.
    void absorb_face(FontFamily &family, const std::string &path, FT_Face face)
    {
      const std::string style_name = face->style_name ? face->style_name : "";
      const bool flag_bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
      const bool flag_italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
      switch (classify_slant(style_name, stem(path), flag_bold, flag_italic))
      {
      case FontSlant::Bold:
        if (family.bold.empty())
        {
          family.bold = path;
        }
        break;
      case FontSlant::Italic:
        if (family.italic.empty())
        {
          family.italic = path;
        }
        break;
      case FontSlant::BoldItalic:
        if (family.bold_italic.empty())
        {
          family.bold_italic = path;
        }
        break;
      case FontSlant::Regular:
        if (family.regular.empty())
        {
          family.regular = path;
        }
        break;
      }
    }

    // A cheap lookup used before falling back to a full scan: open only the files
    // whose own name keys to `key`, then keep just the ones whose reported family
    // agrees. Font filenames almost always name their family ("JetBrainsMono-Bold"
    // in "JetBrains Mono"), so this normally finds the four faces by opening four
    // files instead of every font on the machine -- which matters at startup, where
    // a family is resolved before the first frame.
    bool resolve_by_filenames(FT_Library lib,
                              const std::vector<std::string> &dirs,
                              const std::string &key,
                              FontFamily &out)
    {
      if (key.empty())
      {
        return false;
      }
      for (const std::string &dir : dirs)
      {
        if (dir.empty())
        {
          continue;
        }
        std::vector<std::string> files;
        collect_files(dir, files, 0);
        std::sort(files.begin(), files.end());
        FontFamily family;
        for (const std::string &path : files)
        {
          if (file_family_key(path) != key)
          {
            continue;
          }
          FT_Face face = nullptr;
          if (FT_New_Face(lib, path.c_str(), 0, &face) != 0)
          {
            continue;
          }
          const std::string reported = face->family_name ? face->family_name : "";
          // The name on the file is only a guess; the face has to agree, or a
          // renamed file would hand back the wrong family.
          if (normalize_family(reported) == key)
          {
            if (family.name.empty())
            {
              family.name = reported;
              family.fixed_width = (face->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0;
            }
            absorb_face(family, path, face);
          }
          FT_Done_Face(face);
        }
        // Directories are ordered by priority, so the first one holding the family
        // is the one to take it from.
        if (family.valid())
        {
          out = family;
          return true;
        }
      }
      return false;
    }

  } // namespace

  std::string normalize_family(const std::string &name)
  {
    // Comparison drops the separators entirely rather than reducing them to one
    // space: "JetBrainsMono", "JetBrains Mono" and "jetbrains-mono" then all key
    // to "jetbrainsmono". A space-preserving key would not, because the case
    // split that turns the first into "jet brains mono" cannot be applied to a
    // name the user typed in lower case.
    const std::vector<std::string> words = words_of(name);
    return join_words(words, words.size());
  }

  bool is_font_file(const std::string &path)
  {
    const std::string ext = lower_ascii(std::filesystem::path(path).extension().string());
    return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
  }

  FontSlant classify_slant(const std::string &style_name,
                           const std::string &filename,
                           bool flag_bold,
                           bool flag_italic)
  {
    const std::string haystack = lower_ascii(split_words(style_name) + " " + filename);
    // "oblique" is a slanted face under another name, and is the only spelling
    // some families ship. Only the two weights are recognised: a family that
    // ships Black or Light has them treated as regular, which keeps the regular
    // slot predictable rather than picking whichever weight sorted first.
    const bool bold = flag_bold || filename_says(haystack, "bold");
    const bool italic =
        flag_italic || filename_says(haystack, "italic") || filename_says(haystack, "oblique");
    if (bold && italic)
    {
      return FontSlant::BoldItalic;
    }
    if (bold)
    {
      return FontSlant::Bold;
    }
    if (italic)
    {
      return FontSlant::Italic;
    }
    return FontSlant::Regular;
  }

  std::vector<std::string> default_font_dirs()
  {
    std::vector<std::string> dirs;
    const char *home = std::getenv("HOME");
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg && xdg[0])
    {
      dirs.push_back(std::string(xdg) + "/fonts");
    }
    if (home && home[0])
    {
      dirs.push_back(std::string(home) + "/.local/share/fonts");
      dirs.push_back(std::string(home) + "/.fonts");
      // macOS: a font dropped in by the user lives here.
      dirs.push_back(std::string(home) + "/Library/Fonts");
    }
    dirs.push_back("/usr/local/share/fonts");
    dirs.push_back("/usr/share/fonts");
    // Where most Linux distributions put the TTF subdirectory, listed so it is
    // searched even when a distribution splits it out of the tree above.
    dirs.push_back("/usr/share/fonts/truetype");
    // macOS system fonts.
    dirs.push_back("/Library/Fonts");
    dirs.push_back("/System/Library/Fonts");
    // Windows: the user's own fonts first, then the machine's.
    const char *local_appdata = std::getenv("LOCALAPPDATA");
    if (local_appdata && local_appdata[0])
    {
      dirs.push_back(std::string(local_appdata) + "\\Microsoft\\Windows\\Fonts");
    }
    const char *windir = std::getenv("WINDIR");
    if (windir && windir[0])
    {
      dirs.push_back(std::string(windir) + "\\Fonts");
    }
    return dirs;
  }

  std::vector<FontFamily>
  scan_font_families(FT_Library lib, const std::vector<std::string> &dirs, bool fixed_width_only)
  {
    std::vector<FontFamily> families;
    if (!lib)
    {
      return families;
    }

    std::vector<std::string> files;
    for (const std::string &dir : dirs)
    {
      if (!dir.empty())
      {
        collect_files(dir, files, 0);
      }
    }

    // Grouped by normalized family name, preserving the directory priority: the
    // first file found for a family is the one kept.
    std::map<std::string, FontFamily> by_name;
    std::vector<std::string> order;
    for (const std::string &path : files)
    {
      FT_Face face = nullptr;
      if (FT_New_Face(lib, path.c_str(), 0, &face) != 0)
      {
        continue;
      }
      // A collection (.ttc) holds several faces; take the first of each so a
      // family packed that way is still selectable.
      const long face_count = face->num_faces > 0 ? face->num_faces : 1;
      for (long index = 0; index < face_count; index++)
      {
        FT_Face current = face;
        if (index > 0 && FT_New_Face(lib, path.c_str(), index, &current) != 0)
        {
          continue;
        }
        const std::string family_name = current->family_name ? current->family_name : "";
        if (!family_name.empty())
        {
          const std::string key = normalize_family(family_name);
          auto it = by_name.find(key);
          if (it == by_name.end())
          {
            FontFamily family;
            family.name = family_name;
            family.fixed_width = (current->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0;
            it = by_name.emplace(key, std::move(family)).first;
            order.push_back(key);
          }
          absorb_face(it->second, path, current);
          // A style seen in a lower-priority file still counts for a family that
          // was discovered from a higher-priority one.
          it->second.fixed_width =
              it->second.fixed_width || ((current->face_flags & FT_FACE_FLAG_FIXED_WIDTH) != 0);
        }
        if (index > 0)
        {
          FT_Done_Face(current);
        }
      }
      FT_Done_Face(face);
    }

    for (const std::string &key : order)
    {
      FontFamily &family = by_name[key];
      if (!family.valid())
      {
        continue;
      }
      if (fixed_width_only && !family.fixed_width)
      {
        continue;
      }
      families.push_back(std::move(family));
    }
    std::sort(families.begin(),
              families.end(),
              [](const FontFamily &a, const FontFamily &b) { return a.name < b.name; });
    return families;
  }

  std::vector<FontFamily> installed_families(FT_Library lib)
  {
    // The picker asks for this every time it opens, and a full scan opens every
    // installed face. The result cannot change while the process runs in any way
    // worth noticing, so it is scanned once.
    static std::map<FT_Library, std::vector<FontFamily>> cache;
    auto it = cache.find(lib);
    if (it != cache.end())
    {
      return it->second;
    }
    std::vector<FontFamily> families = scan_font_families(lib, default_font_dirs(), true);
    cache.emplace(lib, families);
    return families;
  }

  bool resolve_font_family(FT_Library lib, const std::string &name, FontFamily &out)
  {
    if (!lib || name.empty())
    {
      return false;
    }

    // A path to a face: read the family out of it, then look that family up so
    // its other styles are picked up as well. When it is a face no catalog entry
    // covers, the single file still makes a usable family.
    std::error_code ec;
    const bool is_path =
        std::filesystem::exists(name, ec) && !std::filesystem::is_directory(name, ec);
    std::string wanted = name;
    if (is_path)
    {
      FT_Face face = nullptr;
      if (FT_New_Face(lib, name.c_str(), 0, &face) == 0)
      {
        wanted = face->family_name ? face->family_name : "";
        FT_Done_Face(face);
      }
      if (wanted.empty())
      {
        return false;
      }
    }

    // Two keys, because a family can be named in two shapes. "JetBrains Mono"
    // keys as a family name; "JetBrainsMono-Bold" keys as a filename, whose
    // trailing style word the family name never carries.
    const std::vector<std::string> dirs = default_font_dirs();
    const std::string key = normalize_family(wanted);
    const std::string file_key = file_family_key(wanted);
    FontFamily cheap;
    if (resolve_by_filenames(lib, dirs, key, cheap)
        || (file_key != key && resolve_by_filenames(lib, dirs, file_key, cheap)))
    {
      out = cheap;
      return true;
    }

    // Nothing matched by filename: fall back to reading every installed face,
    // which is the only way to find a family whose filenames do not name it.
    const std::vector<FontFamily> all = scan_font_families(lib, dirs, false);
    for (const FontFamily &family : all)
    {
      if (normalize_family(family.name) == key)
      {
        out = family;
        return true;
      }
    }
    for (const FontFamily &family : all)
    {
      if ((!family.regular.empty() && file_family_key(family.regular) == file_key)
          || (!family.bold.empty() && file_family_key(family.bold) == file_key)
          || (!family.italic.empty() && file_family_key(family.italic) == file_key)
          || (!family.bold_italic.empty() && file_family_key(family.bold_italic) == file_key))
      {
        out = family;
        return true;
      }
    }
    if (is_path)
    {
      FontFamily family;
      family.name = wanted;
      family.regular = name;
      family.fixed_width = true;
      out = family;
      return true;
    }
    return false;
  }

} // namespace jot_gui
