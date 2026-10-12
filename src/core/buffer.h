#ifndef JOT_MODEL_BUFFER_H
#define JOT_MODEL_BUFFER_H

#include "core/decorations.h"
#include "core/fold_ranges.h"
#include "core/syntax.h"
#include "line_provider.h"
#include "syntax/color/color_definitions.h"
#include "syntax/index.h" // SymbolMatch (the winbar's symbol cache)
#include "text_features.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef JOT_TREESITTER
struct TSParser;
struct TSTree;
struct TSQuery;
#endif

namespace
{
  constexpr std::uintmax_t kFileSizeLazyThreshold = 10ULL * 1024ULL * 1024ULL;
  constexpr int kDeltaWindowHalfSize = 50;
  constexpr int kMaxFullSnapshotLines = 5000;
  // Undo/redo history budgets, per stack and per buffer. The state count alone
  // is not a memory bound: a full snapshot carries one line pointer per line of
  // the buffer (~64 KB for a 4000-line file) plus a fresh copy of every line its
  // command changed, so 500 states of a mid-size buffer is tens of megabytes
  // per buffer, and a shot that replaces the whole text (a formatter, a
  // plugin's set_text, :%s) makes every line fresh on top of that. The history
  // is therefore trimmed by accounted bytes as well, never dropping below
  // kMinUndoStates so undo stays useful however large the edits were.
  constexpr std::size_t kMaxUndoHistory = 500;
  constexpr std::size_t kUndoHistoryBytes = 2u * 1024u * 1024u;
  constexpr std::size_t kMinUndoStates = 16;
} // namespace

// The clock FileBuffer's own stamps are read on: the occurrence highlight's
// typing hold measures the pause since the last edit with it. In its own
// namespace (like lsp_internal::now_ms) because every .cpp here keeps a
// private steady_now_ms() and the two must be able to coexist.
namespace model_clock
{
  inline long long now_ms()
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
} // namespace model_clock

struct Cursor
{
  int x, y;
  bool operator==(const Cursor &other) const
  {
    return x == other.x && y == other.y;
  }
};

struct Selection
{
  Cursor start;
  Cursor end;
  bool active;
};

struct GlobalMark
{
  std::string filepath;
  int line = 0;
  int col = 0;
};

// One line held by an undo snapshot. A snapshot shares the lines it did not
// change with the snapshot before it, so typing a character into a 2000-line
// file puts that one line on the stack instead of a copy of every line in the
// file (~190 KB per keystroke, and the history keeps 500 of them).
using SnapshotLine = std::shared_ptr<const std::string>;

struct State
{
  bool full_snapshot = false;
  int start_line = 0;
  std::vector<SnapshotLine> old_lines;
  int old_total_lines = 0;

  Cursor cursor;
  int preferred_x;
  Selection selection;
  std::vector<Selection> extra_carets;
  int scroll_offset;
  int scroll_x;
  bool modified;
  bool is_placeholder;
};

struct FileBuffer
{
  std::vector<std::string> lines;
  std::unique_ptr<LineProvider> lazy_provider;

  Cursor cursor;
  int preferred_x; // desired column for vertical movement
  Selection selection;
  std::vector<Selection> extra_carets;
  int scroll_offset;
  int scroll_x;
  std::string filepath;
  bool modified;
  bool is_preview = false;
  bool is_placeholder = false;
  // A view the editor paints but the user does not type into: the Bible
  // reader's `[Bible]` tab (extras/bible_client.cpp) marks itself read-only, and
  // every interactive edit path opens with Editor::edit_allowed(). The flag
  // rides on the buffer, so a tab move or a pane switch keeps it, and a save
  // is refused for good measure (is_rendered_view_path already covers that).
  bool read_only = false;
  // Identity for the tab strip: 0 until the tab order first sees this buffer,
  // then a number that stays with it for the buffer's life. Buffer *indices*
  // shift whenever one closes to the left (buffers is a vector that is erased
  // from the middle), so the strip's order, its pinned set and its jump-to-
  // buffer letters all key on this instead (see ui/tab_order.h).
  long long tab_uid = 0;
  std::stack<State> undo_stack;
  std::stack<State> redo_stack;
  // Bytes the two histories hold, maintained by undo.cpp as states are pushed,
  // popped and trimmed (see kUndoHistoryBytes). Counts each state's line
  // pointers plus the text of the lines it created; lines still shared by a
  // newer state stay counted until the last holder goes, so the total is an
  // upper bound on what the stacks retain.
  std::size_t undo_stack_bytes = 0;
  std::size_t redo_stack_bytes = 0;

