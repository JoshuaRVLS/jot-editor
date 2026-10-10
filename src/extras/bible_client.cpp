// The editor side of the Bible reader (see extras/bible.h for the reference
// grammar, the URL builders and the renderers, and api/bible.h for the
// commands). What is left here is plumbing:
//
//   * the fetch, on the worker queue (like the REST client), so a slow API
//     never costs a keystroke;
//   * the cache under JOT_CACHE_HOME, so a passage seen once is instant and
//     offline forever after -- including the verse of the day, which costs the
//     day's first home-screen frame one request and nothing after that;
//   * the one `[Bible]` scratch tab every look re-uses, so walking chapters
//     replaces the text where it is instead of stacking a tab per verse; the
//     tab is read-only, because a passage is something to read.
//
// Reports are toasts (`set_message(..., true)`), the same reason the REST
// client uses them: the status line belongs to the Lua UI kit in a real
// session, and a plain message would be written but never seen.
#include "base/string_util.h"
#include "core/editor.h"
#include "core/decorations_rebase.h"
#include "extras/bible.h"
#include "extras/leetcode_http.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace
{
  // The repo's libcurl client (extras/leetcode_http.*) is a plain HTTP
  // GET/POST client that happens to be named for its first caller; reusing it
  // keeps the Bible reader out of the shell (no command line to quote wrong)
  // and off the main thread.
  bool http_get(const std::string &url, int timeout_s, std::string &body, std::string &error)
  {
    LeetCodeHttp::Request request;
    request.method = "GET";
    request.url = url;
    request.timeout_seconds = timeout_s;
    const LeetCodeHttp::Response response = LeetCodeHttp::perform(request);
    if (!response.ok)
    {
      error = response.error.empty() ? "HTTP " + std::to_string(response.status) : response.error;
      return false;
    }
    if (response.status < 200 || response.status >= 300)
    {
      error = "HTTP " + std::to_string(response.status);
      return false;
    }
    body = response.body;
    return true;
  }

  // Mirrors the LeetCode cache's rules (runtime/lua/features/leetcode/cache.lua): an
  // explicit JOT_CACHE_HOME wins, then the platform's cache dir, with "jot"
  // and then the feature under it.
  fs::path bible_cache_root()
  {
    const char *override_home = std::getenv("JOT_CACHE_HOME");
    if (override_home && *override_home)
    {
      return fs::path(override_home) / "bible";
    }
#ifdef _WIN32
    const char *local = std::getenv("LOCALAPPDATA");
    if (local && *local)
    {
      return fs::path(local) / "jot" / "cache" / "bible";
    }
    const char *app_data = std::getenv("APPDATA");
    if (app_data && *app_data)
    {
      return fs::path(app_data) / "jot" / "cache" / "bible";
    }
#else
    const char *xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && *xdg)
    {
      return fs::path(xdg) / "jot" / "bible";
    }
    const char *home = std::getenv("HOME");
    if (home && *home)
    {
      return fs::path(home) / ".cache" / "jot" / "bible";
    }
#endif
    return fs::temp_directory_path() / "jot-bible";
  }

  bool bible_cache_read(const std::string &url, std::string &body)
  {
    std::error_code ec;
    const fs::path path = bible_cache_root() / Bible::cache_file_name(url);
    if (!fs::is_regular_file(path, ec) || ec)
    {
      return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
      return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    body = text.str();
    return !body.empty();
  }

  void bible_cache_write(const std::string &url, const std::string &body)
  {
    std::error_code ec;
    const fs::path root = bible_cache_root();
    fs::create_directories(root, ec);
    if (ec)
    {
      return;
    }
    std::ofstream out(root / Bible::cache_file_name(url), std::ios::binary | std::ios::trunc);
    if (!out)
    {
      return;
    }
    out << body;
  }

  // Unix days, so "today" flips at midnight UTC and the verse is stable for
  // the whole day wherever the editor runs.
  long long today_day()
  {
    const std::time_t now = std::time(nullptr);
    return now <= 0 ? 0 : (long long)(now / 86400);
  }

  // The reference a fetched chapter belongs to: the random and verse-of-the-day
  // paths start from a chapter and pick a verse only once the JSON says how
  // many there are.
  Bible::Reference narrowed(const Bible::Reference &reference, int verse)
  {
    Bible::Reference out = reference;
    out.has_verse = true;
    out.verse_first = verse;
    out.verse_last = verse;
    return out;
  }
} // namespace

