#include "base/string_util.h"
#include "core/editor.h"
#include "lsp_attach_data.h"
#include "syntax/manager.h"
#include "syntax/query_predicates.h"
#include <algorithm>
#include <filesystem>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace
{

  // Lines no longer than this are highlighted whole and cached; only longer
  // lines use windowed highlighting bounded by the visible width.
  constexpr int kFullHighlightLineBytes = 4096;

  // Best-effort human label for a filetype the LSP catalog knows but that has
  // no tree-sitter grammar or regex rules (e.g. .astro, .tfvars). Kept next to
  // the generated attach tables so detection stays in sync with the catalog:
  // language ids first, then the attach server with its "-language-server" /
  // "-lsp" style suffixes dropped for display.
  std::string catalog_filetype_label(const std::string &raw_extension)
  {
    if (raw_extension.empty() || raw_extension[0] != '.')
    {
      return "";
    }
    const std::string key = raw_extension.substr(1);
    for (const auto &entry : kLspLangIdTable)
    {
      if (key == entry.ext)
      {
        return entry.langid;
      }
    }
    for (const auto &entry : kLspAttachTable)
    {
      if (key != entry.ext)
      {
        continue;
      }
      std::string name = entry.server;
      static const char *const suffixes[] = {
          "-language-server", "-langserver", "-language-lsp", "-lsp", "-ls"};
      bool stripped = true;
      while (stripped)
      {
        stripped = false;
        for (const char *suffix : suffixes)
        {
          if (string_util::ends_with(name, suffix))
          {
            name.resize(name.size() - std::string(suffix).size());
            stripped = true;
          }
        }
      }
      if (!name.empty())
      {
        return name;
      }
    }
    return "";
  }

  // Upper bound on the per-buffer line-colour cache (see get_line_syntax_colors).
  // One entry costs roughly one (int, int) pair per byte of its line, so this
  // is a few hundred thousand highlighted lines' worth of colour data; past it
  // the cache is dropped wholesale and refilled from the visible viewport.
  constexpr std::size_t kSyntaxCacheByteBudget = 8u * 1024u * 1024u;

  // Hands the memory a dropped tree just returned back to the OS. Freeing a
  // tree gives its chunks back to malloc, but glibc keeps them in its arenas,
  // and the whole-file parses that built those trees ran on background
  // threads -- each of which gets its own arena -- so the chunks freed by the
  // sweep are not the chunks the next parse asks for, and RSS parks at the
  // sum of every arena's high-water mark (twelve 3000-line files held ~120 MB
  // this way even with only three trees left alive). malloc_trim walks the
  // arenas and releases what they can give back; it is a no-op on platforms
  // whose allocator already does that itself.
  void release_free_heap()
  {
#if defined(__GLIBC__)
    malloc_trim(0);
#endif
  }

#ifdef JOT_TREESITTER
  bool contains_any(const std::string &text, const std::vector<std::string> &needles)
  {
    for (const auto &needle : needles)
    {
      if (text.find(needle) != std::string::npos)
      {
        return true;
      }
    }
    return false;
  }

  bool header_content_looks_like_cpp(const FileBuffer &buf)
  {
    std::string sample;
    const size_t max_lines = std::min<size_t>(buf.line_count(), 200);
    for (size_t i = 0; i < max_lines; ++i)
    {
      sample += buf.line((int)i);
      sample += '\n';
      if (sample.size() > 32768)
      {
        break;
      }
    }

    return contains_any(sample,
                        {
                            "namespace ",
                            "class ",
                            "template",
                            "typename",
                            "public:",
                            "private:",
                            "protected:",
                            "std::",
                            "::",
                            "constexpr",
                            "noexcept",
                            "override",
                            "final",
                            "operator",
                            "#include <vector>",
                            "#include <string>",
                            "#include <memory>",
                            "#include <iostream>",
                        });
  }

  bool has_cpp_sibling_source(const std::string &path)
  {
    if (path.empty())
    {
      return false;
    }
    std::error_code ec;
    std::filesystem::path header(path);
    std::filesystem::path parent = header.parent_path();
    std::filesystem::path stem = header.stem();
    if (parent.empty() || stem.empty())
    {
      return false;
    }
    for (const auto &ext : {".cpp", ".cc", ".cxx", ".C"})
    {
      if (std::filesystem::exists(parent / (stem.string() + ext), ec))
      {
        return true;
      }
    }
    return false;
  }

  // Start byte of `line_idx` in the buffer, resolved in O(1) via lazily built
  // prefix sums (cleared by FileBuffer::mark_edited on every edit).
  // offsets[i] = byte offset where line i begins in the joined buffer text:
  // offsets[0] = 0 and offsets[i] = offsets[i-1] + line(i-1).size() + 1.
  uint32_t line_start_byte(FileBuffer &buf, int line_idx)
  {
    auto &offsets = buf.ts_line_offsets;
    while ((int)offsets.size() <= line_idx)
    {
      const int next = (int)offsets.size();
      if (next == 0)
      {
        // First line starts at byte 0. (The previous loop body would have read
        // line(-1) here, shifting every line origin by +1 and making every
        // token span end one byte early - dropping the last char of each word.)
        offsets.push_back(0);
      }
      else
      {
        const uint32_t acc = offsets.back();
        offsets.push_back(acc + (uint32_t)buf.line(next - 1).size() + 1);
      }
    }
    return offsets[line_idx];
  }

  // Byte offset of a tree point, clamped into its line. A row past the last
  // line can only come from a tree older than the buffer, and reports itself as
  // out of range so the capture is dropped instead of painted in a wrong spot.
  uint32_t point_to_byte(FileBuffer &buf, TSPoint point)
  {
    if (point.row >= (uint32_t)buf.line_count())
    {
      return UINT32_MAX;
    }
    const std::string &line = buf.line((int)point.row);
    const uint32_t base = line_start_byte(buf, (int)point.row);
    return base + std::min(point.column, (uint32_t)line.size());
  }

  // Moves a tree point by whole rows and columns. A column underflow carries
  // into the previous row, which is what makes `(#offset! @x 0 0 0 -1)` drop a
  // trailing newline from a capture that ran to the end of its line.
  void shift_point(FileBuffer &buf, TSPoint &point, int rows, int columns)
  {
    int row = (int)point.row + rows;
    int column = (int)point.column + columns;
    while (column < 0 && row > 0)
    {
      row--;
      column += (int)buf.line(row).size() + 1;
    }
    if (row < 0)
    {
      row = 0;
    }
    if (column < 0)
    {
      column = 0;
    }
    if (row < buf.line_count())
    {
      const int line_size = (int)buf.line(row).size();
      column = std::min(column, line_size);
    }
    point.row = (uint32_t)row;
    point.column = (uint32_t)column;
  }

  std::vector<std::pair<int, int>>
  query_ts_highlights(FileBuffer &buf, int line_idx, TSQuery *query, TSTree *tree, int byte_limit)
  {
    if (!query || !tree)
    {
      return {};
    }

    const std::string &line = buf.line(line_idx);
    const int n = (int)line.size();
    const int limit = std::clamp(byte_limit, 0, n);
    std::vector<std::pair<int, int>> colors(limit, {0, 0});
    std::vector<int> priorities(limit, 0);

    const uint32_t line_start = line_start_byte(buf, line_idx);

    // Predicates and directives are steps the C library hands back rather than
    // applies: the pattern decides here whether its captures count at all
    // (predicates) and where they land and how strongly (directives). Most
    // query files carry neither, and this gate keeps the per-match lookups and
    // the source text they read off their path entirely.
    const bool has_conditions = jot_query::query_has_conditions(query);
    // A predicate compares a captured node against its own text, so this
    // resolves a tree point back into the buffer. Rows past the last line can
    // only come from a tree older than the buffer; those read as empty.
    const jot_query::TextReader read_text = [&buf](TSPoint start, TSPoint end) {
      std::string out;
      const int line_count = buf.line_count();
      for (uint32_t row = start.row; row <= end.row && row < (uint32_t)line_count; row++)
      {
        const std::string &text_line = buf.line((int)row);
        const uint32_t from = (row == start.row)
                                  ? std::min(start.column, (uint32_t)text_line.size())
                                  : 0;
        const uint32_t to = (row == end.row)
                                ? std::min(end.column, (uint32_t)text_line.size())
                                : (uint32_t)text_line.size();
        if (from < to)
        {
          out.append(text_line, from, to - from);
        }
        if (row != end.row)
        {
          out += '\n';
        }
      }
      return out;
    };

    TSNode root = ts_tree_root_node(tree);
    TSQueryCursor *cursor = ts_query_cursor_new();

    // Query only the visible window of the line: for minified single-line files
    // this keeps per-line highlight cost proportional to the on-screen width
    // instead of the line length.
    ts_query_cursor_set_byte_range(cursor, line_start, line_start + (uint32_t)limit);
    ts_query_cursor_exec(cursor, query, root);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match))
    {
      // A pattern whose predicate fails contributes nothing. This is what keeps
      // a query written for one grammar (or a `#match?` narrowing a node type to
      // keywords) from painting every node the pattern could match.
      if (has_conditions && !jot_query::match_satisfies(query, match, read_text))
      {
        continue;
      }
      for (uint16_t ci = 0; ci < match.capture_count; ci++)
      {
        TSQueryCapture cap = match.captures[ci];
        uint32_t name_len;
        const char *name = ts_query_capture_name_for_id(query, cap.index, &name_len);
        const std::string capture_name(name, name_len);
        const int token = tree_sitter_capture_token_for_name(capture_name);
        if (token == TS_TOKEN_NONE)
        {
          continue;
        }
        int priority = tree_sitter_capture_priority_for_name(capture_name);
        uint32_t s = ts_node_start_byte(cap.node);
        uint32_t e = ts_node_end_byte(cap.node);
        if (has_conditions)
        {
          const jot_query::CaptureDirectives directives =
              jot_query::capture_directives(query, match, capture_name);
          if (directives.has_priority)
          {
            priority = directives.priority;
          }
          if (directives.has_offset)
          {
            TSPoint start_point = ts_node_start_point(cap.node);
            TSPoint end_point = ts_node_end_point(cap.node);
            shift_point(buf, start_point, directives.start_row, directives.start_column);
            shift_point(buf, end_point, directives.end_row, directives.end_column);
            s = point_to_byte(buf, start_point);
            e = point_to_byte(buf, end_point);
          }
        }
        uint32_t start = (s > line_start) ? (s - line_start) : 0;
        uint32_t end = (e > line_start) ? (e - line_start) : 0;
        if (end > (uint32_t)limit)
        {
          end = (uint32_t)limit;
        }
        if (start >= (uint32_t)limit)
        {
          continue;
        }

        for (uint32_t col = start; col < end && col < colors.size(); col++)
        {
          if (priority >= priorities[col])
          {
            priorities[col] = priority;
            colors[col] = {1, token};
          }
        }
      }
    }
    ts_query_cursor_delete(cursor);
    return colors;
  }