  // Drops the recorded history: the two stacks and the byte accounting that
  // bounds them, which every path that resets the stacks has to clear with
  // them or the next push is charged against a budget that was never freed.
  void clear_undo_history()
  {
    undo_stack = std::stack<State>();
    redo_stack = std::stack<State>();
    undo_stack_bytes = 0;
    redo_stack_bytes = 0;
  }
  std::set<int> bookmarks;
  // Range of the last multi-line comment toggle (both comment and uncomment
  // directions). A no-selection Ctrl+/ with the cursor inside this range
  // re-toggles the same lines, so select -> Ctrl+/ -> Ctrl+/ still cycles
  // after the selection is cleared.
  int last_comment_start = -1;
  int last_comment_end = -1;
  std::unordered_map<std::string, std::string> lua_vars;
  std::map<char, Cursor> marks; // buffer-local marks ('a'-'z')
  // Anchored decorations, sorted by (row, col, priority desc, id). The
  // rebase machinery snapshots the full text before every edit and lazily
  // shifts decorations through the single pending edit window (see
  // decorations.cpp); decoration_dirty tracks whether a pending edit exists
  // so the renderer pays nothing between edits. decoration_base_valid goes
  // false when the text is replaced wholesale (file load): decorations are
  // then cleared and consumers re-apply them (e.g. on DiagnosticChanged).
  std::vector<Decoration> decorations;
  std::string decoration_base;
  bool decoration_base_valid = false;
  bool decoration_dirty = false;
  std::vector<Diagnostic> diagnostics;
  // Per-line worst diagnostic severity (0 = none, 1 = error .. 4 = hint)
  // built lazily by the renderer over buf.diagnostics. The gutter asks for
  // the severity of every visible row on every frame; a dense per-line
  // array answers in O(1) where scanning the diagnostic list would cost
  // O(diagnostics) per row. diag_severity_by_line covers lines up to
  // diag_severity_built_upto; diag_wide_spans holds the indices of
  // diagnostics whose range is too wide to expand densely, checked
  // directly on lookup. diag_severity_dirty is set whenever the list is
  // replaced (LSP publish, reload); mark_edited does not set it because
  // the diagnostic set is intentionally not re-offset on local edits, so
  // the cached answers stay exactly as accurate as the list they index.
  std::vector<unsigned char> diag_severity_by_line;
  int diag_severity_built_upto = -1;
  bool diag_severity_dirty = true;
  std::vector<int> diag_wide_spans;
  // The fold ranges and the index prepared from them. Every writer bumps the
  // revision the index is validated against, so no caller can leave it stale;
  // see FoldRanges in this header.
  FoldRanges fold_ranges;
  bool folds_dirty = true;
  // The document-symbol index the breadcrumb winbar cuts its symbol crumbs from
  // (syntax/index.h). Validated by edit_generation the way the fold index
  // is, and rebuilt by the winbar at most every ~250 ms while the buffer is
  // being typed in: a symbol that appears one keystroke late beats a full-buffer
  // scan per keystroke, and the row is a breadcrumb, not an outline.
  std::vector<SymbolMatch> symbol_cache;
  std::uint64_t symbol_cache_generation = 0;
  bool symbol_cache_valid = false;
  long long symbol_cache_ms = 0;
  // Incremental absolute bracket-depth prefix used by the rainbow-bracket
  // renderer. bracket_depth_prefix[i] holds the bracket depth at the start
  // of buffer line i (the floored raw +/- walk over lines [0, i)); entry i
  // is valid for i <= bracket_depth_prefix_upto (when non-empty the vector
  // has size upto + 1, with entry 0 == 0). mark_edited(anchor) drops every
  // entry past the edited line, so the next render re-extends lazily only
  // over the affected region. Without this cache the renderer reseeded depth
  // from a sliding ~500-line window starting at depth 0, so every bracket's
  // color shifted as enclosing brackets entered or left that window while
  // scrolling.
  std::vector<int> bracket_depth_prefix;
  int bracket_depth_prefix_upto = 0;
  // Lazy prefix sums of line sizes (+1 for each newline), used to resolve the
  // start byte of a line in O(1) for tree-sitter byte-range queries. Cleared
  // on every edit (see mark_edited) and rebuilt incrementally on demand.
  std::vector<uint32_t> ts_line_offsets;
  std::string syntax_cache_extension;
  std::size_t syntax_cache_line_count = 0;
  // Memoized answer of the "is this .h really C++?" probe (see
  // Editor::tree_sitter_extension_for_buffer). Resolving it stats the
  // filesystem for a sibling translation unit and samples the first 32KB of
  // the buffer, and it used to run once per rendered row per frame. An empty
  // result means "not computed yet"; `ts_extension_probe_path` pins the answer
  // to the file it was computed for, so save-as / reload recompute it without
  // every path assignment having to remember to invalidate.
  std::string ts_extension_probe;
  std::string ts_extension_probe_path;
  // Memoized git_file_status key (absolute, lexically-normal path) for this
  // buffer. Resolving it runs std::filesystem::absolute + lexically_normal, and
  // the tab strip asks for it once per tab per frame. Pinned to the path it was
  // computed from so save-as / reload recompute it without a separate hook.
  std::string git_status_key;
  std::string git_status_key_path;
  // Approximate bytes held by `syntax_cache`'s per-line colour vectors, used
  // to bound the cache's growth (see get_line_syntax_colors).
  std::size_t syntax_cache_bytes = 0;
  // Bumped by every content mutation (mark_edited); lets render-side memos
  // reuse work across frames without any edit path knowing about them.
  std::uint64_t edit_generation = 0;
  // Steady-clock ms of the last content mutation (mark_edited): the occurrence
  // highlight stays down for word_highlight_delay_ms past it, so the tint does
  // not flicker along with the typing that keeps restarting the clock (VS
  // Code's occurrencesHighlightDelay). Cursor moves do not touch it, so landing
  // on a word lights it at once. 0 = nothing edited yet.
  long long word_highlight_last_edit_ms = 0;
  // Memoized bracket-pair guide for the caret's row (see
  // buffer_internal::build_active_bracket_guide). Finding the partner bracket
  // walks up to kBracketMatchSearchLimitLines lines and the builder tries
  // three caret columns, so an unmatched bracket under the caret -- the normal
  // state while typing a block -- used to rescan thousands of lines on every
  // frame. Keyed on the edit generation and the caret, so it is recomputed
  // exactly when either changes.
  struct BracketGuideMemo
  {
    bool valid = false;
    std::uint64_t generation = 0;
    int cursor_x = -1;
    int cursor_y = -1;
    // The reported column is a visual column, so a change to the tab width has
    // to invalidate the memo as well.
    int tab_size = -1;
    // Result, mirroring buffer_internal::ActiveBracketGuide.
    bool active = false;
    int column = 0;
    int start_line = 0;
    int end_line = 0;
    // The pair the caret is on, mirrored from buffer_internal::BracketPairMatch
    // the same way: the bracket-match highlight reads it back out of this memo.
    bool pair_found = false;
    int pair_open_line = -1;
    int pair_open_col = -1;
    int pair_close_line = -1;
    int pair_close_col = -1;
  } bracket_guide_memo;
  std::unordered_map<int, SyntaxLineCache> syntax_cache;
  // Colour-preview variable definitions (--name: value, $name: value) for this
  // buffer, and their version. Rebuilt lazily when the buffer is edited; the
  // renderer's line cache keys on the version so a resolved var(--x) is never
  // served from a scan that predates the edit that changed its definition.
  jot_color::Definitions color_defs;
  std::uint64_t color_defs_version = 0;
  bool color_defs_dirty = true;
  SyntaxEngine syntax_engine = SYNTAX_ENGINE_UNKNOWN;
  std::string syntax_language_label;

