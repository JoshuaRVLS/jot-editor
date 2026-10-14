#include "base/string_util.h"
#include "core/editor.h"
#include "lsp_attach_data.h"
#include "syntax/folding.h"
#include "syntax/manager.h"
#include "syntax/query_predicates.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
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

  // Byte-range reading of the buffer, for predicates that compare a captured
  // node against its own text and for the text an injected region parses. A row
  // past the last line can only come from a tree older than the buffer; those
  // read as empty, which the text predicates treat as satisfied.
  jot_query::TextReader buffer_text_reader(FileBuffer &buf, TSPoint origin = {0, 0})
  {
    return [&buf, origin](TSPoint start, TSPoint end) {
      // A child tree's points are relative to the region it was parsed from:
      // every row moves by the region's first row, and a point on the region's
      // own first row moves by its first column too.
      if (origin.row != 0 || origin.column != 0)
      {
        start = {(uint32_t)(origin.row + start.row),
                 (uint32_t)(start.row == 0 ? origin.column + start.column : start.column)};
        end = {(uint32_t)(origin.row + end.row),
               (uint32_t)(end.row == 0 ? origin.column + end.column : end.column)};
      }
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

  // The `.ext` of a file name, lowercased, or an empty string when it has none.
  // The name arrives as node text from an injection capture, so it is a bare
  // name (or a path) rather than a buffer path.
  std::string extension_of_file_name(const std::string &file_name)
  {
    const size_t slash = file_name.find_last_of("/\\");
    const size_t dot = file_name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    {
      return "";
    }
    std::string extension = file_name.substr(dot);
    std::transform(extension.begin(),
                   extension.end(),
                   extension.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return extension;
  }

  std::string trim_spaces(const std::string &text)
  {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace((unsigned char)text[begin]))
    {
      begin++;
    }
    while (end > begin && std::isspace((unsigned char)text[end - 1]))
    {
      end--;
    }
    return text.substr(begin, end - begin);
  }

  // Paints the colours of an embedded language over the cells its region owns.
  //
  // A host file often holds a whole other language inside one node -- a SQL
  // string in Python, CSS in HTML, a shell here-doc, a fenced code block in
  // Markdown -- and the host grammar can only call that text a string. The
  // language's own `injections.scm` says which nodes those are and what
  // language each one holds; the region's text is parsed with that language's
  // grammar and its captures are painted over the host's inside the region.
  //
  // The work is bounded to the line being coloured: the injection query runs
  // over that line's byte window, and only the regions touching it are parsed.
  // A line's colours are then cached, so a 300-line block costs one parse per
  // line rather than one per frame.
  void paint_injected_highlights(FileBuffer &buf,
                                 int line_idx,
                                 const std::string &extension,
                                 int limit,
                                 std::vector<std::pair<int, int>> &colors,
                                 TreeSitterManager &manager)
  {
    if (!buf.ts_tree || limit <= 0 || colors.empty())
    {
      return;
    }
    TSQuery *injection_query = manager.get_injection_query(extension);
    if (!injection_query)
    {
      return;
    }
    const uint32_t line_start = line_start_byte(buf, line_idx);
    const bool has_conditions = jot_query::query_has_conditions(injection_query);
    const jot_query::TextReader read_buffer_text = buffer_text_reader(buf);

    TSQueryCursor *cursor = ts_query_cursor_new();
    if (!cursor)
    {
      return;
    }
    ts_query_cursor_set_byte_range(cursor, line_start, line_start + (uint32_t)limit);
    ts_query_cursor_exec(cursor, injection_query, ts_tree_root_node(buf.ts_tree));

    // Child captures are painted over the host's inside the region, so the one
    // question they answer is which child capture wins a cell: the priorities
    // only order them among themselves.
    std::vector<int> child_priorities(colors.size(), 0);

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match))
    {
      if (has_conditions && !jot_query::match_satisfies(injection_query, match, read_buffer_text))
      {
        continue;
      }
      const jot_query::InjectionDirectives directives =
          jot_query::injection_directives(injection_query, match.pattern_index);
      std::string language = directives.language;
      TSNode content = {};
      bool have_content = false;
      for (uint16_t ci = 0; ci < match.capture_count; ci++)
      {
        const TSQueryCapture cap = match.captures[ci];
        uint32_t name_len = 0;
        const char *name = ts_query_capture_name_for_id(injection_query, cap.index, &name_len);
        const std::string capture_name(name, name_len);
        if (capture_name == "injection.content")
        {
          content = cap.node;
          have_content = true;
        }
        else if (capture_name == "injection.language")
        {
          // The language for this match can be data rather than a constant:
          // a fenced block's info string (` ```python `) names it.
          language = trim_spaces(
              read_buffer_text(ts_node_start_point(cap.node), ts_node_end_point(cap.node)));
        }
        else if (capture_name == "injection.filename")
        {
          // Some queries name a file instead, and its extension says the
          // language (`(comment) @injection.content (#set! injection.filename)
          // ...`): the capture's own text is the file name.
          const std::string file_name = trim_spaces(
              read_buffer_text(ts_node_start_point(cap.node), ts_node_end_point(cap.node)));
          language = manager.language_id_for_extension(extension_of_file_name(file_name));
        }
      }
      if (!have_content)
      {
        continue;
      }
      const std::string child_id = manager.language_for_extension(language);
      if (child_id.empty())
      {
        continue;
      }
      const TSPoint region_start = ts_node_start_point(content);
      const TSPoint region_end = ts_node_end_point(content);
      const int child_row = line_idx - (int)region_start.row;
      const int child_last_row = (int)region_end.row - (int)region_start.row;
      if (child_row < 0 || child_row > child_last_row)
      {
        continue;
      }
      TSQuery *child_query = manager.get_highlight_query_for_language(child_id);
      if (!child_query)
      {
        continue;
      }
      TSParser *child_parser = manager.create_parser_for_language(child_id);
      if (!child_parser)
      {
        continue;
      }
      const std::string region =
          read_buffer_text(region_start, region_end);
      TSTree *child_tree = ts_parser_parse_string(
          child_parser, nullptr, region.c_str(), (uint32_t)region.size());
      ts_parser_delete(child_parser);
      if (!child_tree)
      {
        continue;
      }

      // The child's row `child_row` is this buffer line, and its byte range is
      // what bounds the child query to it. region_line_offsets[r] is where row r
      // of the region text starts, which is also how a child point turns into a
      // byte offset inside the region.
      std::vector<uint32_t> region_line_offsets;
      region_line_offsets.push_back(0);
      for (size_t i = 0; i < region.size(); i++)
      {
        if (region[i] == '\n')
        {
          region_line_offsets.push_back((uint32_t)i + 1);
        }
      }
      region_line_offsets.push_back((uint32_t)region.size());
      const auto region_byte = [&region, &region_line_offsets](TSPoint point) -> uint32_t {
        if (point.row + 1 >= region_line_offsets.size())
        {
          return (uint32_t)region.size();
        }
        const uint32_t base = region_line_offsets[point.row];
        const uint32_t line_end = region_line_offsets[point.row + 1];
        const uint32_t line_size = line_end > base ? line_end - base - 1 : 0;
        return base + std::min(point.column, line_size);
      };
      // Predicates in the child query compare the child's own text.
      const jot_query::TextReader read_region_text = [&region, &region_byte](TSPoint start,
                                                                            TSPoint end) {
        const uint32_t from = region_byte(start);
        const uint32_t to = region_byte(end);
        return to > from ? region.substr(from, to - from) : std::string();
      };
      const uint32_t child_line_start = region_line_offsets[(size_t)child_row];
      const uint32_t child_line_end = region_line_offsets[(size_t)child_row + 1];
      const int column_shift = child_row == 0 ? (int)region_start.column : 0;

      const bool child_has_conditions = jot_query::query_has_conditions(child_query);
      ts_query_cursor_set_byte_range(cursor, child_line_start, child_line_end);
      ts_query_cursor_exec(cursor, child_query, ts_tree_root_node(child_tree));
      const uint32_t child_line_len = child_line_end - child_line_start;
      while (ts_query_cursor_next_match(cursor, &match))
      {
        if (child_has_conditions
            && !jot_query::match_satisfies(child_query, match, read_region_text))
        {
          continue;
        }
        for (uint16_t ci = 0; ci < match.capture_count; ci++)
        {
          const TSQueryCapture cap = match.captures[ci];
          uint32_t name_len = 0;
          const char *name = ts_query_capture_name_for_id(child_query, cap.index, &name_len);
          const std::string capture_name(name, name_len);
          const int token = tree_sitter_capture_token_for_name(capture_name);
          if (token == TS_TOKEN_NONE)
          {
            continue;
          }
          int priority = tree_sitter_capture_priority_for_name(capture_name);
          uint32_t s = ts_node_start_byte(cap.node);
          uint32_t e = ts_node_end_byte(cap.node);
          if (child_has_conditions)
          {
            const jot_query::CaptureDirectives capture_directives =
                jot_query::capture_directives(child_query, match, capture_name);
            if (capture_directives.has_priority)
            {
              priority = capture_directives.priority;
            }
            if (capture_directives.has_offset)
            {
              // A shifted child capture walks the region's own rows, not the
              // buffer's: the child tree does not know this file.
              const auto shifted = [&](TSPoint point, int rows, int columns) -> uint32_t {
                int row = (int)point.row + rows;
                int column = (int)point.column + columns;
                const int last_row = (int)region_line_offsets.size() - 2;
                while (column < 0 && row > 0)
                {
                  column += (int)(region_line_offsets[(size_t)row]
                                  - region_line_offsets[(size_t)row - 1]);
                }
                row = std::clamp(row, 0, std::max(last_row, 0));
                const uint32_t base = region_line_offsets[(size_t)row];
                const uint32_t line_size = region_line_offsets[(size_t)row + 1] > base
                                               ? region_line_offsets[(size_t)row + 1] - base - 1
                                               : 0;
                column = std::clamp(column, 0, (int)line_size);
                return base + (uint32_t)column;
              };
              s = shifted(ts_node_start_point(cap.node),
                          capture_directives.start_row,
                          capture_directives.start_column);
              e = shifted(ts_node_end_point(cap.node),
                          capture_directives.end_row,
                          capture_directives.end_column);
            }
          }
          uint32_t start = s > child_line_start ? s - child_line_start : 0;
          uint32_t end = e > child_line_start ? e - child_line_start : 0;
          end = std::min(end, child_line_len);
          if (start >= child_line_len)
          {
            continue;
          }
          for (uint32_t col = start; col < end; col++)
          {
            const int target = column_shift + (int)col;
            if (target < 0 || target >= (int)colors.size())
            {
              continue;
            }
            if (priority >= child_priorities[(size_t)target])
            {
              child_priorities[(size_t)target] = priority;
              colors[(size_t)target] = {1, token};
            }
          }
        }
      }
      ts_tree_delete(child_tree);
    }
    ts_query_cursor_delete(cursor);
  }

  // What an `indents.scm` capture asks of the node it is on (query_predicates.h
  // names the file; these are its capture names).
  struct IndentMarks
  {
    bool begin = false;
    bool end = false;
    bool branch = false;
    bool dedent = false;
    bool auto_indent = false;
    bool ignore = false;
    bool zero = false;
    bool align = false;
    jot_query::IndentProperties properties;
  };

  bool indent_marks_add(IndentMarks &marks, const std::string &capture_name)
  {
    if (capture_name == "indent.begin")
      marks.begin = true;
    else if (capture_name == "indent.end")
      marks.end = true;
    else if (capture_name == "indent.branch")
      marks.branch = true;
    else if (capture_name == "indent.dedent")
      marks.dedent = true;
    else if (capture_name == "indent.auto")
      marks.auto_indent = true;
    else if (capture_name == "indent.ignore")
      marks.ignore = true;
    else if (capture_name == "indent.zero")
      marks.zero = true;
    else if (capture_name == "indent.align")
      marks.align = true;
    else
      return false;
    return true;
  }

  // The indent captures of `node`, looked up by node identity. A node can carry
  // several at once (Lua's `end` is both a branch and an end), and the begin
  // ones also carry the pattern's properties.
  IndentMarks indent_marks_for(const std::vector<std::pair<TSNode, IndentMarks>> &captures,
                              TSNode node)
  {
    IndentMarks marks;
    for (const auto &capture : captures)
    {
      if (!ts_node_eq(capture.first, node))
      {
        continue;
      }
      marks.begin = marks.begin || capture.second.begin;
      marks.end = marks.end || capture.second.end;
      marks.branch = marks.branch || capture.second.branch;
      marks.dedent = marks.dedent || capture.second.dedent;
      marks.auto_indent = marks.auto_indent || capture.second.auto_indent;
      marks.ignore = marks.ignore || capture.second.ignore;
      marks.zero = marks.zero || capture.second.zero;
      marks.align = marks.align || capture.second.align;
      if (capture.second.begin)
      {
        marks.properties = capture.second.properties;
      }
    }
    return marks;
  }

  // Every `indents.scm` capture on the nodes that overlap [start, end): the
  // blocks, branches and closers the indentation of one line is decided by.
  // Bounded to that byte window, and an ancestor of a node in the window always
  // overlaps the window, so the walk below never needs the rest of the file.
  std::vector<std::pair<TSNode, IndentMarks>> gather_indent_captures(FileBuffer &buf,
                                                                    TSQuery *query,
                                                                    TSNode root,
                                                                    uint32_t start,
                                                                    uint32_t end)
  {
    std::vector<std::pair<TSNode, IndentMarks>> captures;
    TSQueryCursor *cursor = ts_query_cursor_new();
    if (!cursor)
    {
      return captures;
    }
    const bool has_conditions = jot_query::query_has_conditions(query);
    const jot_query::TextReader read_text = buffer_text_reader(buf);
    ts_query_cursor_set_byte_range(cursor, start, end);
    ts_query_cursor_exec(cursor, query, root);
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match))
    {
      if (has_conditions && !jot_query::match_satisfies(query, match, read_text))
      {
        continue;
      }
      const jot_query::IndentProperties properties =
          jot_query::indent_properties(query, match.pattern_index);
      for (uint16_t i = 0; i < match.capture_count; i++)
      {
        uint32_t name_len = 0;
        const char *name = ts_query_capture_name_for_id(query, match.captures[i].index, &name_len);
        const std::string capture_name(name, name_len);
        // A leading underscore marks a capture that only feeds a predicate.
        if (!capture_name.empty() && capture_name[0] == '_')
        {
          continue;
        }
        IndentMarks marks;
        if (!indent_marks_add(marks, capture_name))
        {
          continue;
        }
        marks.properties = properties;
        captures.push_back({match.captures[i].node, marks});
      }
    }
    ts_query_cursor_delete(cursor);
    return captures;
  }

  // The column of the last non-whitespace character of `line`, or UINT32_MAX
  // when it has none: the position the indenter reads the previous line from.
  uint32_t last_content_column(const std::string &line)
  {
    const size_t at = line.find_last_not_of(" \t\r");
    return at == std::string::npos ? UINT32_MAX : (uint32_t)at;
  }

  TSNode node_at_byte(TSTree *tree, uint32_t byte)
  {
    if (!tree || byte == UINT32_MAX || byte > ts_node_end_byte(ts_tree_root_node(tree)))
    {
      return {};
    }
    TSNode root = ts_tree_root_node(tree);
    return ts_node_descendant_for_byte_range(root, byte, byte);
  }

  std::vector<std::pair<int, int>>
  query_ts_highlights(FileBuffer &buf, int line_idx, TSQuery *query, TSTree *tree, int byte_limit,
                      const std::string &extension, TreeSitterManager &manager)
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
    const jot_query::TextReader read_text = buffer_text_reader(buf);

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
    paint_injected_highlights(buf, line_idx, extension, limit, colors, manager);
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