#endif

} // namespace

#ifdef JOT_TREESITTER
void FileBuffer::release_syntax()
{
  if (ts_tree)
  {
    ts_tree_delete(ts_tree);
    ts_tree = nullptr;
  }
  if (ts_parser)
  {
    ts_parser_delete(ts_parser);
    ts_parser = nullptr;
  }
}

void Editor::ts_begin_edit(FileBuffer &buf)
{
  // Only the first edit after a rebuild needs a snapshot: later edits in the
  // same batch leave ts_edit_base at the original text the tree was parsed
  // from, so one bounded ts_tree_edit covers all of them.
  if (buf.ts_parser && buf.ts_tree && buf.ts_tree_in_sync)
  {
    buf.ts_edit_base = get_buffer_text(buf);
    buf.ts_edit_base_valid = true;
    buf.ts_tree_in_sync = false;
  }
}

void Editor::reparse_tree(FileBuffer &buf)
{
  if (!buf.ts_parser)
  {
    return;
  }
  std::string text = get_buffer_text(buf);

  if (buf.ts_tree && buf.ts_edit_base_valid)
  {
    // Incremental path: edits were snapshotted by ts_begin_edit, so the tree
    // still matches ts_edit_base. A single bounded edit covers everything that
    // changed since then (typing is usually one line, ~1ms instead of a
    // whole-file parse).
    const std::string base = std::move(buf.ts_edit_base);
    buf.ts_edit_base.clear();
    buf.ts_edit_base_valid = false;
    TSTree *result = TreeSitterManager::reparse_incremental(buf.ts_parser, buf.ts_tree, base, text);
    // reparse_incremental returns the same pointer on no-op; if it also failed
    // to parse (still matching base, not text), keep the edit pending so the
    // next rebuild retries instead of querying a stale tree.
    buf.ts_tree_in_sync = (result == buf.ts_tree && base != text) ? false : true;
    buf.ts_tree = result;
    return;
  }

  // Full rebuild (first parse, language switch, or an edit path that did not
  // go through ts_begin_edit). The per-line syntax cache is intentionally NOT
  // cleared: entries are content-hashed, so untouched lines stay cached and
  // only genuinely changed lines are re-queried.
  if (buf.ts_tree)
  {
    ts_tree_delete(buf.ts_tree);
    buf.ts_tree = nullptr;
  }
  buf.ts_tree = ts_parser_parse_string(buf.ts_parser, nullptr, text.c_str(), (uint32_t)text.size());
  buf.ts_edit_base_valid = false;
  buf.ts_edit_base.clear();
  buf.ts_tree_in_sync = true;
}