  // Called after any content mutation: fold ranges, tree-sitter byte
  // offsets and the bracket-depth prefix become stale and must be rebuilt
  // lazily on next use. `anchor_line` is the first buffer line whose
  // content changed (cursor/selection line for edits, replace start for
  // bulk replacements, 0 when unknown). Depth entries at/after it are
  // dropped; entries before it stay valid because depth at the start of a
  // line only depends on earlier lines.
  void mark_edited(int anchor_line = 0)
  {
    folds_dirty = true;
    edit_generation++;
    // The typing hold on the occurrence highlight restarts here: every edit
    // path funnels through this one call, and typing is what the hold waits
    // out (see word_highlight_last_edit_ms above).
    word_highlight_last_edit_ms = model_clock::now_ms();
    ts_line_offsets.clear();
    // A header that does not look like C++ yet may be getting filled in, so
    // re-probe it on the next highlight. Once the answer is "C++" it is
    // stable, and the cache stays warm across edits.
    if (ts_extension_probe != ".cpp")
    {
      ts_extension_probe.clear();
    }
    // A definition line may have changed, so the colour preview's variable index
    // has to be rebuilt before it is next consulted.
    color_defs_dirty = true;
    if (anchor_line < 0)
    {
      anchor_line = 0;
    }
    if (anchor_line < bracket_depth_prefix_upto)
    {
      bracket_depth_prefix_upto = anchor_line;
      if ((int)bracket_depth_prefix.size() > bracket_depth_prefix_upto + 1)
      {
        bracket_depth_prefix.resize((size_t)bracket_depth_prefix_upto + 1);
      }
    }
  }

#ifdef JOT_TREESITTER
  TSParser *ts_parser = nullptr;
  TSTree *ts_tree = nullptr;
  // The highlight query the per-line syntax cache was computed with. The
  // deferred boot-time query compile installs a (possibly different) query
  // shortly after first paint, so the cache must re-run when this pointer
  // changes - otherwise stale colors stick until the next cache invalidation
  // (edit or save).
  TSQuery *syntax_query = nullptr;
  // True while an initial whole-file parse runs on the background worker
  // (large files only). While pending the buffer has no parser/tree, so the
  // renderer shows plain text and edits simply leave the pending result to be
  // discarded or repaired at install time (see install_finished_parses).
  bool ts_parse_pending = false;
  // Set when a background parse failed to load a parser; subsequent inits
  // take the synchronous path (which handles a missing parser gracefully)
  // instead of re-queuing forever. Cleared once a sync parse succeeds.
  bool ts_async_parse_failed = false;
  std::string ts_language_id;
  // Incremental-parse bookkeeping. ts_tree matches the buffer text exactly
  // while ts_tree_in_sync is true. On an edit (save_state/undo/redo) the text
  // the tree was parsed from is snapshotted into ts_edit_base and
  // ts_tree_in_sync is cleared; the next highlight rebuild diffs ts_edit_base
  // against the current text, applies one bounded ts_tree_edit and reparses,
  // so per-keystroke cost tracks the edited region instead of a whole-file
  // parse. Fields are only touched on the main thread (edit + render paths).
  bool ts_tree_in_sync = true;
  bool ts_edit_base_valid = false;
  std::string ts_edit_base;
  // Recency stamp of the last painted frame this buffer was drawn on. The
  // renderer bumps one counter per pane (Editor::render_pane) and
  // Editor::evict_stale_syntax_trees drops the trees of buffers that are not
  // recent, so a source file left in the background does not keep its parse
  // tree -- ~1.3-3.5 KB of CST per line of C++, see kMaxCachedSyntaxTrees --
  // alive just because it is open.
  std::uint64_t ts_last_use_seq = 0;

