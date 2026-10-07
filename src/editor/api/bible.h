// ---------------------------------------------------------------------------
// The Bible reader (bible.helloao.org, `:bible`)
// ---------------------------------------------------------------------------
//
// `:bible John 3:16` opens the passage in a `[Bible]` scratch tab that later
// looks re-use. Around it: `:bible` (or `:bible votd`) for the verse of the
// day, `:bible random` for a random verse with its context, `:bible next` /
// `:bible prev` to walk chapter by chapter, `:bible commentary [ref]` for a
// commentary chapter, `:bible xref [ref]` for the open-cross-ref links, and
// `:bible translations` for the catalog.
//
// Every fetch runs on the worker queue (like the REST client) and is cached
// under JOT_CACHE_HOME, so a passage seen once costs one request ever. Which
// translation and commentary to use comes from `bible_translation` /
// `bible_commentary`; `bible_api_base` points the whole thing at a fixture
// server for tests.
//
// A fragment of the Editor class body, included by src/editor/editor.h. It is
// not a standalone header: no include guard, no includes, and the members
// sit in class scope exactly as if they were written in editor.h.
private:
static constexpr int kBibleTimeoutSeconds = 20;

// What a fetched body is for; the URL scheme alone cannot tell a chapter
// from a commentary, and each renders differently.
enum class BibleJob
{
  Chapter,
  DailyVerse,
  // The same day's chapter, fetched only to fill the home screen's verse row:
  // it stores the verse and repaints, but never steals the pane with the
  // `[Bible]` tab the way the `:bible votd` command does.
  DailyVerseText,
  Commentary,
  Xrefs,
  Translations
};

void bible_run(const std::string &arg);
// Opens a passage: validates the chapter against the canon, remembers it as
// the last reference and fetches it.
void bible_open(const Bible::Reference &reference, bool announce);
void bible_open_random();
void bible_step(bool forward);
void bible_open_verse_of_the_day(bool announce);
void bible_open_commentary(const Bible::Reference &reference);
void bible_open_xref(const Bible::Reference &reference);
void bible_open_translations();
// The worker-queue fetch behind every command above: a cache hit is shown
// without touching the network. False when it refused (one fetch is already
// in flight), so a caller that armed a pending flag can put it back.
bool bible_fetch(const std::string &url,
                 BibleJob job,
                 const Bible::Reference &reference,
                 bool pick_random,
                 bool announce);
void bible_deliver(BibleJob job,
                   const Bible::Reference &reference,
                   bool pick_random,
                   bool announce,
                   const std::string &body,
                   bool ok);
// The `[Bible]` scratch tab, created on first use and re-used from then on.
// The label is part of its name (`[Bible] John 3:16`), so the tab strip says
// which passage is on screen.
int bible_view_buffer(const std::string &label);
// Paints the passage and its colours: the spans come from
// Bible::highlight(), and each one becomes an anchored decoration over the
// newly written text.
void bible_show_lines(const std::string &label,
                      const std::vector<std::string> &lines,
                      const std::vector<Bible::Span> &spans);
// Cached verse-of-the-day lookup for the home screen: starts a fetch when the
// day has not been seen yet, and returns the text when it has.
std::string bible_verse_of_the_day();

std::string bible_translation();
std::string bible_commentary();
std::string bible_api_base();
int bible_render_width();
int bible_context_verses();