// Open-time whole-file parses that would stall the first paint (e.g. ~460ms
// for a 38k-line file) run on the background worker instead; smaller files
// parse synchronously because the cost is imperceptible and the syntax
// colors are then present from the very first frame.
static const int kAsyncParseThresholdLines = 1500;

void Editor::init_ts_for_buffer(FileBuffer &buf)
{
  if (buf.ts_parse_pending)
  {
    // The initial parse is running on the background worker; nothing to do
    // until install_finished_parses hands the tree back.
    return;
  }
  std::string ext = tree_sitter_extension_for_buffer(buf);
  std::string language_id = ts_manager_.language_id_for_extension(ext);
  if (language_id.empty())
  {
    buf.release_syntax();
    buf.ts_language_id.clear();
    buf.ts_edit_base_valid = false;
    buf.ts_edit_base.clear();
    buf.ts_tree_in_sync = true;
    return;
  }

  if (buf.ts_parser && buf.ts_language_id == language_id)
  {
    // Rebuild when the tree is missing or when edits are pending (ts_begin_edit
    // cleared ts_tree_in_sync); the incremental path keeps this cheap.
    if (!buf.ts_tree || !buf.ts_tree_in_sync)
    {
      reparse_tree(buf);
    }
    return;
  }

  buf.release_syntax();
  buf.ts_language_id.clear();
  buf.ts_edit_base_valid = false;
  buf.ts_edit_base.clear();
  buf.ts_tree_in_sync = true;

  TSParser *parser = ts_manager_.create_parser(ext);
  if (!parser)
  {
    return;
  }
  if (buf.line_count() > kAsyncParseThresholdLines && !buf.ts_async_parse_failed)
  {
    // Large file: parse off the main thread so the first frame paints
    // immediately; install_finished_parses (polled by the event loop) swaps
    // the tree in and repaints when the worker finishes.
    TreeSitterManager::AsyncParseJob job;
    job.buffer_index = buffer_index_of(buf);
    job.extension = ext;
    job.language_id = language_id;
    job.text = get_buffer_text(buf);
    const TSLanguageEntry *entry = ts_manager_.get_language(ext);
    if (entry)
    {
      job.symbol = entry->symbol;
      job.library_names = entry->library_names;
    }
    job.library_paths = ts_manager_.runtime_library_paths();
    buf.ts_parser = nullptr;
    buf.ts_language_id = language_id;
    buf.syntax_cache.clear();
    buf.syntax_cache_bytes = 0;
    buf.ts_tree_in_sync = false;
    buf.ts_parse_pending = true;
    ts_manager_.queue_async_parse(std::move(job));
    return;
  }

  buf.ts_parser = parser;
  buf.ts_language_id = language_id;
  buf.syntax_cache.clear();
  buf.syntax_cache_bytes = 0;
  buf.ts_async_parse_failed = false;
  reparse_tree(buf);
}

