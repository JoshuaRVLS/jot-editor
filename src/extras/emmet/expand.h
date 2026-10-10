#ifndef EMMET_H
#define EMMET_H

#include <string>

// Abbreviation expansion: `div.box>p{Hello}` and `m10-20` typed as text and
// turned into markup and declarations.
//
// The engine is split in two so both halves are testable without an editor. The
// tokenizer answers "what did the author just type", the two expanders answer
// "what does it mean", and neither touches a buffer: the editor hands over a
// line and a cursor and gets back the snippet body (src/editor/lua/api/api_emmet.cpp
// splices it in through the snippet engine, which is what makes the result
// tabbable).
//
// The output is *snippet* text, not plain text: newlines are real newlines and
// the caret position is the `$0` stop, so a multi-line expansion lands with the
// caret where the next thing is typed rather than at the end of the last line.
namespace Emmet
{
  struct Expansion
  {
    std::string snippet; // the body, one line per '\n', `$0` for the caret
  };

  // The file types an abbreviation may be expanded in.
  bool is_markup_file(const std::string &path);
  bool is_css_file(const std::string &path);
  bool is_supported_file(const std::string &path);

  // The abbreviation token ending at `cursor` on `line`, or an empty string when
  // there is nothing expandable there. A tag name is a word, so the token stops
  // at whitespace; `css` swaps the alphabet for the shorthand one, which has no
  // room for `{}` or `>`.
  std::string abbreviation_before(const std::string &line, int cursor, bool css);

  // Markup: elements, `#id`, `.class`, `[attr]`, `{text}`, and the `>`, `+`, `^`
  // and `*` operators (with `$` numbering inside a repeat). `indent_unit` is one
  // level of nesting. Returns false when the abbreviation does not parse, which
  // is what keeps a stray word from being swallowed by a Tab. `!` is the
  // doctype.
  bool
  expand_markup(const std::string &abbr, const std::string &indent_unit, bool jsx, Expansion &out);

  // CSS: `m10-20` -> `margin: 10px 20px;`, `d:f` -> `display: flex;`, `c#fff` ->
  // `color: #fff;`, and a `+` chain as one declaration per line, `indent` before
  // each continuation. A property with no value gets the `$0` stop, so Tab lands
  // in the value.
  bool expand_css(const std::string &abbr, const std::string &indent, Expansion &out);
} // namespace Emmet

#endif