std::string Editor::bible_translation()
{
  std::string id = string_util::trim_copy(config.get("bible_translation", "BSB"));
  return id.empty() ? std::string("BSB") : id;
}

std::string Editor::bible_commentary()
{
  std::string id = string_util::trim_copy(config.get("bible_commentary", "adam-clarke"));
  return id.empty() ? std::string("adam-clarke") : id;
}

std::string Editor::bible_api_base()
{
  std::string base =
      string_util::trim_copy(config.get("bible_api_base", "https://bible.helloao.org/api"));
  return base.empty() ? std::string("https://bible.helloao.org/api") : base;
}

int Editor::bible_render_width()
{
  if (!ui)
  {
    return 96;
  }
  return std::max(40, ui->get_render_width() - 6);
}

int Editor::bible_context_verses()
{
  return std::clamp(config.get_int("bible_context_verses", 2), 0, 10);
}

void Editor::bible_run(const std::string &arg)
{
  const std::string trimmed = string_util::trim_copy(arg);
  const std::string lower = string_util::lower_copy(trimmed);

  if (trimmed.empty() || lower == "votd" || lower == "daily" || lower == "today")
  {
    bible_open_verse_of_the_day(true);
    return;
  }
  if (lower == "random")
  {
    bible_open_random();
    return;
  }
  if (lower == "next" || lower == "n")
  {
    bible_step(true);
    return;
  }
  if (lower == "prev" || lower == "previous" || lower == "p")
  {
    bible_step(false);
    return;
  }
  if (lower == "translations" || lower == "versions")
  {
    bible_open_translations();
    return;
  }
  if (lower == "commentary" || lower == "notes")
  {
    if (!bible_has_last)
    {
      set_message("Bible: open a passage first, or :bible commentary <reference>", true);
      return;
    }
    bible_open_commentary(bible_last);
    return;
  }
  if (string_util::starts_with_icase(lower, "commentary "))
  {
    Bible::Reference reference;
    if (!Bible::parse_reference(trimmed.substr(11), reference))
    {
      set_message("Bible: cannot read \"" + trimmed.substr(11) + "\"", true);
      return;
    }
    bible_open_commentary(reference);
    return;
  }
  if (lower == "xref" || lower == "xrefs" || lower == "cross")
  {
    if (!bible_has_last)
    {
      set_message("Bible: open a passage first, or :bible xref <reference>", true);
      return;
    }
    bible_open_xref(bible_last);
    return;
  }
  if (string_util::starts_with_icase(lower, "xref ")
      || string_util::starts_with_icase(lower, "xrefs "))
  {
    const size_t space = trimmed.find(' ');
    Bible::Reference reference;
    if (!Bible::parse_reference(trimmed.substr(space + 1), reference))
    {
      set_message("Bible: cannot read \"" + trimmed.substr(space + 1) + "\"", true);
      return;
    }
    bible_open_xref(reference);
    return;
  }

  Bible::Reference reference;
  if (!Bible::parse_reference(trimmed, reference))
  {
    set_message("Bible: cannot read \"" + trimmed
                    + "\" -- try `:bible John 3:16`, `:bible ps23`, `:bible random`",
                true);
    return;
  }
  bible_open(reference, true);
}

void Editor::bible_open(const Bible::Reference &reference, bool announce)
{
  if (!reference.book)
  {
    set_message("Bible: no such book", true);
    return;
  }
  if (reference.chapter < 1 || reference.chapter > reference.book->chapters)
  {
    set_message("Bible: " + std::string(reference.book->name) + " has "
                    + std::to_string(reference.book->chapters)
                    + (reference.book->chapters == 1 ? " chapter" : " chapters"),
                true);
    return;
  }
  bible_last = reference;
  bible_has_last = true;
  bible_fetch(Bible::chapter_url(
                  bible_api_base(), bible_translation(), reference.book->id, reference.chapter),
              BibleJob::Chapter,
              reference,
              false,
              announce);
}

