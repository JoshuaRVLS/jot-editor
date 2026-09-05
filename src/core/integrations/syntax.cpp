#include "editor.h"
#include "tree_sitter/manager.h"
#include <algorithm>
#include <filesystem>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace
{

  // Lines no longer than this are highlighted whole and cached; only longer
  // lines use windowed highlighting bounded by the visible width.
  constexpr int kFullHighlightLineBytes = 4096;

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
        // token span end one byte early — dropping the last char of each word.)
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
      for (uint16_t ci = 0; ci < match.capture_count; ci++)
      {
        TSQueryCapture cap = match.captures[ci];
        uint32_t s = ts_node_start_byte(cap.node);
        uint32_t e = ts_node_end_byte(cap.node);
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

        uint32_t name_len;
        const char *name = ts_query_capture_name_for_id(query, cap.index, &name_len);
        const int token = tree_sitter_capture_token_for_name(std::string(name, name_len));
        const int priority = tree_sitter_capture_priority_for_name(std::string(name, name_len));
        if (token == TS_TOKEN_NONE)
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
    if (buf.ts_tree)
    {
      ts_tree_delete(buf.ts_tree);
      buf.ts_tree = nullptr;
    }
    if (buf.ts_parser)
    {
      ts_parser_delete(buf.ts_parser);
      buf.ts_parser = nullptr;
    }
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

  if (buf.ts_tree)
  {
    ts_tree_delete(buf.ts_tree);
    buf.ts_tree = nullptr;
  }
  if (buf.ts_parser)
  {
    ts_parser_delete(buf.ts_parser);
    buf.ts_parser = nullptr;
  }
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
    buf.ts_tree_in_sync = false;
    buf.ts_parse_pending = true;
    ts_manager_.queue_async_parse(std::move(job));
    return;
  }

  buf.ts_parser = parser;
  buf.ts_language_id = language_id;
  buf.syntax_cache.clear();
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
  std::vector<TreeSitterManager::AsyncParseResult> results =
      ts_manager_.take_finished_parses();
  if (results.empty())
  {
    return;
  }
  bool repaint = false;
  for (auto &result : results)
  {
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
    repaint = true;
  }
  if (repaint)
  {
    needs_redraw = true;
  }
}

std::string Editor::tree_sitter_extension_for_buffer(const FileBuffer &buf)
{
  std::string ext = get_file_extension(buf.filepath);
  if (ext != ".h" || ts_manager_.has_language_override(ext))
  {
    return ext;
  }
  if (has_cpp_sibling_source(buf.filepath) || header_content_looks_like_cpp(buf))
  {
    return ".cpp";
  }
  return ext;
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
    buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
    buf.syntax_language_label.clear();
  }

  if (buf.syntax_cache_line_count != buf.line_count())
  {
    buf.syntax_cache_line_count = buf.line_count();
    buf.syntax_cache.clear();
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
    buf.syntax_cache_line_count = buf.line_count();
  }
  // Re-run the tree-sitter pass not only when the engine is not TS yet, but
  // also when the query changed since the cache was built (e.g. the deferred
  // boot-time compile installed a different query after first paint).
  retry_tree_sitter = query != nullptr && buf.ts_tree
                      && (buf.syntax_engine != SYNTAX_ENGINE_TREESITTER
                          || buf.syntax_query != query);
#endif

  const std::string &line = buf.line(line_idx);
  SyntaxLineCache &cache = buf.syntax_cache[line_idx];
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
    return cache.colors;
  }
  buf.syntax_query = nullptr;
#endif

  highlighter.set_language(raw_extension);
  cache.colors = highlighter.get_colors(line, limit);
  if (highlighter.has_rules())
  {
    buf.syntax_engine = SYNTAX_ENGINE_REGEX;
    buf.syntax_language_label = raw_extension.empty() ? "plain" : raw_extension;
  }
  else
  {
    buf.syntax_engine = SYNTAX_ENGINE_NONE;
    buf.syntax_language_label.clear();
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
  buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
#ifdef JOT_TREESITTER
  buf.syntax_query = nullptr;
#endif
  buf.syntax_language_label.clear();
}