  // Drops this buffer's parse tree and parser. They are raw pointers into
  // tree-sitter memory that nothing else owns, so every path that discards a
  // buffer -- close, workspace switch, editor teardown -- has to go through
  // here; skipping it is a silent leak of the whole tree (tens of MB for a
  // large file). Defined in integrations/syntax.cpp, which has the
  // tree-sitter headers this header only forward-declares. Bookkeeping fields
  // are left to the caller, which knows whether the buffer is being
  // re-initialised or dropped.
  void release_syntax();
#endif

  // Line accessor methods
  bool is_lazy() const
  {
    return lazy_provider != nullptr;
  }

  const std::string &line(int n) const
  {
    if (lazy_provider)
      return lazy_provider->get_line(n);
    if (n < 0 || n >= (int)lines.size())
    {
      static const std::string empty;
      return empty;
    }
    return lines[n];
  }

  std::string &line_mut(int n)
  {
    if (lazy_provider)
      materialize();
    if (n < 0 || n >= (int)lines.size())
    {
      if (lines.empty())
        lines.push_back("");
      return lines[0];
    }
    return lines[n];
  }

  size_t line_count() const
  {
    if (lazy_provider)
      return lazy_provider->line_count();
    return lines.size();
  }

  bool has_lines() const
  {
    return line_count() > 0;
  }

