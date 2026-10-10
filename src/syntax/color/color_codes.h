#ifndef FEATURES_COLOR_CODES_H
#define FEATURES_COLOR_CODES_H

// Colour-literal detection for the inline colour preview.
//
// Ported in spirit from catgoose/nvim-colorizer.lua: find colour literals in a
// line and report the byte range plus the colour they name, so the renderer can
// paint the span in that colour. Upstream dispatches on the first byte, then a
// trie of prefixes, then a fallback list; that structure exists to keep Lua
// fast. In C++ a single left-to-right walk with the same boundary rules is
// clearer and needs no trie, so the parts that are ported faithfully are the
// *rules*: which byte sequences count as colours, and where a match may start.
//
// Covered here: hex (with and without "#", the QML/Android alpha-first forms,
// 0x...), CSS/X11 names, Tailwind class suffixes and LaTeX xcolor expressions.
// The CSS function family lives in color_functions, terminal codes in
// color_terminal_codes, and variable resolution in color_definitions.
//
// Not ported: the LSP documentColor bridge, and following @import into other
// files when resolving variables.
//
// Kept free of UI/editor types so it is unit testable on its own.

#include <cstdint>
#include <string>
#include <vector>

namespace jot_color
{
  class Definitions;

  struct ColorSpan
  {
    int start = 0;         // byte offset of the first character of the literal
    int len = 0;           // length in bytes
    std::uint32_t rgb = 0; // 0xRRGGBB (alpha, where a format carries one, is ignored)
  };

  struct Options
  {
    // Hex forms, matching upstream's hex.* sub-keys.
    bool hex3 = true;          // #RGB
    bool hex4 = true;          // #RGBA
    bool hex6 = true;          // #RRGGBB
    bool hex8 = false;         // #RRGGBBAA
    bool hex_aarrggbb = false; // #AARRGGBB (QML's alpha-first order)
    bool hex_no_hash = false;  // RRGGBB at a token boundary, with no "#"
    bool hex_0x = false;       // 0xRGB / 0xRRGGBB / 0xAARRGGBB
    // Named colours (CSS/X11). Case variants are separate switches upstream.
    bool names = true;
    bool names_camelcase = true;  // LightBlue
    bool names_uppercase = false; // LIGHTBLUE
    // Tailwind class suffixes, e.g. text-orange-500 / bg-slate-50.
    bool tailwind = false;
    // LaTeX xcolor expressions, e.g. red!30.
    bool xcolor = false;
    // rgb()/rgba()/hsl()/hsla()/hwb()/lab()/lch()/oklch()/hsluv()/color().
    bool functions = true;
    // Terminal codes: #xNN, ANSI SGR escapes, LS_COLORS snippets.
    bool xterm = false;
    bool ls_colors = false;
    // var(--name) and $name references, resolved through `definitions`.
    bool css_var = false;
    bool sass = false;
  };

  // How a detected colour is shown (upstream's display.mode).
  enum class DisplayMode
  {
    Background,  // the literal's background becomes the colour
    Foreground,  // the literal's text is drawn in the colour
    VirtualText, // a swatch is appended after the line's text
  };

  // Parses the `colorizer_mode` setting; anything unrecognised (including the
  // empty string) is Background, the default.
  DisplayMode parse_display_mode(const std::string &value);

  // Finds every colour literal in `line`, scanning at most `byte_limit` bytes
  // (the visible window; a minified file is never walked in full). When `scope`
  // is non-null only bytes whose entry is non-zero may start a match -- the
  // renderer fills it with the string/comment bytes for the
  // "only in strings and comments" option. When `definitions` is non-null,
  // var(--x) and $x references resolve against it.
  //
  // The result is sorted by `start` and never overlaps.
  std::vector<ColorSpan> scan_line(const std::string &line,
                                   int byte_limit,
                                   const Options &options,
                                   const std::vector<std::uint8_t> *scope = nullptr,
                                   const Definitions *definitions = nullptr);

  // #RGB / #RGBA expansion and the text colour to use on top of a solid fill,
  // exposed for the renderer (and its tests).
  std::uint32_t expand_short_hex(const char *digits, int count);
  std::uint32_t contrast_text_color(std::uint32_t rgb);

  // Per-line memo for scan_line, validated by content hash rather than by
  // invalidating on edits: an edit changes the line's bytes, which invalidates
  // its entry, so no edit path has to know this exists (the renderer re-reads
  // the visible lines every frame anyway). Bounded so scrolling a huge file
  // cannot grow it without limit.
  class SpanCache
  {
  public:
    // Returns the spans for `line`, rescanning only when the content, the
    // window, the options, the scope or the definition set changed since the
    // last call. `definitions_version` is bumped by the owner whenever the
    // definition index is rebuilt, so resolved variables cannot go stale.
    const std::vector<ColorSpan> &spans_for(int line_index,
                                            const std::string &line,
                                            int byte_limit,
                                            const Options &options,
                                            const std::vector<std::uint8_t> *scope = nullptr,
                                            const Definitions *definitions = nullptr,
                                            std::uint64_t definitions_version = 0);

    void clear()
    {
      entries_.clear();
    }

  private:
    struct Entry
    {
      std::uint64_t hash = 0;
      int limit = -1;
      std::uint32_t options_mask = 0;
      std::uint64_t scope_hash = 0;
      std::uint64_t definitions_version = 0;
      std::vector<ColorSpan> spans;
    };
    std::vector<Entry> entries_;
    std::vector<int> line_to_slot_;
    // Generous for a screenful of lines, small enough that the rescan cost of
    // hitting the cap stays bounded.
    static constexpr size_t kMaxEntries = 4096;
  };
} // namespace jot_color

#endif // FEATURES_COLOR_CODES_H
