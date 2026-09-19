#ifndef JOT_EDITOR_SEARCH_CONTROLLER_H
#define JOT_EDITOR_SEARCH_CONTROLLER_H

#include "jot/model/buffer.h" // Cursor
#include "jot/model/search.h" // SearchMatch
#include <string>
#include <vector>

class Editor;

// The find/replace panel: the query, the flags, the match list, and the two
// ways to walk it (the panel's own replace-all and the editor's find next /
// prev).
//
// This is the first of the editor's collaborators. It used to be a block of
// EditorState members plus a dozen Editor methods spread across three files;
// the state and the behaviour now live together here, and Editor keeps one of
// these. The controller reaches the shared editor state (the buffers, the UI
// grid, the message line) through the Editor it was built with -- the area is
// self-contained enough that the coupling is a handful of calls per method
// rather than a coupling of lifetimes.
class SearchController
{
public:
  explicit SearchController(Editor &editor) : editor_(editor) {}

  // --- What the panel is showing ---
  // The match list the buffer renderer highlights and the Lua surface reports;
  // `selected_index()` is -1 when nothing is selected yet.
  bool visible() const { return visible_; }
  const std::string &query() const { return query_; }
  const std::string &replace_text() const { return replace_text_; }
  const std::vector<SearchMatch> &results() const { return results_; }
  int selected_index() const { return result_index_; }
  bool case_sensitive() const { return case_sensitive_; }
  bool whole_word() const { return whole_word_; }
  bool regex() const { return regex_; }
  bool replace_visible() const { return replace_visible_; }
  bool focus_replace() const { return focus_replace_; }
  bool scoped_to_selection() const { return scoped_to_selection_; }

  // --- Commands, input and paint ---
  void open();
  void toggle();
  // Hides the panel and drops the scope it was limited to (closing floating UI
  // or a file switch must not leave the next search scoped to a stale range).
  void close();
  bool open_scoped_replace_from_selection();
  void perform();
  void find_next();
  void find_prev();
  bool replace_current();
  bool replace_all();
  void clear_scope();
  void set_case_sensitive(bool on) { case_sensitive_ = on; }
  void clear_results();
  // Construction / session reset: every field back to its documented default.
  void reset();
  void handle_panel_input(int ch, bool is_ctrl, bool is_shift);
  bool handle_mouse(int x, int y, bool is_click);
  void render_panel();
  void place_cursor();

private:
  Editor &editor_;

  bool visible_ = false;
  std::string query_;
  std::string replace_text_;
  std::vector<SearchMatch> results_;
  int result_index_ = 0;
  bool case_sensitive_ = false;
  bool whole_word_ = false;
  bool regex_ = false;
  bool replace_visible_ = false;
  bool focus_replace_ = false;
  bool scoped_to_selection_ = false;
  Cursor scope_start_{0, 0};
  Cursor scope_end_{0, 0};
};

#endif