  char char_at(int line_idx, int col) const
  {
    if (lazy_provider)
      return lazy_provider->get_char(line_idx, col);
    if (line_idx < 0 || line_idx >= (int)lines.size())
      return '\0';
    if (col < 0 || col >= (int)lines[line_idx].size())
      return '\0';
    return lines[line_idx][col];
  }

  void scroll_hint(int center_line)
  {
    if (lazy_provider)
      lazy_provider->scroll_hint(center_line);
  }

  void materialize();

  void replace_lines(int start, int count, const std::vector<std::string> &new_lines)
  {
    mark_edited(start);
    if (lazy_provider)
    {
      lazy_provider->replace_lines(start, count, new_lines);
    }
    else
    {
      if (count < 0)
        count = 0;
      if (start < 0)
        start = 0;
      if (start > (int)lines.size())
        start = (int)lines.size();
      if (start + count > (int)lines.size())
      {
        count = (int)lines.size() - start;
      }
      if (count > 0)
      {
        lines.erase(lines.begin() + start, lines.begin() + start + count);
      }
      if (!new_lines.empty())
      {
        lines.insert(lines.begin() + start, new_lines.begin(), new_lines.end());
      }
    }
  }
};

// A buffer named for something it renders rather than a file it holds: the REST
// response tab (app/rest_client.cpp), the Bible reader's passage tab
// (extras/bible_client.cpp), the AI chat (features/ai/chat.lua) and the
// compilation buffer (:compile, workspace/tasks.cpp). There is nothing behind
// the name, so a save is refused and a workspace session does not reopen one.
inline bool is_rendered_view_path(const std::string &path)
{
  return path.rfind("[Response]", 0) == 0 || path.rfind("[Bible]", 0) == 0
         || path.rfind("[Chat]", 0) == 0 || path.rfind("[Compile]", 0) == 0;
}

// The compilation log a `:compile` run streams into (workspace/tasks.cpp). It
// keeps a buffer of its own -- that is what the incremental parse and the row
// decorations read -- and a pane to show itself in, but it is nobody's file:
// no tab strip lists it and the tab keys walk past it, so a build's output is
// never one of the opened tabs.
inline bool is_compile_log_path(const std::string &path)
{
  return path.rfind("[Compile]", 0) == 0;
}

#endif