void Editor::bible_open_random()
{
  std::random_device device;
  const long long seed =
      (long long)(((unsigned long long)device() << 32) ^ (unsigned long long)device()
                  ^ (unsigned long long)std::time(nullptr));
  bible_random_seed = seed;
  // One seed picks both the chapter and the verse inside it, so the draw is
  // reproducible from the state a test can read back.
  const Bible::Reference chapter = Bible::chapter_for_day(seed);
  bible_last = chapter;
  bible_has_last = true;
  bible_fetch(
      Bible::chapter_url(bible_api_base(), bible_translation(), chapter.book->id, chapter.chapter),
      BibleJob::Chapter,
      chapter,
      true,
      true);
}

void Editor::bible_step(bool forward)
{
  if (!bible_has_last || !bible_last.book)
  {
    set_message("Bible: nothing to step from yet -- open a passage first", true);
    return;
  }
  const std::vector<Bible::Book> &canon = Bible::books();
  size_t index = 0;
  for (size_t i = 0; i < canon.size(); i++)
  {
    if (&canon[i] == bible_last.book)
    {
      index = i;
      break;
    }
  }

  Bible::Reference next = bible_last;
  next.has_verse = false;
  next.verse_first = 0;
  next.verse_last = 0;
  if (forward)
  {
    if (next.chapter < next.book->chapters)
    {
      next.chapter++;
    }
    else if (index + 1 < canon.size())
    {
      next.book = &canon[index + 1];
      next.chapter = 1;
    }
    else
    {
      set_message("Bible: that was the last chapter", true);
      return;
    }
  }
  else
  {
    if (next.chapter > 1)
    {
      next.chapter--;
    }
    else if (index > 0)
    {
      next.book = &canon[index - 1];
      next.chapter = next.book->chapters;
    }
    else
    {
      set_message("Bible: that was the first chapter", true);
      return;
    }
  }
  bible_open(next, true);
}

void Editor::bible_open_verse_of_the_day(bool announce)
{
  const long long day = today_day();
  if (bible_votd_day == day && !bible_votd_text.empty())
  {
    const std::vector<std::string> lines = {
        bible_votd_label + " (verse of the day)", "", bible_votd_text};
    bible_show_lines(bible_votd_label, lines, Bible::highlight(lines, Bible::Layout::Chapter));
    set_message("Bible: verse of the day - " + bible_votd_label, true);
    return;
  }
  const Bible::Reference chapter = Bible::chapter_for_day(day);
  bible_last = chapter;
  bible_has_last = true;
  bible_fetch(
      Bible::chapter_url(bible_api_base(), bible_translation(), chapter.book->id, chapter.chapter),
      BibleJob::DailyVerse,
      chapter,
      false,
      announce);
}

void Editor::bible_open_commentary(const Bible::Reference &reference)
{
  if (!reference.book)
  {
    set_message("Bible: no such book", true);
    return;
  }
  if (reference.chapter < 1 || reference.chapter > reference.book->chapters)
  {
    set_message("Bible: " + std::string(reference.book->name) + " has "
                    + std::to_string(reference.book->chapters)
                    + (reference.book->chapters == 1 ? " chapter" : " chapters"),
                true);
    return;
  }
  bible_last = reference;
  bible_has_last = true;
  bible_fetch(Bible::commentary_url(
                  bible_api_base(), bible_commentary(), reference.book->id, reference.chapter),
              BibleJob::Commentary,
              reference,
              false,
              true);
}

void Editor::bible_open_xref(const Bible::Reference &reference)
{
  if (!reference.book)
  {
    set_message("Bible: no such book", true);
    return;
  }
  bible_last = reference;
  bible_has_last = true;
  bible_fetch(Bible::xref_url(bible_api_base(), reference.book->id, reference.chapter),
              BibleJob::Xrefs,
              reference,
              false,
              true);
}

void Editor::bible_open_translations()
{
  bible_fetch(Bible::translations_url(bible_api_base()),
              BibleJob::Translations,
              Bible::Reference{},
              false,
              true);
}