int Editor::buffer_index_of(const FileBuffer &buf) const
{
  const FileBuffer *base = buffers.data();
  const FileBuffer *p = &buf;
  if (p < base || p >= base + buffers.size())
  {
    return -1;
  }
  return static_cast<int>(p - base);
}

void Editor::install_finished_parses()
{
  std::vector<TreeSitterManager::AsyncParseResult> results = ts_manager_.take_finished_parses();
  if (results.empty())
  {
    return;
  }
  bool repaint = false;
  for (auto &result : results)
  {
    // Take ownership of the worker's dlopen handle up front: the language
    // pointer behind result.parser lives inside that library, so it must
    // stay mapped no matter which path the result takes below (retaining is
    // a no-op when the handle is null, e.g. a failed parse).
    ts_manager_.retain_parser_library(result.language_id, result.library_handle);

    FileBuffer *buf = nullptr;
    if (result.buffer_index >= 0 && result.buffer_index < (int)buffers.size())
    {
      buf = &buffers[(size_t)result.buffer_index];
    }
    // The pending flag doubles as identity: if the buffer was closed and a
    // new one took over the slot, its flag is false, so the stale result is
    // dropped. A language change while parsing drops it too.
    if (!buf || !buf->ts_parse_pending || buf->ts_language_id != result.language_id)
    {
      if (result.parser)
      {
        ts_parser_delete(result.parser);
      }
      if (result.tree)
      {
        ts_tree_delete(result.tree);
      }
      continue;
    }
    if (!result.parser || !result.tree)
    {
      // The worker could not load a parser (e.g. not installed yet): fall
      // back to the synchronous path, but only once - the failed flag keeps
      // later inits from re-queuing forever.
      buf->ts_parse_pending = false;
      buf->ts_async_parse_failed = true;
      continue;
    }
    buf->ts_parser = result.parser;
    buf->ts_tree = result.tree;
    buf->ts_parse_pending = false;
    if (get_buffer_text(*buf) != result.parsed_text)
    {
      // The buffer was edited while the parse ran. The tree still describes
      // parsed_text, so hand it to the incremental repair path instead of
      // discarding: reparse_tree diffs parsed_text against the current text
      // and applies one bounded ts_tree_edit (cheap, tracks the edited
      // region).
      buf->ts_edit_base = std::move(result.parsed_text);
      buf->ts_edit_base_valid = true;
      buf->ts_tree_in_sync = false;
    }
    else
    {
      buf->ts_tree_in_sync = true;
    }
    buf->syntax_cache.clear();
    buf->syntax_cache_bytes = 0;
    repaint = true;
  }
  if (repaint)
  {
    needs_redraw = true;
  }
}