// The fold ranges a language's own `folds.scm` asks for, in place of the
// bracket/indent guess `Folding::detect_ranges` makes. The query file is the
// same one nvim-treesitter ships and :tsinstall writes into the query paths,
// and it knows the grammar: a multi-line string, a block that is not indented,
// a table literal. Returns false (leaving `ranges` empty) when the language has
// no fold query, no parser, or nothing to fold, and the caller falls back to
// the heuristic.
bool Editor::query_fold_ranges(FileBuffer &buf, std::vector<FoldRange> &ranges)
{
  ranges.clear();
  const std::string extension = tree_sitter_extension_for_buffer(buf);
  if (extension.empty() || !ts_manager_.has_language(extension))
  {
    return false;
  }
  if (!syntax_tree_ready(buf) || !buf.ts_tree)
  {
    return false;
  }
  TSQuery *query = ts_manager_.get_fold_query(extension);
  if (!query)
  {
    return false;
  }
  const jot_query::TextReader read_text = buffer_text_reader(buf);
  TSQueryCursor *cursor = ts_query_cursor_new();
  if (!cursor)
  {
    return false;
  }
  ts_query_cursor_exec(cursor, query, ts_tree_root_node(buf.ts_tree));
  const int line_count = buf.line_count();
  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match))
  {
    if (!jot_query::match_satisfies(query, match, read_text))
    {
      continue;
    }
    for (uint16_t i = 0; i < match.capture_count; i++)
    {
      uint32_t name_length = 0;
      const char *name =
          ts_query_capture_name_for_id(query, match.captures[i].index, &name_length);
      const std::string capture(name, name_length);
      // `@fold`, and the namespaced forms (`@fold.inner`) that newer query
      // files use to say which block a range belongs to.
      if (capture != "fold" && capture.rfind("fold.", 0) != 0)
      {
        continue;
      }
      const TSPoint start = ts_node_start_point(match.captures[i].node);
      const TSPoint end = ts_node_end_point(match.captures[i].node);
      const int start_line = (int)start.row;
      // A node that ends where a line begins -- a block's closing bracket on a
      // row of its own -- does not fold that row in, only the ones it has text
      // on.
      int end_line = (int)end.row - (end.column == 0 ? 1 : 0);
      end_line = std::min(end_line, line_count - 1);
      if (end_line <= start_line)
      {
        continue;
      }
      ranges.push_back({start_line, end_line, false});
    }
  }
  ts_query_cursor_delete(cursor);
  if (ranges.empty())
  {
    return false;
  }
  std::sort(ranges.begin(), ranges.end(), [](const FoldRange &a, const FoldRange &b) {
    if (a.start_line != b.start_line)
    {
      return a.start_line < b.start_line;
    }
    return a.end_line > b.end_line;
  });
  ranges.erase(std::unique(ranges.begin(),
                           ranges.end(),
                           [](const FoldRange &a, const FoldRange &b) {
                             return a.start_line == b.start_line && a.end_line == b.end_line;
                           }),
               ranges.end());
  return true;
}