bool Editor::bible_fetch(const std::string &url,
                         BibleJob job,
                         const Bible::Reference &reference,
                         bool pick_random,
                         bool announce)
{
  // One fetch at a time: the "last passage" and the verse-of-the-day state
  // only make sense for the newest request, so a second one waits rather than
  // racing.
  if (bible_request_running)
  {
    if (announce)
    {
      set_message("Bible: still waiting on the previous passage", true);
    }
    return false;
  }
  std::string cached;
  if (bible_cache_read(url, cached))
  {
    bible_deliver(job, reference, pick_random, announce, cached, true);
    return true;
  }
  if (!task_queue_)
  {
    set_message("Bible: no worker queue in this session", true);
    return false;
  }

  bible_request_running = true;
  if (announce)
  {
    const std::string label =
        reference.book ? Bible::reference_label(reference) : std::string("the Bible");
    set_transient_message("Bible: " + label + " ...", 4000);
  }
  task_queue_->submit_val<std::pair<std::string, bool>>(
      [url]()
      {
        std::string body;
        std::string error;
        const bool ok = http_get(url, kBibleTimeoutSeconds, body, error);
        return std::make_pair(ok ? body : error, ok);
      },
      [this, url, job, reference, pick_random, announce](std::pair<std::string, bool> result)
      {
        bible_request_running = false;
        if (!running)
        {
          return;
        }
        if (result.second)
        {
          bible_cache_write(url, result.first);
        }
        bible_deliver(job, reference, pick_random, announce, result.first, result.second);
      });
  return true;
}

void Editor::bible_deliver(BibleJob job,
                           const Bible::Reference &reference,
                           bool pick_random,
                           bool announce,
                           const std::string &body,
                           bool ok)
{
  bible_request_running = false;
  if (job == BibleJob::DailyVerse || job == BibleJob::DailyVerseText)
  {
    bible_votd_loading = false;
  }
  if (!running)
  {
    return;
  }
  if (!ok)
  {
    if (announce)
    {
      set_message("Bible: " + (body.empty() ? std::string("request failed") : body), true);
    }
    needs_redraw = true;
    return;
  }

  if (job == BibleJob::Translations)
  {
    std::vector<Bible::Translation> translations;
    if (!Bible::parse_translations(body, translations))
    {
      set_message("Bible: could not read the translation list", true);
      return;
    }
    const std::vector<std::string> lines =
        Bible::render_translations(translations, bible_render_width());
    bible_show_lines("translations", lines, Bible::highlight(lines, Bible::Layout::Translations));
    set_message("Bible: " + std::to_string(translations.size()) + " translations ("
                    + bible_translation() + " is current)",
                true);
    return;
  }

  if (job == BibleJob::DailyVerseText)
  {
    Bible::Chapter chapter;
    if (!Bible::parse_chapter(body, chapter))
    {
      return;
    }
    const long long day = today_day();
    const int verse = Bible::verse_for_day(day, (int)chapter.verse_numbers.size());
    const Bible::Reference shown = narrowed(reference, verse);
    const std::string *text = Bible::verse_text(chapter, verse);
    bible_votd_day = day;
    bible_votd_label = Bible::reference_label(shown);
    bible_votd_text = text ? *text : std::string();
    needs_redraw = true;
    return;
  }

  if (job == BibleJob::Xrefs)
  {
    std::vector<Bible::Xref> xrefs;
    if (!Bible::parse_xrefs(body, xrefs))
    {
      set_message("Bible: could not read the cross references", true);
      return;
    }
    const std::vector<std::string> lines =
        Bible::render_xrefs(xrefs, reference, bible_render_width());
    bible_show_lines(Bible::reference_label(reference) + " xref",
                     lines,
                     Bible::highlight(lines, Bible::Layout::Xrefs));
    set_message("Bible: " + std::to_string(xrefs.size()) + " cross references for "
                    + Bible::reference_label(reference),
                true);
    return;
  }

  Bible::Chapter chapter;
  const bool parsed = job == BibleJob::Commentary ? Bible::parse_commentary(body, chapter)
                                                  : Bible::parse_chapter(body, chapter);
  if (!parsed)
  {
    set_message("Bible: could not read the response", true);
    return;
  }

  Bible::Reference shown = reference;
  if (job == BibleJob::Chapter && pick_random)
  {
    // A chapter was drawn at random; now the verse inside it, from the same
    // seed, so the draw is one draw and not two.
    const int verse = Bible::verse_for_day(bible_random_seed, (int)chapter.verse_numbers.size());
    shown = narrowed(reference, verse);
    bible_last = shown;
    bible_has_last = true;
  }
  else if (job == BibleJob::DailyVerse)
  {
    const long long day = today_day();
    const int verse = Bible::verse_for_day(day, (int)chapter.verse_numbers.size());
    shown = narrowed(reference, verse);
    bible_last = shown;
    bible_has_last = true;
    const std::string *text = Bible::verse_text(chapter, verse);
    bible_votd_day = day;
    bible_votd_label = Bible::reference_label(shown);
    bible_votd_text = text ? *text : std::string();
  }

  Bible::RenderOptions options;
  options.width = bible_render_width();
  options.context = bible_context_verses();
  const std::vector<std::string> lines = Bible::render_chapter(chapter, shown, options);
  const std::string label = Bible::reference_label(shown);
  bible_show_lines(job == BibleJob::Commentary ? label + " commentary" : label,
                   lines,
                   Bible::highlight(lines, Bible::Layout::Chapter));
  if (announce)
  {
    set_message("Bible: " + Bible::reference_label(shown) + " - "
                    + (chapter.translation_name.empty() ? chapter.translation_id
                                                        : chapter.translation_name),
                true);
  }
  needs_redraw = true;
}

