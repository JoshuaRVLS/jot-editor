#ifndef FEATURES_COLOR_TERMINAL_CODES_H
#define FEATURES_COLOR_TERMINAL_CODES_H

// Terminal colour codes for the colour preview:
//
//   * xterm 256 / ANSI SGR, as they appear in source: the #xNN shorthand and
//     whole escape sequences (`\e[38;5;208m`, `\x1b[48;2;R;G;Bm`, the 16-colour
//     `[1;31m` forms) -- the kind of thing you find in a shell script, a theme
//     file or a prompt configuration;
//   * LS_COLORS / SGR snippets (`=38;5;196`, `=01;34`, `=48;2;0;0;255`), which
//     dircolors files and tool configs are made of.
//
// Ported from nvim-colorizer.lua's xterm.lua and ls_colors.lua. The 256-colour
// palette itself is shared with the UI's palette (ui/xterm_palette) rather than
// duplicated, so a palette entry can never mean two different colours.

#include <cstdint>
#include <string>

namespace jot_color
{
  // The RGB for palette entry `index` (0-255), clamped to that range.
  std::uint32_t xterm256_rgb(int index);

  // True when an xterm/ANSI colour code starts at `i`; on success `rgb` holds
  // the colour and `end` is the index just past the code.
  bool parse_xterm_code(const std::string &line,
                        size_t i,
                        size_t limit,
                        std::uint32_t &rgb,
                        size_t &end);

  // True when an LS_COLORS/SGR snippet starts at `i` (which must be the '=');
  // on success `rgb` holds the colour and `end` is just past the snippet.
  bool parse_ls_colors(const std::string &line,
                       size_t i,
                       size_t limit,
                       std::uint32_t &rgb,
                       size_t &end);
} // namespace jot_color

#endif // FEATURES_COLOR_TERMINAL_CODES_H
