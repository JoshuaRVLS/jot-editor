#ifndef JOT_FEATURES_BIBLE_H
#define JOT_FEATURES_BIBLE_H

// The Bible reader (https://bible.helloao.org, the "free use" HelloAO API) as
// pure text work, so the editor side (extras/bible_client.cpp) only has to
// choose a URL, run one curl on the worker queue and paint the answer into a
// `[Bible]` scratch tab.
//
// Everything here is deterministic and testable:
//
//   * parse_reference() reads the way people actually type a verse --
//     `John 3:16`, `john3:16`, `1cor13:4-7`, `1 Cor 13:4-7`, `ps23`,
//     `1cor13 4-7` -- with the book named in full, as a USFM id or as one of
//     the usual abbreviations, and reports it as a Reference.
//   * reference_at() finds such a reference inside a line of prose (the hover
//     path: run the mouse over "John 3:16" in any file).
//   * the URL builders name the endpoints the API documents;
//   * parse_chapter()/parse_commentary()/parse_xrefs() read the JSON back
//     (via the DAP client's json parser) into plain structs;
//   * render_*() turn those structs into the lines the scratch tab shows.
//
// The API needs no key and sets no quota. A chapter is fetched in its `.simple`
// form (one string per verse, which is what a reader wants) and a commentary in
// its full form (prose per verse). Cross references come from the
// `open-cross-ref` dataset.

#include <string>
#include <vector>

namespace Bible
{
  // One of the 66 books, in canonical order. `aliases` is a space-separated
  // list of the spellings find_book() answers to, on top of the id and the
  // name (spaces and case are ignored when matching).
  struct Book
  {
    const char *id;      // "1CO" (what the API's URLs use)
    const char *name;    // "1 Corinthians" (what a reader reads)
    const char *aliases; // "1co 1cor 1corinthians 1 corinthians ..."
    int chapters;        // chapter count, from the translation metadata
  };

  const std::vector<Book> &books();
  // Null when `text` names no book. `text` may carry spaces, dots and the
  // ordinal ("1 cor", "1cor", "1 corinthians").
  const Book *find_book(const std::string &text);

  struct Reference
  {
    const Book *book = nullptr;
    int chapter = 0;
    // `has_verse` false means the whole chapter was asked for; otherwise the
    // span [verse_first, verse_last] inclusive (they are equal for one verse).
    bool has_verse = false;
    int verse_first = 0;
    int verse_last = 0;
    bool has_verse_span() const
    {
      return has_verse && verse_last > verse_first;
    }
  };

  // `John 3:16-17`, `John 3:16`, `John 3`, `1 Corinthians 13:4-7`.
  std::string reference_label(const Reference &reference);
  // `John 3:16` for a verse, `John 3` for a chapter.
  std::string reference_short(const Reference &reference);

  // The full reference parsed; false when no book name leads a chapter number.
  // A reference past the book's last chapter or with a nonsense verse (0, or a
  // span that runs backwards) is still parsed -- the caller decides whether to
  // report it, so a typo gets a message instead of silence.
  bool parse_reference(const std::string &text, Reference &out);

  // The reference a caret at `col` sits on inside `line`, if any. `start_col`
  // and `end_col` are the half-open byte span of the matched text, so a caller
  // can underline it or replace it.
  bool reference_at(const std::string &line, int col, int &start_col, int &end_col, Reference &out);

  // A heading or a verse, in the order the chapter lists them.
  struct Block
  {
    enum Kind
    {
      Heading,
      Verse
    } kind = Verse;
    int number = 0; // verse number (0 for a heading)
    std::string text;
  };

  struct Chapter
  {
    std::string translation_id;   // "BSB"
    std::string translation_name; // "Berean Standard Bible"
    std::string book_id;          // "JHN"
    std::string book_name;        // "John"
    int chapter = 0;
    std::vector<Block> blocks;
    // The chapter's own verses, keyed by number, for a quick lookup (hover,
    // the verse of the day). Built from `blocks` by the parsers.
    std::vector<int> verse_numbers;
  };

