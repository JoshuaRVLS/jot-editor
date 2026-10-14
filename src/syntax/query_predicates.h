#ifndef QUERY_PREDICATES_H
#define QUERY_PREDICATES_H

// Query predicates and directives: the `(#match? @x "regex")` half of the query
// language, which decides whether a pattern's match counts at all.
//
// A tree-sitter pattern with a predicate compiles and then matches everything,
// because the C library hands the predicate back to the editor to evaluate: the
// `(#match? ...)` forms are steps returned alongside the captures, not something
// the query engine applies. An editor that ignores them highlights every node
// the pattern could match, which is how a query written for one grammar ends up
// painting whole buffers.
//
// The same steps carry the directives, which change what a capture means rather
// than whether it matched:
//
//   * `(#set! priority 110)` raises every capture in the pattern, and
//     `(#set! @capture priority 110)` only that capture, over the priority the
//     capture name itself carries (render/syntax.h). Directives naming anything
//     else (conceal, injection.*) are for the layers that consume them and are
//     ignored here.
//   * `(#offset! @capture 0 1 0 -1)` moves a capture's range, which is how a
//     query keeps a delimiter out of a capture it hands to something else.
//
// A predicate this file does not know is treated as satisfied rather than as a
// failure: query files arrive from grammar repositories and from other editors'
// plugins, and refusing the whole language over a predicate we never heard of
// would trade a small inaccuracy for a dead language. An unknown *name* is not
// the same as a falsifiable claim.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace jot_query
{
#ifdef JOT_TREESITTER
  // Source text a predicate compares against, read by the tree points the match
  // already carries. A caller with no text in hand (a query run over a tree the
  // Lua API parsed from a string that was not kept) leaves it empty, and the
  // predicates that need text are then treated as satisfied; the ones that read
  // only the tree (#kind-eq?, #has-parent?, #has-ancestor?) still decide.
  using TextReader = std::function<std::string(TSPoint start, TSPoint end)>;

  // True when every predicate the pattern carries holds for this match. A
  // pattern without predicates is always satisfied.
  bool match_satisfies(const TSQuery *query, const TSQueryMatch &match, const TextReader &text);

  // What the directives do to one capture of a match.
  struct CaptureDirectives
  {
    // #offset! is relative, in rows and columns, and applies to the capture's
    // range: `(#offset! @content 0 1 0 -1)` drops the delimiter on each side.
    bool has_offset = false;
    int start_row = 0;
    int start_column = 0;
    int end_row = 0;
    int end_column = 0;
    // #set! priority, when the directive names this capture or the pattern.
    bool has_priority = false;
    int priority = 0;
  };

  // The directives that apply to one capture of a match: its own by name, plus
  // the pattern-wide ones. `capture_name` is the captured name from
  // ts_query_capture_name_for_id.
  CaptureDirectives capture_directives(const TSQuery *query,
                                       const TSQueryMatch &match,
                                       const std::string &capture_name);

  // Whether the pattern carries predicates at all, so a caller that would have
  // to assemble source text can skip that when there is nothing to decide.
  bool pattern_has_predicates(const TSQuery *query, uint32_t pattern_index);

  // Whether any pattern of the query carries a predicate or a directive step.
  // Most query files carry neither, and a highlighter that asks this once per
  // line skips the per-match predicate and per-capture directive lookups (and
  // the source text they compare against) entirely for those files.
  bool query_has_conditions(const TSQuery *query);

  // What a pattern says about injecting another language into its content:
  //
  //   (#set! injection.language "sql")   the language to parse it with
  //
  // The language can also come from an `@injection.language` capture, whose
  // node text names it; that one is read from the match, not from here.
  struct InjectionDirectives
  {
    std::string language;
  };

  InjectionDirectives injection_directives(const TSQuery *query, uint32_t pattern_index);

  // The `#set!` properties an `indents.scm` pattern carries, which change how a
  // node's capture is applied:
  //
  //   (#set! indent.immediate 1)          a one-line node still opens a block
  //   (#set! indent.start_at_same_line 1) a node starting on the line being
  //                                       indented still opens one
  struct IndentProperties
  {
    bool immediate = false;
    bool start_at_same_line = false;
  };

  IndentProperties indent_properties(const TSQuery *query, uint32_t pattern_index);
#endif // JOT_TREESITTER

  // Lua patterns, for the `#lua-match?` predicate: anchors (^ $), character
  // classes (%a %d %l %u %s %w %x %p %c and their upper-case complements),
  // sets ([abc], [^abc], ranges), the magic set characters, and the four
  // quantifiers (* + - ?). This is the same dialect Neovim's queries are
  // written in, which is what makes their files usable here.
  bool lua_pattern_match(const std::string &pattern, const std::string &text);

  // The modeline a query file may open with, which is what a user query file
  // says about how it relates to the one it would otherwise replace.
  enum class Modeline
  {
    None,
    Extends,  // `;; extends`: add to the base query instead of replacing it
    Inherits, // `;; inherits: a,b,(c)`: pull in those languages' queries
  };

  struct ModelineResult
  {
    Modeline kind = Modeline::None;
    // The languages named by `;; inherits:`, in order. A parenthesised one is
    // optional: it is skipped when its query is not available.
    std::vector<std::string> languages;
    std::vector<bool> optional; // one per language
  };

  ModelineResult parse_modeline(const std::string &source);
} // namespace jot_query

#endif // QUERY_PREDICATES_H