void Editor::evict_stale_syntax_trees()
{
  std::vector<FileBuffer *> holders;
  for (auto &buf : buffers)
  {
    if (buf.ts_tree)
    {
      holders.push_back(&buf);
    }
  }
  if (holders.size() <= kMaxCachedSyntaxTrees)
  {
    syntax_sweep_seq_ = ts_use_seq_;
    return;
  }

  // Newest first, so the tail of this list is what goes.
  std::sort(holders.begin(), holders.end(), [](const FileBuffer *a, const FileBuffer *b) {
    return a->ts_last_use_seq > b->ts_last_use_seq;
  });
  // Everything drawn since the previous sweep is the on-screen set: it keeps
  // its trees however large that set is (a split layout must never evict a pane
  // it is about to redraw, which would make every frame re-parse).
  std::vector<FileBuffer *> stale;
  for (FileBuffer *buf : holders)
  {
    if (buf->ts_last_use_seq > syntax_sweep_seq_)
    {
      continue;
    }
    stale.push_back(buf);
  }
  // The newest stale trees fill the cache up to the budget; the rest are
  // dropped. When the on-screen set already fills the budget nothing else is
  // kept, which is the point: the visible buffers are the ones that matter.
  const std::size_t protected_count = holders.size() - stale.size();
  const std::size_t keep = protected_count >= kMaxCachedSyntaxTrees
                               ? 0
                               : kMaxCachedSyntaxTrees - protected_count;
  bool dropped_any = false;
  for (std::size_t i = keep; i < stale.size(); i++)
  {
    FileBuffer &buf = *stale[i];
    buf.release_syntax();
    // The buffer now holds no parser at all, so clear the incremental-parse
    // bookkeeping with it: leaving a stale ts_edit_base behind would make the
    // next rebuild diff against a tree that no longer exists.
    buf.ts_parse_pending = false;
    buf.ts_edit_base_valid = false;
    buf.ts_edit_base.clear();
    buf.ts_tree_in_sync = true;
    dropped_any = true;
  }
  if (dropped_any)
  {
    release_free_heap();
  }
  syntax_sweep_seq_ = ts_use_seq_;
}

