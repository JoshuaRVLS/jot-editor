#ifndef JOT_STATE_BIBLE_STATE_H
#define JOT_STATE_BIBLE_STATE_H

#include "extras/bible.h"
#include <cstdint>
#include <string>

// The Bible reader's session (see extras/bible.h for the pure half and
// extras/bible_client.cpp for the runs): the passage `:bible` showed last, so
// `:bible next` / `:bible prev` know where to go on, and whether one fetch is
// still in flight. The passage itself lives in an ordinary `[Bible]` scratch
// buffer -- closing it needs no cleanup beyond clearing the last reference.
struct BibleState
{
  Bible::Reference bible_last;
  bool bible_has_last = false;
  bool bible_request_running = false;
  // The verse of the day, kept once fetched so the home screen shows it for
  // free and the day's one request is the day's only one. `bible_votd_day` is
  // the unix day the text belongs to.
  long long bible_votd_day = -1;
  std::string bible_votd_label;
  std::string bible_votd_text;
  bool bible_votd_loading = false;
  // A random passage's seed, so the chapter and the verse inside it come from
  // the same draw.
  long long bible_random_seed = 0;
  // The next anchored-decoration id for the passage tab's colours. Ids only
  // have to be unique inside one buffer, and the tab drops its whole span set
  // on every look, so one counter for the session is enough.
  std::uint64_t bible_decoration_id = 0;
};

#endif
