#ifndef AUTOCLOSE_H
#define AUTOCLOSE_H

#include <string>

// Deciding what a typed pair character should do, kept free of the buffer so
// the rules are testable on their own.
class AutoClose
{
public:
  // Whether `c` is one of the characters that can open a pair, so typing it
  // over a selection wraps instead of replacing.
  static bool should_auto_close(char c);

  // The character that closes `c`, or '\0' when `c` is not a pair opener.
  static char get_closing_bracket(char c);

  // Whether `c` is a character that closes a pair, so it can step over its own
  // twin rather than insert a second one.
  static bool is_closing_bracket(char c);

  // Whether the character at `pos` carries an odd number of backslashes before
  // it, and so is escaped: literal text rather than syntax.
  static bool is_escaped(const std::string &line, int pos);

  // The quote the caret at `pos` is inside, or '\0' when it is not inside a
  // string. Escaped quotes are skipped and a quote of the other kind is text,
  // so the apostrophe of `"it's"` does not open a string of its own.
  static char open_quote_at(const std::string &line, int pos);

  // Whether typing `c` at `pos` should also insert its partner. A bracket pairs
  // unless the matching closer is already under the caret and nothing to its
  // left is waiting on it, in which case the closer is adopted instead. A quote
  // pairs only where it is the character being written rather than the end of
  // one already open: inside a string, or beside a word, it is text.
  static bool should_insert_pair(char c, const std::string &line, int pos);

  // Whether typing `c` at `pos` should step over the closer already there.
  static bool should_skip_closing(char c, const std::string &line, int pos);
};

#endif