std::string Editor::tree_sitter_extension_for_buffer(FileBuffer &buf)
{
  const std::string ext = get_file_extension(buf.filepath);
  if (ext != ".h" || ts_manager_.has_language_override(ext))
  {
    return ext;
  }
  // The probe below is a property of the buffer, not of the line being
  // highlighted, but this function is called once per rendered row per frame
  // (plus once per bracket/diagnostic pass). Resolve it once and remember the
  // answer for this path; mark_edited drops a "not C++" answer so a header the
  // user is filling in still gets promoted on the next frame, while the
  // stable ".cpp" answer survives edits.
  if (buf.ts_extension_probe_path != buf.filepath)
  {
    buf.ts_extension_probe_path = buf.filepath;
    buf.ts_extension_probe.clear();
  }
  if (buf.ts_extension_probe.empty())
  {
    buf.ts_extension_probe =
        (has_cpp_sibling_source(buf.filepath) || header_content_looks_like_cpp(buf)) ? ".cpp" : ext;
  }
  return buf.ts_extension_probe;
}
#endif

const std::vector<std::pair<int, int>> &
Editor::get_line_syntax_colors(FileBuffer &buf, int line_idx, int byte_limit)
{
  static const std::vector<std::pair<int, int>> empty_colors;

  if (line_idx < 0 || line_idx >= (int)buf.line_count())
  {
    return empty_colors;
  }

#ifdef JOT_TREESITTER
  const std::string ts_extension = tree_sitter_extension_for_buffer(buf);
#else
  const std::string ts_extension;
#endif
  const std::string raw_extension = get_file_extension(buf.filepath);
  const std::string cache_extension =
#ifdef JOT_TREESITTER
      raw_extension + "|" + ts_extension;
#else
      raw_extension;
#endif
  if (buf.syntax_cache_extension != cache_extension)
  {
    buf.syntax_cache_extension = cache_extension;
    buf.syntax_cache_line_count = buf.line_count();
    buf.syntax_cache.clear();
    buf.syntax_cache_bytes = 0;
    buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
    buf.syntax_language_label.clear();
  }

  if (buf.syntax_cache_line_count != buf.line_count())
  {
    buf.syntax_cache_line_count = buf.line_count();
    buf.syntax_cache.clear();
    buf.syntax_cache_bytes = 0;
  }

#ifdef JOT_TREESITTER
  const bool tree_sitter_candidate = ts_manager_.has_language(ts_extension);
  if (tree_sitter_candidate)
  {
    init_ts_for_buffer(buf);
  }
#endif

  bool retry_tree_sitter = false;
#ifdef JOT_TREESITTER
  TSQuery *query = (tree_sitter_candidate && buf.ts_tree)
                       ? ts_manager_.get_highlight_query(ts_extension)
                       : nullptr;
  // The per-line cache holds colors keyed on line content; when the query
  // itself was replaced (e.g. the deferred boot-time compile installed the
  // bundled query after first paint compiled a fallback), every cached entry
  // is stale regardless of line content, so invalidate the whole cache once
  // (before the per-line entry reference is taken below) instead of leaving
  // old lines colored by the previous query.
  if (query && buf.ts_tree && buf.syntax_query != query && !buf.syntax_cache.empty())
  {
    buf.syntax_cache.clear();
    buf.syntax_cache_bytes = 0;
    buf.syntax_cache_line_count = buf.line_count();
  }
  // Re-run the tree-sitter pass not only when the engine is not TS yet, but
  // also when the query changed since the cache was built (e.g. the deferred
  // boot-time compile installed a different query after first paint).
  retry_tree_sitter =
      query != nullptr && buf.ts_tree
      && (buf.syntax_engine != SYNTAX_ENGINE_TREESITTER || buf.syntax_query != query);
#endif

  const std::string &line = buf.line(line_idx);
  // Bound the per-line colour cache before touching it. Each entry holds a
  // (token flag, token type) pair per byte of its line, so a multi-megabyte
  // source file scrolled end to end would otherwise keep one entry -- and one
  // colour vector -- for every line ever rendered, growing without limit. When
  // the accumulated cost passes the budget the whole cache is dropped; only the
  // viewport is highlighted per frame, so refilling costs exactly one frame of
  // the work a first paint already does. Checked before `cache` is taken so the
  // reference can never outlive the clear.
  if (buf.syntax_cache_bytes > kSyntaxCacheByteBudget)
  {
    buf.syntax_cache.clear();
    buf.syntax_cache_bytes = 0;
  }
  SyntaxLineCache &cache = buf.syntax_cache[line_idx];
  // Charge this entry against the cache budget, adding only the difference when
  // a line is re-highlighted (a windowed line grows between frames).
  const auto account_cache_entry = [&buf, &cache]()
  {
    // The fixed term approximates the unordered_map node plus the vector
    // header, which the budget cares about at the same order of magnitude as
    // the colour bytes themselves.
    const std::size_t now =
        sizeof(SyntaxLineCache) + 48u + cache.colors.size() * sizeof(std::pair<int, int>);
    buf.syntax_cache_bytes += now - cache.accounted_bytes;
    cache.accounted_bytes = now;
  };
  // Normal lines are highlighted whole once and cached for good: requests with
  // a growing window (horizontal scroll into the line, minimap probing) then
  // hit the cache instead of re-running the query/regexes at every window size.
  // Only genuinely huge lines keep the windowed behavior so first paint of a
  // minified single-line file stays proportional to the visible width.
  const int line_len = (int)line.size();
  const int limit =
      std::clamp(line_len <= kFullHighlightLineBytes ? line_len : byte_limit, 0, line_len);

  // Compare lengths first so cache hits on huge single lines don't pay for a
  // full-line hash every frame; only hash when the length matches.
  if (!retry_tree_sitter && cache.valid && cache.line_length == line.length()
      && cache.colors_upto >= (std::size_t)limit
      && cache.line_hash == std::hash<std::string>{}(line))
  {
    return cache.colors;
  }
  const std::size_t line_hash = std::hash<std::string>{}(line);

#ifdef JOT_TREESITTER
  if (query)
  {
    cache.colors = query_ts_highlights(buf, line_idx, query, buf.ts_tree, limit);
    cache.line_hash = line_hash;
    cache.line_length = line.length();
    cache.colors_upto = (std::size_t)limit;
    cache.valid = true;
    buf.syntax_engine = SYNTAX_ENGINE_TREESITTER;
    buf.syntax_query = query;
    buf.syntax_language_label = ts_manager_.language_id_for_extension(ts_extension);
    account_cache_entry();
    return cache.colors;
  }
  buf.syntax_query = nullptr;
#endif

  highlighter.set_language(raw_extension);
  cache.colors = highlighter.get_colors(line, limit);
  account_cache_entry();
  if (highlighter.has_rules())
  {
    buf.syntax_engine = SYNTAX_ENGINE_REGEX;
    buf.syntax_language_label = raw_extension.empty() ? "plain" : raw_extension;
  }
  else
  {
    buf.syntax_engine = SYNTAX_ENGINE_NONE;
    // No engine could color this file, but when the filetype is still known
    // (a tree-sitter grammar exists for it without the parser installed, or
    // the LSP catalog maps the extension) keep a language label so status /
    // filetype views identify it instead of reporting a blank file.
    std::string label;
#ifdef JOT_TREESITTER
    label = ts_manager_.language_id_for_extension(ts_extension);
#endif
    if (label.empty())
    {
      label = catalog_filetype_label(raw_extension);
    }
    buf.syntax_language_label = label;
  }
  cache.line_hash = line_hash;
  cache.line_length = line.length();
  cache.colors_upto = (std::size_t)limit;
  cache.valid = true;
  return cache.colors;
}

void Editor::invalidate_syntax_cache(FileBuffer &buf)
{
  buf.mark_edited();
  buf.syntax_cache_extension.clear();
  buf.syntax_cache_line_count = 0;
  buf.syntax_cache.clear();
  buf.syntax_cache_bytes = 0;
  buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
#ifdef JOT_TREESITTER
  buf.syntax_query = nullptr;
  // The tree goes with the colours. This is the invalidation the paths that
  // rewrite a buffer from outside the edit hooks call -- the on-save external
  // formatters (prettier, clang-format), an LSP format, a Lua buffer write --
  // and they swap `lines` without ever snapshotting the text the tree was
  // parsed from. Leaving ts_tree_in_sync true would have the next paint believe
  // the tree still matches, skip the reparse and query it against the new bytes:
  // every highlight past the rewrite would come from the old offsets and stay
  // wrong, since nothing marks the tree out of sync again. Callers that did
  // snapshot first (ts_begin_edit) keep their ts_edit_base, so the rebuild is
  // the incremental one.
  buf.ts_tree_in_sync = false;
#endif
  buf.syntax_language_label.clear();
}