void Editor::refresh_fold_ranges(FileBuffer &buf)
{
#ifdef JOT_TREESITTER
  std::vector<FoldRange> query_ranges;
  if (query_fold_ranges(buf, query_ranges))
  {
    // The language's own answer replaces the guess wholesale rather than
    // merging with it: a query file folds the blocks the grammar has, and
    // adding bracket pairs (or indent steps) a grammar does not mean on top of
    // that is how a fold lands on an unrelated row.
    Folding::apply_detected_ranges(buf.fold_ranges, std::move(query_ranges));
    return;
  }
#endif
  Folding::refresh_ranges(buf.fold_ranges, buf.lines, get_file_extension(buf.filepath));
}

// The indent of the empty line a new line takes below buffer line `line_idx`,
// from the language's own `indents.scm`.
//
// The grammar knows where the blocks are, which is what the text heuristics in
// EditorFeatures have to guess: the line after `if(A)` belongs one step in, the
// line after `endif()` does not, a continuation inside a multi-line argument
// list keeps the indentation the user gave it. The file names those nodes
// (`@indent.begin`, `@indent.branch`, `@indent.end`, `@indent.auto`), so the
// answer is a walk up the ancestors of the node the previous line ends with: an
// open block steps in, a branch or a dedent steps out, and a multiline string
// or comment asks for the previous line's indentation to be kept.
//
// Returns false when the language has no indent query or no usable tree, which
// leaves the caller's own estimate in place. This is nvim-treesitter's
// algorithm (indent.lua) without its `@indent.align` delimiter handling, which
// needs per-capture delimiter metadata no query here uses.
bool Editor::query_indent_for_new_line(FileBuffer &buf, int line_idx, int *indent_columns)
{
  if (!indent_columns)
  {
    return false;
  }
  const std::string extension = tree_sitter_extension_for_buffer(buf);
  if (extension.empty() || !ts_manager_.has_language(extension))
  {
    return false;
  }
  // The parse is asked for here rather than through syntax_tree_ready, which
  // reports a missing tree in the message line: Enter must not do that.
  init_ts_for_buffer(buf);
  if (!buf.ts_tree || !buf.ts_tree_in_sync)
  {
    return false;
  }
  TSQuery *query = ts_manager_.get_indent_query(extension);
  if (!query)
  {
    return false;
  }
  if (line_idx < 0 || line_idx >= buf.line_count())
  {
    return false;
  }
  TSTree *tree = buf.ts_tree;
  TSNode root = ts_tree_root_node(tree);
  const uint32_t shift = (uint32_t)std::max(1, tab_size);
  const int target_row = line_idx + 1;

  // The line a new line creates is empty, so its indent is decided by the line
  // above it: the indenter starts from the last node of the previous non-blank
  // line. A blank previous line means no node of its own to read.
  int previous = line_idx;
  while (previous >= 0 && EditorFeatures::is_whitespace(buf.line(previous)))
  {
    previous--;
  }
  if (previous < 0)
  {
    *indent_columns = 0;
    return true;
  }
  const std::string &previous_line = buf.line(previous);
  const uint32_t previous_start = line_start_byte(buf, previous);
  const uint32_t previous_end = previous_start + (uint32_t)previous_line.size();
  const std::vector<std::pair<TSNode, IndentMarks>> captures =
      gather_indent_captures(buf, query, root, previous_start, previous_end);

  uint32_t column = last_content_column(previous_line);
  if (column == UINT32_MAX)
  {
    *indent_columns = 0;
    return true;
  }
  // A trailing comment is not what the line ends with as far as the grammar of
  // the code is concerned: `end -- done` ends with `end`. A comment that starts
  // the line is the line's content, and is read as it is.
  TSNode node = node_at_byte(tree, previous_start + column);
  if (!ts_node_is_null(node) && std::string(ts_node_type(node)).find("comment") != std::string::npos)
  {
    const uint32_t comment_column = ts_node_start_point(node).column;
    const std::string before_comment = previous_line.substr(0, std::min<size_t>(comment_column, previous_line.size()));
    const size_t code_column = before_comment.find_last_not_of(" \t");
    if (code_column != std::string::npos)
    {
      node = node_at_byte(tree, previous_start + (uint32_t)code_column);
    }
  }
  // A node that closes an indented region (`")" @indent.end`) ends the walk:
  // the line below it belongs to the construct *after* it, so the walk starts
  // from the node the new line would hold instead.
  if (!ts_node_is_null(node) && indent_marks_for(captures, node).end)
  {
    node = node_at_byte(tree, previous_end + 1);
  }

  if (!ts_node_is_null(node) && indent_marks_for(captures, node).zero)
  {
    *indent_columns = 0;
    return true;
  }

  int indent = 0;
  // Which rows have already contributed a step, so nested nodes starting on one
  // row cannot each add their own: that is what keeps `[ (a) (b) ] @indent.begin`
  // from indenting a line twice.
  std::unordered_map<int, bool> processed_rows;
  while (!ts_node_is_null(node))
  {
    const IndentMarks marks = indent_marks_for(captures, node);
    const int start_row = (int)ts_node_start_point(node).row;
    const int end_row = (int)ts_node_end_point(node).row;
    const bool spans_target = start_row < target_row && target_row <= end_row;
    // A node the query marks auto/ignore owns the indentation of its own
    // contents (a multi-line string, a comment, an argument list): the user's
    // indentation stands, or the line goes to column 0.
    if (!marks.begin && !marks.align && marks.auto_indent && spans_target)
    {
      *indent_columns = EditorFeatures::get_indent_level(previous_line);
      return true;
    }
    if (!marks.begin && marks.ignore && spans_target)
    {
      *indent_columns = 0;
      return true;
    }
    const bool should_process = !processed_rows[start_row];
    bool processed = false;
    // A branch (`else`, `elif`) starts a region one step out, but only the line
    // the branch itself is on is dedented: the line after it is its body. A
    // dedent applies to the lines after the node instead.
    if (should_process
        && ((marks.branch && start_row == target_row) || (marks.dedent && start_row != target_row)))
    {
      indent -= (int)shift;
      processed = true;
    }
    bool in_error = false;
    if (should_process)
    {
      TSNode parent = ts_node_parent(node);
      in_error = !ts_node_is_null(parent) && ts_node_has_error(parent);
    }
    // An open block steps in. A node that begins and ends on one row does not
    // unless the query says the block is immediate or it sits inside a parse
    // error (typing a block before its body exists), and a node starting on the
    // line being indented does not either unless the query asks for that.
    if (should_process && marks.begin && (start_row != end_row || in_error || marks.properties.immediate)
        && (start_row != target_row || marks.properties.start_at_same_line))
    {
      indent += (int)shift;
      processed = true;
    }
    processed_rows[start_row] = processed_rows[start_row] || processed;
    node = ts_node_parent(node);
  }
  *indent_columns = std::max(0, indent);
  return true;
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
    cache.colors =
        query_ts_highlights(buf, line_idx, query, buf.ts_tree, limit, ts_extension, ts_manager_);
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
