// The Bible reader's pure half (see bible.h for what each piece is for).
//
// Nothing here touches the network or the editor: a reference goes in as text
// and comes out as a URL; a chapter's JSON goes in and comes out as the lines
// the `[Bible]` tab shows. That is what makes the whole feature testable
// without a server.
#include "features/bible.h"
#include "base/string_util.h"
#include "dap/client.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace Bible
{
  namespace
  {
    // The 66 books in canonical order. `aliases` is what a reader may type on
    // top of the id and the common name; matching ignores case, spaces and
    // punctuation (see normalize_book), so "1 Cor 13" finds "1cor13".
    const std::vector<Book> kBooks = {
        {"GEN", "Genesis", "gen ge gn genesis", 50},
        {"EXO", "Exodus", "exo ex exod exodus", 40},
        {"LEV", "Leviticus", "lev lv leviticus", 27},
        {"NUM", "Numbers", "num nu nm numbers", 36},
        {"DEU", "Deuteronomy", "deu deut dt deuteronomy", 34},
        {"JOS", "Joshua", "jos josh joshua", 24},
        {"JDG", "Judges", "jdg judg jg jsh judges", 21},
        {"RUT", "Ruth", "rut rth ruth", 4},
        {"1SA", "1 Samuel", "1sa 1sam 1samuel 1sm 1s firstsamuel", 31},
        {"2SA", "2 Samuel", "2sa 2sam 2samuel 2sm 2s secondsamuel", 24},
        {"1KI", "1 Kings", "1ki 1kgs 1kg 1kings firstkings", 22},
        {"2KI", "2 Kings", "2ki 2kgs 2kg 2kings secondkings", 25},
        {"1CH", "1 Chronicles", "1ch 1chr 1chron 1chronicles firstchronicles", 29},
        {"2CH", "2 Chronicles", "2ch 2chr 2chron 2chronicles secondchronicles", 36},
        {"EZR", "Ezra", "ezr ezra", 10},
        {"NEH", "Nehemiah", "neh ne nehemiah", 13},
        {"EST", "Esther", "est es esther", 10},
        {"JOB", "Job", "job jb job", 42},
        {"PSA", "Psalms", "psa ps pss psalm psalms", 150},
        {"PRO", "Proverbs", "pro prov prv proverbs", 31},
        {"ECC", "Ecclesiastes", "ecc eccl eccles ecclesiastes qoheleth", 12},
        {"SNG", "Song of Solomon", "sng sos song songs songofsolomon songofsongs canticles", 8},
        {"ISA", "Isaiah", "isa is isaiah", 66},
        {"JER", "Jeremiah", "jer je jeremiah", 52},
        {"LAM", "Lamentations", "lam la lamentations", 5},
        {"EZK", "Ezekiel", "ezk ezek ezekiel", 48},
        {"DAN", "Daniel", "dan dn da daniel", 12},
        {"HOS", "Hosea", "hos ho hosea", 14},
        {"JOL", "Joel", "jol joe joel", 3},
        {"AMO", "Amos", "amo am amos", 9},
        {"OBA", "Obadiah", "oba ob obadiah", 1},
        {"JON", "Jonah", "jon jonah", 4},
        {"MIC", "Micah", "mic mi micah", 7},
        {"NAM", "Nahum", "nam nah na nahum", 3},
        {"HAB", "Habakkuk", "hab habakkuk", 3},
        {"ZEP", "Zephaniah", "zep zeph zephaniah", 3},
        {"HAG", "Haggai", "hag haggai", 2},
        {"ZEC", "Zechariah", "zec zech zechariah", 14},
        {"MAL", "Malachi", "mal ml malachi", 4},
        {"MAT", "Matthew", "mat matt mt matthew", 28},
        {"MRK", "Mark", "mrk mk mr mark", 16},
        {"LUK", "Luke", "luk lk luke", 24},
        {"JHN", "John", "jhn jn john", 21},
        {"ACT", "Acts", "act acts", 28},
        {"ROM", "Romans", "rom ro rm romans", 16},
        {"1CO", "1 Corinthians", "1co 1cor 1corinthians firstcorinthians", 16},
        {"2CO", "2 Corinthians", "2co 2cor 2corinthians secondcorinthians", 13},
        {"GAL", "Galatians", "gal ga galatians", 6},
        {"EPH", "Ephesians", "eph ephesians", 6},
        {"PHP", "Philippians", "php phil philippians", 4},
        {"COL", "Colossians", "col colossians", 4},
        {"1TH", "1 Thessalonians", "1th 1thess 1thessalonians firstthessalonians", 5},
        {"2TH", "2 Thessalonians", "2th 2thess 2thessalonians secondthessalonians", 3},
        {"1TI", "1 Timothy", "1ti 1tim 1timothy firsttimothy", 6},
        {"2TI", "2 Timothy", "2ti 2tim 2timothy secondtimothy", 4},
        {"TIT", "Titus", "tit ti titus", 3},
        {"PHM", "Philemon", "phm phlm philemon", 1},
        {"HEB", "Hebrews", "heb hebrews", 13},
        {"JAS", "James", "jas jm james", 5},
        {"1PE", "1 Peter", "1pe 1pet 1peter firstpeter", 5},
        {"2PE", "2 Peter", "2pe 2pet 2peter secondpeter", 3},
        {"1JN", "1 John", "1jn 1jhn 1john firstjohn", 5},
        {"2JN", "2 John", "2jn 2jhn 2john secondjohn", 1},
        {"3JN", "3 John", "3jn 3jhn 3john thirdjohn", 1},
        {"JUD", "Jude", "jud jude", 1},
        {"REV", "Revelation", "rev re revelation revelations apocalypse", 22},
    };

    // Lowercase, spell out nothing, and drop every separator: "1 Cor. 13:4"
    // and "1cor13:4" both answer to "1cor". "1st/2nd/3rd" fold into 1/2/3 so
    // "1st John" reads like "1 John".
    std::string normalize_book(const std::string &text)
    {
      std::string lowered = string_util::lower_copy(text);
      for (const auto &pair : {std::make_pair(std::string("1st"), std::string("1")),
                               std::make_pair(std::string("2nd"), std::string("2")),
                               std::make_pair(std::string("3rd"), std::string("3"))})
      {
        size_t pos = 0;
        while ((pos = lowered.find(pair.first, pos)) != std::string::npos)
        {
          lowered.replace(pos, pair.first.size(), pair.second);
          pos += pair.second.size();
        }
      }
      std::string out;
      out.reserve(lowered.size());
      for (char c : lowered)
      {
        if (std::isalnum((unsigned char)c))
        {
          out.push_back(c);
        }
      }
      return out;
    }

    const std::unordered_map<std::string, const Book> &alias_index()
    {
      static const std::unordered_map<std::string, const Book> index = [] {
        std::unordered_map<std::string, const Book> built;
        for (const Book &book : kBooks)
        {
          built.emplace(normalize_book(book.id), book);
          built.emplace(normalize_book(book.name), book);
          std::string token;
          std::string aliases = book.aliases;
          aliases.push_back(' ');
          for (char c : aliases)
          {
            if (c == ' ')
            {
              if (!token.empty())
              {
                built.emplace(normalize_book(token), book);
                token.clear();
              }
            }
            else
            {
              token.push_back(c);
            }
          }
        }
        return built;
      }();
      return index;
    }

    // The rendered width of a UTF-8 string, counting a code point as one cell.
    // English Scripture has no double-width runes, so this is exact enough for
    // wrapping and needs no terminal.
    int cell_count(const std::string &text)
    {
      int cells = 0;
      for (size_t i = 0; i < text.size();)
      {
        const unsigned char c = (unsigned char)text[i];
        if (c < 0x80)
        {
          i += 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
          i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
          i += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
          i += 4;
        }
        else
        {
          i += 1;
        }
        cells++;
      }
      return cells;
    }

    // Greedy word wrap. `first_prefix` starts the paragraph; `next_prefix`
    // starts every continuation line, so a verse number stays out of the text
    // that follows it.
    std::vector<std::string> wrap_paragraph(const std::string &text,
                                            const std::string &first_prefix,
                                            const std::string &next_prefix,
                                            int width)
    {
      std::vector<std::string> lines;
      width = std::max(8, width);
      // A verse can carry hard line breaks of its own (poetry, an epistle's
      // greeting); each one starts a fresh paragraph with the continuation
      // prefix, not the number.
      std::vector<std::string> paragraphs;
      std::string current;
      for (char c : text)
      {
        if (c == '\n')
        {
          paragraphs.push_back(current);
          current.clear();
        }
        else
        {
          current.push_back(c);
        }
      }
      paragraphs.push_back(current);

      std::string prefix = first_prefix;
      for (const std::string &paragraph : paragraphs)
      {
        std::string line = prefix;
        bool has_word = false;
        std::string word;
        auto flush_word = [&]()
        {
          if (word.empty())
          {
            return;
          }
          const int next_width = cell_count(line) + (has_word ? 1 : 0) + cell_count(word);
          if (has_word && next_width > width && cell_count(line) > cell_count(prefix))
          {
            lines.push_back(line);
            prefix = next_prefix;
            line = prefix;
            has_word = false;
          }
          if (has_word)
          {
            line.push_back(' ');
          }
          line += word;
          has_word = true;
          word.clear();
        };
        for (char c : paragraph)
        {
          if (c == ' ' || c == '\t')
          {
            flush_word();
          }
          else
          {
            word.push_back(c);
          }
        }
        flush_word();
        lines.push_back(line);
        prefix = next_prefix;
      }
      return lines;
    }

    const Dap::Value *object(const Dap::Value &value, const char *key)
    {
      return value.type == Dap::Value::Object ? Dap::object_get(value, key) : nullptr;
    }

    // The same lookup from a possibly-null parent: a missing (or renamed)
    // container degrades to a default instead of dereferencing nothing.
    const Dap::Value *field(const Dap::Value *value, const char *key)
    {
      return value ? object(*value, key) : nullptr;
    }

    std::string book_id_of(const Dap::Value &root, const char *key)
    {
      return Dap::string_or_empty(field(field(&root, key), "id"));
    }

    std::string book_name_of(const Dap::Value &root, const char *key)
    {
      const Dap::Value *book = object(root, key);
      std::string common = Dap::string_or_empty(field(book, "commonName"));
      return common.empty() ? Dap::string_or_empty(field(book, "name")) : common;
    }
  } // namespace

  const std::vector<Book> &books()
  {
    return kBooks;
  }

  const Book *find_book(const std::string &text)
  {
    const auto &index = alias_index();
    auto it = index.find(normalize_book(text));
    return it == index.end() ? nullptr : &it->second;
  }

  std::string reference_label(const Reference &reference)
  {
    if (!reference.book)
    {
      return {};
    }
    std::string label = std::string(reference.book->name) + " " + std::to_string(reference.chapter);
    if (reference.has_verse)
    {
      label += ":" + std::to_string(reference.verse_first);
      if (reference.verse_last > reference.verse_first)
      {
        label += "-" + std::to_string(reference.verse_last);
      }
    }
    return label;
  }

  std::string reference_short(const Reference &reference)
  {
    if (!reference.book)
    {
      return {};
    }
    std::string label = std::string(reference.book->name) + " " + std::to_string(reference.chapter);
    if (reference.has_verse)
    {
      label += ":" + std::to_string(reference.verse_first);
    }
    return label;
  }

  bool parse_reference(const std::string &text, Reference &out)
  {
    // Collapse runs of whitespace so "1 cor 13" and "1  cor 13" read alike.
    std::string s = string_util::lower_copy(text);
    std::string collapsed;
    bool pending_space = false;
    for (char c : s)
    {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      {
        pending_space = !collapsed.empty();
        continue;
      }
      if (pending_space)
      {
        collapsed.push_back(' ');
        pending_space = false;
      }
      collapsed.push_back(c);
    }
    // Trim separators a reader might trail ("John 3:16.").
    while (!collapsed.empty() && (collapsed.back() == '.' || collapsed.back() == ','))
    {
      collapsed.pop_back();
    }
    s = collapsed;
    if (s.empty())
    {
      return false;
    }

    const size_t n = s.size();
    for (size_t i = 1; i <= n; i++)
    {
      std::string book_raw = s.substr(0, i);
      while (!book_raw.empty() && (book_raw.back() == ' ' || book_raw.back() == '.'))
      {
        book_raw.pop_back();
      }
      if (book_raw.empty())
      {
        continue;
      }
      const Book *book = find_book(book_raw);
      if (!book)
      {
        continue;
      }

      size_t p = i;
      while (p < n && s[p] == ' ')
      {
        p++;
      }
      if (p >= n)
      {
        // A bare book name means its first chapter.
        if (i == n)
        {
          out.book = book;
          out.chapter = 1;
          out.has_verse = false;
          out.verse_first = 0;
          out.verse_last = 0;
          return true;
        }
        continue;
      }
      if (!std::isdigit((unsigned char)s[p]))
      {
        continue;
      }

      int chapter = 0;
      while (p < n && std::isdigit((unsigned char)s[p]))
      {
        chapter = chapter * 10 + (s[p] - '0');
        p++;
      }
      if (chapter <= 0)
      {
        continue;
      }

      bool has_verse = false;
      int verse_first = 0;
      int verse_last = 0;
      size_t q = p;
      while (q < n && s[q] == ' ')
      {
        q++;
      }
      if (q < n)
      {
        if (s[q] == ':' || s[q] == '.' || s[q] == ',')
        {
          q++;
          while (q < n && s[q] == ' ')
          {
            q++;
          }
        }
        else if (q == p)
        {
          // "john3abc": digits ran straight into something that is not a
          // verse separator, so this split is not a reference.
          continue;
        }
        if (q >= n || !std::isdigit((unsigned char)s[q]))
        {
          continue;
        }
        while (q < n && std::isdigit((unsigned char)s[q]))
        {
          verse_first = verse_first * 10 + (s[q] - '0');
          q++;
        }
        verse_last = verse_first;
        size_t r = q;
        while (r < n && s[r] == ' ')
        {
          r++;
        }
        if (r < n && (s[r] == '-' || (s[r] == '.' && r + 1 < n && s[r + 1] == '.')))
        {
          size_t r2 = s[r] == '-' ? r + 1 : r + 2;
          while (r2 < n && s[r2] == ' ')
          {
            r2++;
          }
          if (r2 < n && std::isdigit((unsigned char)s[r2]))
          {
            verse_last = 0;
            while (r2 < n && std::isdigit((unsigned char)s[r2]))
            {
              verse_last = verse_last * 10 + (s[r2] - '0');
              r2++;
            }
            q = r2;
          }
        }
        while (q < n && s[q] == ' ')
        {
          q++;
        }
        if (q != n || verse_first <= 0)
        {
          continue;
        }
        has_verse = true;
      }
      else if (q != n)
      {
        continue;
      }

      out.book = book;
      out.chapter = chapter;
      out.has_verse = has_verse;
      out.verse_first = has_verse ? verse_first : 0;
      out.verse_last = has_verse ? std::max(verse_first, verse_last) : 0;
      return true;
    }
    return false;
  }

  bool reference_at(const std::string &line,
                    int col,
                    int &start_col,
                    int &end_col,
                    Reference &out)
  {
    const int n = (int)line.size();
    if (n == 0)
    {
      return false;
    }
    col = std::clamp(col, 0, n - 1);
    auto allowed = [](char c) {
      return std::isalnum((unsigned char)c) != 0 || c == ' ' || c == ':' || c == '.'
             || c == '-' || c == '\'';
    };
    int left = std::max(0, col - 40);
    int right = std::min(n, col + 41);
    while (left > 0 && allowed(line[left - 1]))
    {
      left--;
    }
    while (right < n && allowed(line[right]))
    {
      right++;
    }

    bool found = false;
    int best_len = 0;
    for (int start = left; start <= col; start++)
    {
      for (int end = right; end > col; end--)
      {
        if (line[start] == ' ' || line[end - 1] == ' ')
        {
          continue;
        }
        const std::string piece = line.substr((size_t)start, (size_t)(end - start));
        Reference candidate;
        if (!parse_reference(piece, candidate))
        {
          continue;
        }
        const int length = end - start;
        if (length >= best_len)
        {
          best_len = length;
          // Keep trailing punctuation (a comma, a period) out of the span, so
          // the caller can underline or replace exactly the reference.
          int trimmed_end = end;
          while (trimmed_end > start && !std::isalnum((unsigned char)line[trimmed_end - 1]))
          {
            trimmed_end--;
          }
          int trimmed_start = start;
          while (trimmed_start < trimmed_end
                 && !std::isalnum((unsigned char)line[trimmed_start]))
          {
            trimmed_start++;
          }
          // A reference that runs straight into more word characters
          // ("john3abc") is an identifier, not a citation.
          if ((trimmed_end < n && (std::isalnum((unsigned char)line[trimmed_end]) != 0
                                   || line[trimmed_end] == '_'))
              || (trimmed_start > 0 && (std::isalnum((unsigned char)line[trimmed_start - 1]) != 0
                                        || line[trimmed_start - 1] == '_')))
          {
            continue;
          }
          start_col = trimmed_start;
          end_col = trimmed_end;
          out = candidate;
          found = true;
        }
      }
    }
    return found;
  }

  const std::string *verse_text(const Chapter &chapter, int number)
  {
    for (const Block &block : chapter.blocks)
    {
      if (block.kind == Block::Verse && block.number == number)
      {
        return &block.text;
      }
    }
    return nullptr;
  }

  bool parse_chapter(const std::string &json, Chapter &out)
  {
    Dap::Value root;
    if (!Dap::parse_json(json, root) || root.type != Dap::Value::Object)
    {
      return false;
    }
    const Dap::Value *translation = object(root, "translation");
    out.translation_id = Dap::string_or_empty(field(translation, "id"));
    std::string english = Dap::string_or_empty(field(translation, "englishName"));
    out.translation_name =
        english.empty() ? Dap::string_or_empty(field(translation, "name")) : english;
    out.book_id = book_id_of(root, "book");
    out.book_name = book_name_of(root, "book");

    const Dap::Value *chapter = object(root, "chapter");
    out.chapter = Dap::int_or_default(field(chapter, "number"), 0);
    const Dap::Value *content = field(chapter, "content");
    if (!content || content->type != Dap::Value::Array)
    {
      return false;
    }
    for (const Dap::Value &item : content->array_value)
    {
      const std::string type = Dap::string_or_empty(object(item, "type"));
      if (type == "heading")
      {
        Block block;
        block.kind = Block::Heading;
        block.text = Dap::string_or_empty(object(item, "text"));
        if (!block.text.empty())
        {
          out.blocks.push_back(block);
        }
      }
      else if (type == "verse")
      {
        Block block;
        block.kind = Block::Verse;
        block.number = Dap::int_or_default(object(item, "number"), 0);
        block.text = Dap::string_or_empty(object(item, "text"));
        if (block.number > 0)
        {
          out.blocks.push_back(block);
          out.verse_numbers.push_back(block.number);
        }
      }
    }
    return out.chapter > 0;
  }

  bool parse_commentary(const std::string &json, Chapter &out)
  {
    Dap::Value root;
    if (!Dap::parse_json(json, root) || root.type != Dap::Value::Object)
    {
      return false;
    }
    const Dap::Value *commentary = object(root, "commentary");
    out.translation_id = Dap::string_or_empty(field(commentary, "id"));
    std::string english = Dap::string_or_empty(field(commentary, "englishName"));
    out.translation_name =
        english.empty() ? Dap::string_or_empty(field(commentary, "name")) : english;
    out.book_id = book_id_of(root, "book");
    out.book_name = book_name_of(root, "book");

    const Dap::Value *chapter = object(root, "chapter");
    out.chapter = Dap::int_or_default(field(chapter, "number"), 0);
    const Dap::Value *content = field(chapter, "content");
    if (!content || content->type != Dap::Value::Array)
    {
      return false;
    }

    int expected = 0;
    for (const Dap::Value &item : content->array_value)
    {
      const std::string type = Dap::string_or_empty(object(item, "type"));
      if (type != "verse")
      {
        continue;
      }
      // A commentary may omit the number (the verse is still the next one).
      int number = Dap::int_or_default(object(item, "number"), 0);
      if (number <= 0)
      {
        number = expected + 1;
      }
      expected = number;
      // `content` is a list of prose fragments (sometimes nested); join the
      // strings in order with a newline between them.
      std::string text;
      const Dap::Value *fragments = object(item, "content");
      if (fragments)
      {
        for (const Dap::Value &fragment : fragments->array_value)
        {
          if (fragment.type != Dap::Value::String)
          {
            continue;
          }
          if (!text.empty())
          {
            text += "\n";
          }
          text += fragment.string_value;
        }
      }
      if (!text.empty())
      {
        Block block;
        block.kind = Block::Verse;
        block.number = number;
        block.text = text;
        out.blocks.push_back(block);
        out.verse_numbers.push_back(number);
      }
    }
    return out.chapter > 0;
  }

  std::vector<std::string> render_chapter(const Chapter &chapter,
                                          const Reference &reference,
                                          const RenderOptions &options)
  {
    std::vector<std::string> lines;
    const std::string title =
        chapter.book_name.empty() ? std::string("Bible") : chapter.book_name;
    std::string header = title + " " + std::to_string(chapter.chapter);
    if (!chapter.translation_name.empty())
    {
      header += " - " + chapter.translation_name;
      if (!chapter.translation_id.empty())
      {
        header += " (" + chapter.translation_id + ")";
      }
    }
    lines.push_back(header);

    const int context = std::max(0, options.context);
    const int lo = reference.has_verse ? std::max(1, reference.verse_first - context) : 0;
    const int hi = reference.has_verse ? reference.verse_last + context : 0;
    auto included = [&](int number) {
      return !reference.has_verse || (number >= lo && number <= hi);
    };
    auto requested = [&](int number) {
      return reference.has_verse && number >= reference.verse_first
             && number <= reference.verse_last;
    };

    int deepest = 1;
    for (const Block &block : chapter.blocks)
    {
      if (block.kind == Block::Verse)
      {
        deepest = std::max(deepest, block.number);
      }
    }
    // A one-column gutter (`>` for a requested verse, a space otherwise), the
    // number right-aligned to the chapter's widest, then two spaces. The
    // continuation indent lines up under the text.
    const size_t number_width = std::to_string(deepest).size();
    const std::string indent(number_width + 3, ' ');

    std::vector<std::string> pending_headings;
    for (const Block &block : chapter.blocks)
    {
      if (block.kind == Block::Heading)
      {
        pending_headings.push_back(block.text);
        continue;
      }
      if (!included(block.number))
      {
        continue;
      }
      if (options.show_headings && !pending_headings.empty())
      {
        if (lines.size() > 1)
        {
          lines.push_back("");
        }
        for (const std::string &heading : pending_headings)
        {
          lines.push_back(heading);
        }
      }
      pending_headings.clear();

      const std::string number = std::to_string(block.number);
      std::string prefix = requested(block.number) ? ">" : " ";
      prefix += std::string(number_width - number.size(), ' ');
      prefix += number + "  ";
      std::vector<std::string> wrapped =
          wrap_paragraph(block.text, prefix, indent, options.width);
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }

    if (lines.size() == 1)
    {
      lines.push_back("");
      lines.push_back(reference.has_verse ? "No such verse in this chapter"
                                          : "No verses in this chapter");
    }
    return lines;
  }

  std::vector<std::string> render_verse(const Chapter &chapter, int verse)
  {
    std::vector<std::string> lines;
    const std::string *text = verse_text(chapter, verse);
    const std::string title =
        chapter.book_name.empty() ? std::string("Bible") : chapter.book_name;
    std::string header = title + " " + std::to_string(chapter.chapter) + ":" + std::to_string(verse);
    if (!chapter.translation_id.empty())
    {
      header += " (" + chapter.translation_id + ")";
    }
    lines.push_back(header);
    if (!text)
    {
      lines.push_back("Verse not found");
      return lines;
    }
    std::vector<std::string> body = wrap_paragraph(*text, "", "", 72);
    lines.insert(lines.end(), body.begin(), body.end());
    return lines;
  }

  bool parse_xrefs(const std::string &json, std::vector<Xref> &out)
  {
    Dap::Value root;
    if (!Dap::parse_json(json, root) || root.type != Dap::Value::Object)
    {
      return false;
    }
    const Dap::Value *chapter = object(root, "chapter");
    const Dap::Value *content = object(*chapter, "content");
    if (!content || content->type != Dap::Value::Array)
    {
      return false;
    }
    for (const Dap::Value &item : content->array_value)
    {
      const int verse = Dap::int_or_default(object(item, "verse"), 0);
      const Dap::Value *references = object(item, "references");
      if (!references || references->type != Dap::Value::Array)
      {
        continue;
      }
      for (const Dap::Value &entry : references->array_value)
      {
        Xref xref;
        xref.verse = verse;
        xref.book = Dap::string_or_empty(object(entry, "book"));
        xref.chapter = Dap::int_or_default(object(entry, "chapter"), 0);
        xref.verse_start = Dap::int_or_default(object(entry, "verse"), 0);
        xref.verse_end = Dap::int_or_default(object(entry, "endVerse"), xref.verse_start);
        xref.score = Dap::int_or_default(object(entry, "score"), 0);
        out.push_back(xref);
      }
    }
    std::stable_sort(out.begin(), out.end(), [](const Xref &a, const Xref &b) {
      if (a.verse != b.verse)
      {
        return a.verse < b.verse;
      }
      return a.score > b.score;
    });
    return true;
  }

  std::vector<std::string> render_xrefs(const std::vector<Xref> &xrefs,
                                        const Reference &reference,
                                        int width)
  {
    std::vector<std::string> lines;
    const std::string title = reference.book ? reference.book->name : std::string("Bible");
    lines.push_back("Cross references - " + title + " " + std::to_string(reference.chapter)
                    + " (open-cross-ref)");
    if (xrefs.empty())
    {
      lines.push_back("");
      lines.push_back("No cross references for this chapter");
      return lines;
    }

    const std::string from_name = reference.book ? reference.book->name : std::string();
    // A group repeats its "from" once; the lines under it indent to the arrow
    // column so the list still reads as one passage pointing many places.
    int widest_verse = 1;
    int chapter = reference.chapter;
    for (const Xref &xref : xrefs)
    {
      widest_verse = std::max(widest_verse, xref.verse);
    }
    const std::string from_column =
        from_name + " " + std::to_string(chapter) + ":";
    const std::string indent(from_column.size() + std::to_string(widest_verse).size(), ' ');

    int last_verse = -1;
    for (const Xref &xref : xrefs)
    {
      std::string line;
      if (xref.verse != last_verse)
      {
        line = from_column + std::to_string(xref.verse);
        last_verse = xref.verse;
      }
      else
      {
        line = indent;
      }
      line += "  ->  ";
      const Book *to_book = find_book(xref.book);
      line += to_book ? to_book->name : xref.book;
      line += " " + std::to_string(xref.chapter) + ":" + std::to_string(xref.verse_start);
      if (xref.verse_end > xref.verse_start)
      {
        line += "-" + std::to_string(xref.verse_end);
      }
      if (xref.score > 0)
      {
        line += "  (" + std::to_string(xref.score) + ")";
      }
      if (width > 12 && cell_count(line) > width)
      {
        line = line.substr(0, (size_t)std::max(0, width - 3)) + "...";
      }
      lines.push_back(line);
    }
    return lines;
  }

  bool parse_translations(const std::string &json, std::vector<Translation> &out)
  {
    Dap::Value root;
    if (!Dap::parse_json(json, root) || root.type != Dap::Value::Object)
    {
      return false;
    }
    const Dap::Value *translations = object(root, "translations");
    if (!translations || translations->type != Dap::Value::Array)
    {
      return false;
    }
    for (const Dap::Value &item : translations->array_value)
    {
      Translation translation;
      translation.id = Dap::string_or_empty(object(item, "id"));
      std::string english = Dap::string_or_empty(object(item, "englishName"));
      translation.name =
          english.empty() ? Dap::string_or_empty(object(item, "name")) : english;
      translation.language = Dap::string_or_empty(object(item, "language"));
      if (!translation.id.empty())
      {
        out.push_back(translation);
      }
    }
    std::sort(out.begin(), out.end(), [](const Translation &a, const Translation &b) {
      return a.id < b.id;
    });
    return true;
  }

  std::vector<std::string> render_translations(const std::vector<Translation> &translations,
                                               int width)
  {
    std::vector<std::string> lines;
    lines.push_back("Available translations (" + std::to_string(translations.size()) + ")");
    lines.push_back("");
    for (const Translation &translation : translations)
    {
      std::string line = translation.id;
      if ((int)line.size() < 10)
      {
        line += std::string((size_t)(10 - line.size()), ' ');
      }
      line += translation.name;
      if ((int)cell_count(line) > width && width > 12)
      {
        // Trim the tail rather than let the terminal fold it.
        int cells = 0;
        size_t cut = 0;
        while (cut < line.size() && cells < width)
        {
          const unsigned char c = (unsigned char)line[cut];
          cut += (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : 4));
          cells++;
        }
        line = line.substr(0, cut);
      }
      lines.push_back(line);
    }
    return lines;
  }

  std::vector<Span> highlight(const std::vector<std::string> &lines, Layout layout)
  {
    std::vector<Span> spans;
    if (lines.empty())
    {
      return spans;
    }
    // The header names the passage and the translation; every layout leads with
    // one.
    spans.push_back({0, 0, (int)lines[0].size(), "keyword"});

    for (int i = 1; i < (int)lines.size(); i++)
    {
      const std::string &line = lines[i];
      if (line.empty())
      {
        continue;
      }

      if (layout == Layout::Chapter)
      {
        // A verse row is a one-column gutter (a space, or `>` for a requested
        // verse), the number right-aligned, then two spaces. Anything else
        // that starts in column 0 is an editorial heading.
        if (line[0] == '>' || line[0] == ' ')
        {
          const bool requested = line[0] == '>';
          // Column 0 is the one-column gutter; the number follows it, padded
          // out so the chapter's widest verse lines up.
          int digit = 1;
          while (digit < (int)line.size() && line[digit] == ' ')
          {
            digit++;
          }
          int end = digit;
          while (end < (int)line.size() && std::isdigit((unsigned char)line[end]))
          {
            end++;
          }
          if (end == digit || end + 1 >= (int)line.size() || line[end] != ' '
              || line[end + 1] != ' ')
          {
            continue; // a wrapped continuation, not a verse row
          }
          if (requested)
          {
            spans.push_back({i, 0, 1, "constant"});
          }
          spans.push_back({i, digit, end, requested ? "constant" : "number"});
        }
        else
        {
          spans.push_back({i, 0, (int)line.size(), "type"});
        }
        continue;
      }

      if (layout == Layout::Xrefs)
      {
        // "John 3:16  ->  Romans 5:8  (9)": the referring verse, the arrow,
        // then the verse it points at (the score stays plain).
        const size_t arrow = line.find("  ->  ");
        if (arrow == std::string::npos)
        {
          continue;
        }
        if (arrow > 0)
        {
          spans.push_back({i, 0, (int)arrow, "number"});
        }
        spans.push_back({i, (int)arrow, (int)arrow + 6, "punctuation"});
        int end = (int)line.size();
        const size_t score = line.find("  (", arrow);
        if (score != std::string::npos)
        {
          end = (int)score;
        }
        spans.push_back({i, (int)arrow + 6, end, "string"});
        continue;
      }

      // Translations: `BSB` padded, then the name.
      int end = 0;
      while (end < (int)line.size() && line[end] != ' ')
      {
        end++;
      }
      if (end > 0)
      {
        spans.push_back({i, 0, end, "constant"});
      }
    }
    return spans;
  }

  Reference chapter_for_day(long long day)
  {
    int total = 0;
    for (const Book &book : kBooks)
    {
      total += book.chapters;
    }
    // A tiny splitmix so consecutive days land far apart instead of walking
    // Genesis one chapter a day.
    uint64_t x = (uint64_t)day + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    int index = total > 0 ? (int)(x % (uint64_t)total) : 0;

    Reference reference;
    for (const Book &book : kBooks)
    {
      if (index < book.chapters)
      {
        reference.book = &book;
        reference.chapter = index + 1;
        return reference;
      }
      index -= book.chapters;
    }
    reference.book = &kBooks.back();
    reference.chapter = kBooks.back().chapters;
    return reference;
  }

  int verse_for_day(long long day, int verse_count)
  {
    if (verse_count <= 0)
    {
      return 1;
    }
    uint64_t x = (uint64_t)day * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x = x ^ (x >> 31);
    return (int)(x % (uint64_t)verse_count) + 1;
  }

  namespace
  {
    std::string trim_base(const std::string &base)
    {
      std::string trimmed = base;
      while (!trimmed.empty() && trimmed.back() == '/')
      {
        trimmed.pop_back();
      }
      return trimmed;
    }
  } // namespace

  std::string translations_url(const std::string &base)
  {
    return trim_base(base) + "/available_translations.json";
  }

  std::string books_url(const std::string &base, const std::string &translation)
  {
    return trim_base(base) + "/" + translation + "/books.json";
  }

  std::string chapter_url(const std::string &base,
                          const std::string &translation,
                          const std::string &book_id,
                          int chapter)
  {
    return trim_base(base) + "/" + translation + "/" + book_id + "/"
           + std::to_string(chapter) + ".simple.json";
  }

  std::string commentary_url(const std::string &base,
                             const std::string &commentary,
                             const std::string &book_id,
                             int chapter)
  {
    return trim_base(base) + "/c/" + commentary + "/" + book_id + "/"
           + std::to_string(chapter) + ".json";
  }

  std::string xref_url(const std::string &base,
                       const std::string &book_id,
                       int chapter)
  {
    return trim_base(base) + "/d/open-cross-ref/" + book_id + "/"
           + std::to_string(chapter) + ".json";
  }

  std::string cache_file_name(const std::string &url)
  {
    uint64_t hash = 14695981039346656037ull;
    for (char c : url)
    {
      hash ^= (unsigned char)c;
      hash *= 1099511628211ull;
    }
    const char *digits = "0123456789abcdef";
    std::string name(16, '0');
    for (int i = 15; i >= 0; i--)
    {
      name[(size_t)i] = digits[hash & 0xF];
      hash >>= 4;
    }
    // The digest is pasted to a fixed prefix so a truncated cache is at least
    // identifiable as ours.
    return "bible-" + name + ".json";
  }
} // namespace Bible