int Editor::bible_view_buffer(const std::string &label)
{
  // The passage rides in the name, so the tab strip reads `[Bible] John 3:16`
  // instead of a pile of identical `[Bible]` tabs -- and the prefix is still
  // what is_rendered_view_path and the test hooks key on.
  std::string name = "[Bible]";
  if (!label.empty())
  {
    name += " " + label;
  }
  for (int i = 0; i < (int)buffers.size(); i++)
  {
    if (buffers[(size_t)i].filepath.rfind("[Bible]", 0) == 0)
    {
      buffers[(size_t)i].filepath = name;
      // A passage is something to read, not something to write: the tab is a
      // view, and every edit path refuses through edit_allowed().
      buffers[(size_t)i].read_only = true;
      return i;
    }
  }
  create_new_buffer();
  get_buffer().filepath = name;
  get_buffer().read_only = true;
  return current_buffer;
}

void Editor::bible_show_lines(const std::string &label,
                              const std::vector<std::string> &lines,
                              const std::vector<Bible::Span> &spans)
{
  const int index = bible_view_buffer(label);
  if (index < 0 || index >= (int)buffers.size())
  {
    return;
  }
  pane_show_buffer(index);
  reveal_tab_for_buffer(index);
  FileBuffer &buf = get_buffer();
  buf.replace_lines(0, (int)buf.line_count(), lines);
  buf.clear_undo_history();
  buf.cursor = {0, 0};
  buf.preferred_x = 0;
  buf.selection = {{0, 0}, {0, 0}, false};
  buf.scroll_offset = 0;
  buf.scroll_x = 0;
  buf.modified = false;

  // The colours ride on anchored decorations (the same mechanism the Lua
  // diagnostics use). The text was just replaced wholesale, so the old span set
  // is dropped and the base is dropped with it: the next render adopts the new
  // text, and the spans below are already in its coordinates.
  buf.decorations.clear();
  buf.decoration_base_valid = false;
  buf.decoration_dirty = false;
  for (const Bible::Span &span : spans)
  {
    if (span.line < 0 || span.line >= (int)lines.size() || span.end <= span.start || span.start < 0
        || span.end > (int)lines[(size_t)span.line].size())
    {
      continue;
    }
    Decoration decoration;
    decoration.id = ++bible_decoration_id;
    decoration.row = span.line;
    decoration.col = span.start;
    decoration.width = span.end - span.start;
    decoration.hl = span.group;
    decoration.priority = 1;
    decoration_insert(buf, decoration);
  }
  needs_redraw = true;
}

std::string Editor::bible_verse_of_the_day()
{
  const long long day = today_day();
  if (bible_votd_day == day)
  {
    return bible_votd_text;
  }
  if (bible_votd_loading)
  {
    return {};
  }
  bible_votd_loading = true;
  const Bible::Reference chapter = Bible::chapter_for_day(day);
  if (!bible_fetch(Bible::chapter_url(
                       bible_api_base(), bible_translation(), chapter.book->id, chapter.chapter),
                   BibleJob::DailyVerseText,
                   chapter,
                   false,
                   false))
  {
    bible_votd_loading = false;
  }
  // A cache hit is delivered synchronously, so the day may already be here.
  return bible_votd_day == day ? bible_votd_text : std::string();
}
