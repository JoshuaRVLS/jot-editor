#ifndef JOT_TEXT_ANSI_SPANS_H
#define JOT_TEXT_ANSI_SPANS_H

// Reading a terminal program's output back as text plus colours.
//
// A build command that prints colour writes SGR sequences around the parts it
// wants coloured (`\e[31m`, `\e[38;5;196m`, `\e[38;2;255;64;0m`, `\e[0m`).
// A log buffer has no emulator to interpret them, so the bytes would sit in the
// text as garbage. This is the small part of an emulator that matters here: it
// removes the sequences and reports, per row, which byte ranges were coloured
// and with what, so the caller can paint spans instead of escape bytes.
//
// What it reads: the colour attributes (30-37/90-97, 40-47/100-107, 38/48 with
// the `;5;N` and `;2;R;G;B` forms, including the `:` spelling), bold, faint
// (SGR 2), underline, and the resets. Bold promotes one of the eight base
// colours to its bright variant, the way every terminal does it and the way
// `parse_ls_colors` in syntax/color does it. Everything else a program can send
// -- cursor moves, erases, window titles (OSC), charset selection -- is dropped
// without a trace: a log cannot honour it, and no reader wants to see it.
//
// The state is the terminal's, not the line's, so it lives in the Parser and
// carries across rows: a command that sets a colour once and prints a hundred
// lines keeps it. `reset()` is what a new run starts with.

#include <string>
#include <vector>

namespace ansi_spans
{
  // One colour an SGR sequence named. Either a palette entry the terminal
  // resolves (0-255), or an exact 24-bit value. `set` is false when the
  // program left the slot alone, which leaves the cell its own colour.
  struct Color
  {
    bool set = false;
    int index = -1; // >= 0 for a palette entry
    int r = 0;
    int g = 0;
    int b = 0; // the exact value, when index < 0

    bool operator==(const Color &other) const
    {
      if (set != other.set)
      {
        return false;
      }
      if (!set)
      {
        return true;
      }
      if (index != other.index)
      {
        return false;
      }
      return index >= 0 || (r == other.r && g == other.g && b == other.b);
    }

    bool operator!=(const Color &other) const
    {
      return !(*this == other);
    }
  };

  // A run of bytes in a row drawn with one colour, plus the two attributes that
  // change how the text itself is drawn rather than what colour it is.
  struct Span
  {
    int col = 0;   // byte offset in Row::text
    int width = 0; // bytes the colour covers
    Color fg;
    Color bg;
    bool dim = false;       // SGR 2: the token's colour, faded
    bool underline = false; // SGR 4
  };

  struct Row
  {
    std::string text;        // the row with every escape sequence removed
    std::vector<Span> spans; // only where a colour was in effect; may be empty
  };

  class Parser
  {
  public:
    // One output row, split by the caller on '\n' with any trailing '\r'
    // already gone. The returned text is what the buffer should hold.
    Row feed(const std::string &raw);

    // Forgets the colours in effect (a new run starts from the terminal's
    // defaults, not from whatever the last one left set).
    void reset();

  private:
    // Applies the sequence at `i` (an ESC byte) and returns the bytes it
    // occupied, so the row walk never re-reads one.
    size_t consume_escape(const std::string &text, size_t i);
    // Reads a `...m` parameter list: `text[begin, end)` is the parameter run.
    void apply_sgr(const std::string &text, size_t begin, size_t end);

    Color fg_;
    Color bg_;
    bool bold_ = false;
    bool dim_ = false;
    bool underline_ = false;
  };
} // namespace ansi_spans

#endif
