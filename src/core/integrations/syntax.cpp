#include "editor.h"
#include "tree_sitter/manager.h"
#include <algorithm>
#include <filesystem>

#ifdef JOT_TREESITTER
#include <tree_sitter/api.h>
#endif

namespace {

#ifdef JOT_TREESITTER
bool contains_any(const std::string &text,
                  const std::vector<std::string> &needles) {
  for (const auto &needle : needles) {
    if (text.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool header_content_looks_like_cpp(const FileBuffer &buf) {
  std::string sample;
  const size_t max_lines = std::min<size_t>(buf.line_count(), 200);
  for (size_t i = 0; i < max_lines; ++i) {
    sample += buf.line((int)i);
    sample += '\n';
    if (sample.size() > 32768) {
      break;
    }
  }

  return contains_any(sample, {
      "namespace ", "class ", "template", "typename", "public:",
      "private:", "protected:", "std::", "::", "constexpr", "noexcept",
      "override", "final", "operator", "#include <vector>",
      "#include <string>", "#include <memory>", "#include <iostream>",
  });
}

bool has_cpp_sibling_source(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  std::filesystem::path header(path);
  std::filesystem::path parent = header.parent_path();
  std::filesystem::path stem = header.stem();
  if (parent.empty() || stem.empty()) {
    return false;
  }
  for (const auto &ext : {".cpp", ".cc", ".cxx", ".C"}) {
    if (std::filesystem::exists(parent / (stem.string() + ext), ec)) {
      return true;
    }
  }
  return false;
}

std::vector<std::pair<int, int>>
query_ts_highlights(FileBuffer &buf, int line_idx, TSQuery *query,
                    TSTree *tree) {
  if (!query || !tree) {
    return {};
  }

  const std::string &line = buf.line(line_idx);
  std::vector<std::pair<int, int>> colors(line.size(), {0, 0});
  std::vector<int> priorities(line.size(), 0);

  uint32_t line_start_byte = 0;
  for (int i = 0; i < line_idx; i++) {
    line_start_byte += (uint32_t)buf.line(i).size() + 1;
  }

  TSNode root = ts_tree_root_node(tree);
  TSQueryCursor *cursor = ts_query_cursor_new();

  ts_query_cursor_set_byte_range(cursor, line_start_byte,
                                 line_start_byte + (uint32_t)line.size());
  ts_query_cursor_exec(cursor, query, root);

  TSQueryMatch match;
  while (ts_query_cursor_next_match(cursor, &match)) {
    for (uint16_t ci = 0; ci < match.capture_count; ci++) {
      TSQueryCapture cap = match.captures[ci];
      uint32_t s = ts_node_start_byte(cap.node);
      uint32_t e = ts_node_end_byte(cap.node);
      uint32_t start = (s > line_start_byte) ? (s - line_start_byte) : 0;
      uint32_t end = (e > line_start_byte) ? (e - line_start_byte) : 0;
      if (end > (uint32_t)line.size()) {
        end = (uint32_t)line.size();
      }

      uint32_t name_len;
      const char *name =
          ts_query_capture_name_for_id(query, cap.index, &name_len);
      const int token = tree_sitter_capture_token_for_name(
          std::string(name, name_len));
      const int priority = tree_sitter_capture_priority_for_name(
          std::string(name, name_len));
      if (token == TS_TOKEN_NONE) {
        continue;
      }

      for (uint32_t col = start; col < end && col < colors.size(); col++) {
        if (priority >= priorities[col]) {
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
void Editor::reparse_tree(FileBuffer &buf) {
  if (!buf.ts_parser) {
    return;
  }
  std::string text = get_buffer_text(buf);
  TSTree *new_tree = ts_parser_parse_string(
      buf.ts_parser, buf.ts_tree, text.c_str(), (uint32_t)text.size());
  if (buf.ts_tree) {
    ts_tree_delete(buf.ts_tree);
  }
  buf.ts_tree = new_tree;
  buf.syntax_cache.clear();
}

void Editor::init_ts_for_buffer(FileBuffer &buf) {
  std::string ext = tree_sitter_extension_for_buffer(buf);
  std::string language_id = ts_manager_.language_id_for_extension(ext);
  if (language_id.empty()) {
    if (buf.ts_tree) {
      ts_tree_delete(buf.ts_tree);
      buf.ts_tree = nullptr;
    }
    if (buf.ts_parser) {
      ts_parser_delete(buf.ts_parser);
      buf.ts_parser = nullptr;
    }
    buf.ts_language_id.clear();
    return;
  }

  if (buf.ts_parser && buf.ts_language_id == language_id) {
    if (!buf.ts_tree) {
      reparse_tree(buf);
    }
    return;
  }

  if (buf.ts_tree) {
    ts_tree_delete(buf.ts_tree);
    buf.ts_tree = nullptr;
  }
  if (buf.ts_parser) {
    ts_parser_delete(buf.ts_parser);
    buf.ts_parser = nullptr;
  }
  buf.ts_language_id.clear();

  TSParser *parser = ts_manager_.create_parser(ext);
  if (!parser) {
    return;
  }
  buf.ts_parser = parser;
  buf.ts_language_id = language_id;
  buf.syntax_cache.clear();
  reparse_tree(buf);
}

std::string Editor::tree_sitter_extension_for_buffer(const FileBuffer &buf) {
  std::string ext = get_file_extension(buf.filepath);
  if (ext != ".h" || ts_manager_.has_language_override(ext)) {
    return ext;
  }
  if (has_cpp_sibling_source(buf.filepath) || header_content_looks_like_cpp(buf)) {
    return ".cpp";
  }
  return ext;
}
#endif

const std::vector<std::pair<int, int>> &
Editor::get_line_syntax_colors(FileBuffer &buf, int line_idx) {
  static const std::vector<std::pair<int, int>> empty_colors;

  if (line_idx < 0 || line_idx >= (int)buf.line_count()) {
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
  if (buf.syntax_cache_extension != cache_extension) {
    buf.syntax_cache_extension = cache_extension;
    buf.syntax_cache_line_count = buf.line_count();
    buf.syntax_cache.clear();
    buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
    buf.syntax_language_label.clear();
  }

  if (buf.syntax_cache_line_count != buf.line_count()) {
    buf.syntax_cache_line_count = buf.line_count();
    buf.syntax_cache.clear();
  }

#ifdef JOT_TREESITTER
  const bool tree_sitter_candidate = ts_manager_.has_language(ts_extension);
  if (tree_sitter_candidate) {
    init_ts_for_buffer(buf);
  }
#endif

  const std::string &line = buf.line(line_idx);
  SyntaxLineCache &cache = buf.syntax_cache[line_idx];
  const std::size_t line_hash = std::hash<std::string>{}(line);

  bool retry_tree_sitter = false;
#ifdef JOT_TREESITTER
  retry_tree_sitter =
      tree_sitter_candidate && buf.ts_tree &&
      buf.syntax_engine != SYNTAX_ENGINE_TREESITTER;
#endif
  if (!retry_tree_sitter && cache.valid && cache.line_hash == line_hash &&
      cache.line_length == line.length()) {
    return cache.colors;
  }

#ifdef JOT_TREESITTER
  if (tree_sitter_candidate && buf.ts_tree) {
    TSQuery *query = ts_manager_.get_highlight_query(ts_extension);
    if (query) {
      cache.colors = query_ts_highlights(buf, line_idx, query, buf.ts_tree);
      cache.line_hash = line_hash;
      cache.line_length = line.length();
      cache.valid = true;
      buf.syntax_engine = SYNTAX_ENGINE_TREESITTER;
      buf.syntax_language_label =
          ts_manager_.language_id_for_extension(ts_extension);
      return cache.colors;
    }
  }
#endif

  highlighter.set_language(raw_extension);
  cache.colors = highlighter.get_colors(line);
  if (highlighter.has_rules()) {
    buf.syntax_engine = SYNTAX_ENGINE_REGEX;
    buf.syntax_language_label = raw_extension.empty() ? "plain" : raw_extension;
  } else {
    buf.syntax_engine = SYNTAX_ENGINE_NONE;
    buf.syntax_language_label.clear();
  }
  cache.line_hash = line_hash;
  cache.line_length = line.length();
  cache.valid = true;
  return cache.colors;
}

void Editor::invalidate_syntax_cache(FileBuffer &buf) {
  buf.syntax_cache_extension.clear();
  buf.syntax_cache_line_count = 0;
  buf.syntax_cache.clear();
  buf.syntax_engine = SYNTAX_ENGINE_UNKNOWN;
  buf.syntax_language_label.clear();
}