  // The verse's text, or null when the chapter carries no such verse.
  const std::string *verse_text(const Chapter &chapter, int number);

  // `.simple` chapter JSON (what the reader fetches).
  bool parse_chapter(const std::string &json, Chapter &out);
  // Full commentary chapter JSON (`chapter.content[]` holds prose per verse).
  bool parse_commentary(const std::string &json, Chapter &out);

  // The header line plus the chapter body: headings (when `show_headings`) and
  // one paragraph per verse, wrapped to `width`. A reference with a verse span
  // narrows the body to those verses (`context` verses either side when
  // `context > 0`); the requested verses read with their number in brackets so
  // they stand out from the context around them.
  struct RenderOptions
  {
    int width = 80;
    bool show_headings = true;
    int context = 0;
  };
  std::vector<std::string>
  render_chapter(const Chapter &chapter, const Reference &reference, const RenderOptions &options);
  // The same body without the surrounding chapter: used for the hover popup,
  // where only a verse or two has room.
  std::vector<std::string> render_verse(const Chapter &chapter, int verse);

  // How a rendered block reads, so highlight() can tell the layouts apart.
  enum class Layout
  {
    Chapter,     // render_chapter (a commentary chapter reads the same way)
    Xrefs,       // render_xrefs
    Translations // render_translations
  };

  // A coloured span over the lines a render_* function returned: `line` indexes
  // that vector, [start, end) is a byte range within it, and `group` names a
  // theme slot ("keyword", "type", "number", ...) the editor resolves to a
  // colour at paint time. Living next to the renderers keeps the two in step:
  // the layout the scanner expects is the layout the renderer just wrote, and
  // the whole thing stays pure and testable.
  struct Span
  {
    int line = 0;
    int start = 0;
    int end = 0;
    std::string group;
  };

  std::vector<Span> highlight(const std::vector<std::string> &lines, Layout layout);

  struct Xref
  {
    int verse = 0;    // the referring verse in this chapter
    std::string book; // "ROM"
    int chapter = 0;
    int verse_start = 0;
    int verse_end = 0; // equals verse_start for a single verse
    int score = 0;
  };

  bool parse_xrefs(const std::string &json, std::vector<Xref> &out);
  // `John 3:16  ->  Romans 5:8`, strongest match first, grouped by verse.
  std::vector<std::string>
  render_xrefs(const std::vector<Xref> &xrefs, const Reference &reference, int width);

  struct Translation
  {
    std::string id;
    std::string name;
    std::string language;
  };
  bool parse_translations(const std::string &json, std::vector<Translation> &out);
  std::vector<std::string> render_translations(const std::vector<Translation> &translations,
                                               int width);

  // The verse of the day: a stable pick for a given day number (unix days, so
  // the same day always lands on the same chapter and verse, whatever the time
  // of day or the timezone passed in). The chapter is picked here; the verse
  // needs the fetched chapter's length, so it comes from verse_for_day().
  Reference chapter_for_day(long long day);
  int verse_for_day(long long day, int verse_count);

  // The endpoints, byte-for-byte as the guide documents them. `base` is the API
  // root without a trailing slash ("https://bible.helloao.org/api").
  std::string translations_url(const std::string &base);
  std::string books_url(const std::string &base, const std::string &translation);
  std::string chapter_url(const std::string &base,
                          const std::string &translation,
                          const std::string &book_id,
                          int chapter);
  std::string commentary_url(const std::string &base,
                             const std::string &commentary,
                             const std::string &book_id,
                             int chapter);
  std::string xref_url(const std::string &base, const std::string &book_id, int chapter);

  // A cache file name for a URL: a hex FNV-1a digest plus ".json". The same URL
  // always maps to the same name, so a cached chapter survives across runs.
  std::string cache_file_name(const std::string &url);
} // namespace Bible

#endif
