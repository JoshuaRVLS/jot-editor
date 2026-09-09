// Internal helpers shared by the render modules (src/render/*.cpp):
// markdown-fence language detection for hover/modals and the marked-text
// drawer used by the palette rows.
#pragma once

#include "features/syntax_highlighter.h"
#include "tree_sitter/manager.h"
#include "ui/components.h"
#include "ui/text.h"
#include <algorithm>
#include <string>
#include <vector>

namespace ui_internal
{
inline std::string trim_hover_fence_language(std::string lang)
{
  size_t start = 0;
  while (start < lang.size() && std::isspace((unsigned char)lang[start]))
  {
    start++;
  }
  size_t end = start;
  while (end < lang.size() && !std::isspace((unsigned char)lang[end]))
  {
    end++;
  }
  lang = lang.substr(start, end - start);
  std::transform(lang.begin(),
                 lang.end(),
                 lang.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return lang;
}

inline bool hover_markdown_fence_language(const std::string &line, std::string *language)
{
  size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
  {
    start++;
  }
  if (line.compare(start, 3, "```") != 0)
  {
    return false;
  }
  if (language)
  {
    *language = trim_hover_fence_language(line.substr(start + 3));
  }
  return true;
}

inline std::string hover_language_extension(const std::string &lang)
{
  if (lang == "c++" || lang == "cpp" || lang == "cc" || lang == "cxx")
  {
    return ".cpp";
  }
  if (lang == "c")
  {
    return ".c";
  }
  if (lang == "python" || lang == "py")
  {
    return ".py";
  }
  if (lang == "javascript" || lang == "js")
  {
    return ".js";
  }
  if (lang == "jsx")
  {
    return ".jsx";
  }
  if (lang == "typescript" || lang == "ts")
  {
    return ".ts";
  }
  if (lang == "tsx")
  {
    return ".tsx";
  }
  if (lang == "rust" || lang == "rs")
  {
    return ".rs";
  }
  if (lang == "go" || lang == "golang")
  {
    return ".go";
  }
  if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh")
  {
    return ".sh";
  }
  if (lang == "json")
  {
    return ".json";
  }
  if (lang == "html")
  {
    return ".html";
  }
  if (lang == "css")
  {
    return ".css";
  }
  if (lang == "xml")
  {
    return ".xml";
  }
  if (lang == "yaml" || lang == "yml")
  {
    return ".yaml";
  }
  if (lang == "toml")
  {
    return ".toml";
  }
  if (lang == "markdown" || lang == "md")
  {
    return ".md";
  }
  if (lang == "cmake")
  {
    return ".cmake";
  }
  if (lang == "make" || lang == "makefile")
  {
    return ".make";
  }
  if (lang == "dockerfile")
  {
    return ".dockerfile";
  }
  return "";
}

inline int hover_syntax_color(const Theme &theme, int token)
{
  switch (token)
  {
  case TS_TOKEN_KEYWORD:
    return theme.fg_keyword;
  case TS_TOKEN_STRING:
    return theme.fg_string;
  case TS_TOKEN_COMMENT:
    return theme.fg_comment;
  case TS_TOKEN_NUMBER:
    return theme.fg_number;
  case TS_TOKEN_TYPE:
    return theme.fg_type;
  case TS_TOKEN_FUNCTION:
    return theme.fg_function;
  case TS_TOKEN_VARIABLE:
    return theme.fg_variable;
  case TS_TOKEN_CONSTANT:
    return theme.fg_constant;
  case TS_TOKEN_BUILTIN:
    return theme.fg_builtin;
  case TS_TOKEN_OPERATOR:
    return theme.fg_operator;
  case TS_TOKEN_PUNCTUATION:
    return theme.fg_punctuation;
  case TS_TOKEN_TAG:
    return theme.fg_tag;
  case TS_TOKEN_ATTRIBUTE:
    return theme.fg_attribute;
  case TS_TOKEN_NAMESPACE:
    return theme.fg_namespace;
  case TS_TOKEN_MODULE:
    return theme.fg_module;
  case TS_TOKEN_PARAMETER:
    return theme.fg_parameter;
  case TS_TOKEN_FIELD:
    return theme.fg_field;
  case TS_TOKEN_KEYWORD_CONTROL:
    return theme.fg_keyword_control;
  case TS_TOKEN_KEYWORD_STORAGE:
    return theme.fg_keyword_storage;
  case TS_TOKEN_KEYWORD_PREPROC:
    return theme.fg_keyword_preproc;
  case TS_TOKEN_FUNCTION_METHOD:
    return theme.fg_function_method;
  case TS_TOKEN_FUNCTION_CONSTRUCTOR:
    return theme.fg_function_constructor;
  case TS_TOKEN_TYPE_BUILTIN:
    return theme.fg_type_builtin;
  case TS_TOKEN_CONSTANT_MACRO:
    return theme.fg_constant_macro;
  case TS_TOKEN_STRING_ESCAPE:
    return theme.fg_string_escape;
  case TS_TOKEN_PUNCTUATION_BRACKET:
    return theme.fg_punctuation_bracket;
  case TS_TOKEN_PUNCTUATION_DELIMITER:
    return theme.fg_punctuation_delimiter;
  default:
    return theme.fg_command;
  }
}

inline void draw_hover_code_line(UI *ui,
                          int x,
                          int y,
                          int w,
                          const std::string &line,
                          const std::string &extension,
                          const Theme &theme)
{
  if (w <= 0)
  {
    return;
  }
  std::string clipped = ui_truncate_cells(line, w);
  if (extension.empty())
  {
    ui->draw_text(x, y, clipped, theme.fg_command, theme.bg_command);
    return;
  }

  SyntaxHighlighter highlighter;
  highlighter.set_language(extension);
  auto colors = highlighter.get_colors(clipped);
  int chunk_start = 0;
  int chunk_token = 0;

  for (int i = 0; i <= (int)clipped.size(); i++)
  {
    int token = 0;
    if (i < (int)colors.size() && colors[i].first == 1)
    {
      token = colors[i].second;
    }
    if (i == 0)
    {
      chunk_token = token;
    }
    if (i == (int)clipped.size() || token != chunk_token)
    {
      if (i > chunk_start)
      {
        ui->draw_text(x + chunk_start,
                      y,
                      clipped.substr(chunk_start, i - chunk_start),
                      hover_syntax_color(theme, chunk_token),
                      theme.bg_command);
      }
      chunk_start = i;
      chunk_token = token;
    }
  }
}

inline void ui_draw_marked_text(UI &ui,
                         int x,
                         int y,
                         const std::string &text,
                         const std::vector<int> &match,
                         int fg,
                         int bg,
                         int match_fg,
                         bool plain_bold = false)
{
  if (match.empty())
  {
    ui.draw_text(x, y, text, fg, bg, plain_bold);
    return;
  }
  std::vector<char> hit(text.size(), 0);
  for (int m : match)
  {
    if (m >= 0 && m < (int)text.size())
    {
      hit[(size_t)m] = 1;
    }
  }
  int cx = x;
  int i = 0;
  while (i < (int)text.size())
  {
    int len = std::max(1, ui_utf8_char_len(text, i));
    bool is_hit = hit[(size_t)i] != 0;
    int run = i + len;
    while (run < (int)text.size())
    {
      int l = std::max(1, ui_utf8_char_len(text, run));
      if ((hit[(size_t)run] != 0) != is_hit)
      {
        break;
      }
      run += l;
    }
    std::string seg = text.substr((size_t)i, (size_t)(run - i));
    ui.draw_text(cx, y, seg, is_hit ? match_fg : fg, bg, is_hit || plain_bold);
    cx += ui_cell_count(seg);
    i = run;
  }
}
} // namespace ui_internal
